#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gmp.h>

#define STACK_SIZE 1000
#define DICT_SIZE 100
#define WORD_CODE_SIZE 256
#define CONTROL_STACK_SIZE 100
#define LOOP_STACK_SIZE 100
#define MAX_STRING_SIZE 256

typedef enum {
    OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_DUP, OP_SWAP, OP_OVER,
    OP_ROT, OP_DROP, OP_EQ, OP_LT, OP_GT, OP_AND, OP_OR, OP_NOT, OP_I,
    OP_DO, OP_LOOP, OP_BRANCH_FALSE, OP_BRANCH, OP_CALL, OP_END, OP_DOT_QUOTE,
    OP_CR, OP_DOT_S, OP_FLUSH, OP_CASE, OP_OF, OP_ENDOF, OP_DOT, OP_ENDCASE
} OpCode;

typedef struct {
    OpCode opcode;
    long int operand;
} Instruction;

typedef struct {
    char *name;
    Instruction code[WORD_CODE_SIZE];
    long int code_length;
    char *strings[WORD_CODE_SIZE];
    long int string_count;
} CompiledWord;

typedef struct {
    mpz_t data[STACK_SIZE];
    long int top;
} Stack;

typedef enum { CT_IF, CT_BEGIN, CT_DO, CT_CASE, CT_OF, CT_ENDOF } ControlType;
typedef struct {
    ControlType type;
    long int addr;
} ControlEntry;

ControlEntry control_stack[CONTROL_STACK_SIZE];
int control_stack_top = 0;

typedef struct {
    mpz_t index;
    mpz_t limit;
    long int addr;
} LoopControl;

LoopControl loop_stack[LOOP_STACK_SIZE];
long int loop_stack_top = -1;

CompiledWord dictionary[DICT_SIZE];
long int dict_count = 0;
long int current_word_index = -1;

void interpret(char *input, Stack *stack);

void initStack(Stack *stack) {
    stack->top = -1;
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_init(stack->data[i]);
    }
}

void clearStack(Stack *stack) {
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(stack->data[i]);
    }
}

void push(Stack *stack, mpz_t value) {
    if (stack->top < STACK_SIZE - 1) {
        mpz_set(stack->data[++stack->top], value);
    } else {
        printf("Débordement de pile !\n");
    }
}

void pop(Stack *stack, mpz_t result) {
    if (stack->top >= 0) {
        mpz_set(result, stack->data[stack->top--]);
    } else {
        printf("Pile vide !\n");
        mpz_set_ui(result, 0);
    }
}

void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word);

void executeCompiledWord(CompiledWord *word, Stack *stack) {
    long int ip = 0;
    while (ip < word->code_length) {
        executeInstruction(word->code[ip], stack, &ip, word);
        ip++;
    }
}

void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word) {
    mpz_t a, b, result;
    mpz_init(a); mpz_init(b); mpz_init(result);

    switch (instr.opcode) {
        case OP_PUSH: 
            mpz_set_si(result, instr.operand); 
            push(stack, result); 
            break;
        case OP_ADD: 
            pop(stack, a); 
            pop(stack, b); 
            mpz_add(result, b, a); 
            push(stack, result); 
            break;
        case OP_SUB: 
            pop(stack, a); 
            pop(stack, b); 
            mpz_sub(result, b, a); 
            push(stack, result); 
            break;
        case OP_MUL: 
            pop(stack, a); 
            pop(stack, b); 
            mpz_mul(result, b, a); 
            push(stack, result); 
            break;
        case OP_DIV: 
            pop(stack, a); 
            pop(stack, b); 
            if (mpz_cmp_si(a, 0) != 0) {
                mpz_div(result, b, a); 
                push(stack, result);
            } else {
                printf("Division par zéro !\n");
            }
            break;
        case OP_DUP: 
            pop(stack, a); 
            push(stack, a); 
            push(stack, a); 
            break;
        case OP_SWAP: 
            pop(stack, a); 
            pop(stack, b); 
            push(stack, a); 
            push(stack, b); 
            break;
        case OP_OVER: 
            pop(stack, a); 
            pop(stack, b); 
            push(stack, b); 
            push(stack, a); 
            push(stack, b); 
            break;
        case OP_ROT: 
            if (stack->top >= 2) {
                mpz_set(a, stack->data[stack->top - 2]);
                mpz_set(b, stack->data[stack->top - 1]);
                mpz_set(result, stack->data[stack->top]);
                mpz_set(stack->data[stack->top - 2], b);
                mpz_set(stack->data[stack->top - 1], result);
                mpz_set(stack->data[stack->top], a);
            } else {
                printf("Pile insuffisante pour ROT !\n");
            }
            break;
        case OP_DROP: 
            pop(stack, a); 
            break;
        case OP_DOT: 
            pop(stack, a); 
            gmp_printf("%Zd\n", a); 
            break;
        case OP_FLUSH: 
            for (int i = 0; i <= stack->top; i++) {
                mpz_clear(stack->data[i]);
                mpz_init(stack->data[i]);
            }
            stack->top = -1; 
            break;
        case OP_EQ: 
            pop(stack, a); 
            pop(stack, b); 
            mpz_set_si(result, mpz_cmp(b, a) == 0 ? 1 : 0); 
            push(stack, result); 
            break;
        case OP_LT: 
            pop(stack, a); 
            pop(stack, b); 
            mpz_set_si(result, mpz_cmp(b, a) < 0 ? 1 : 0); 
            push(stack, result); 
            break;
        case OP_GT: 
            pop(stack, a); 
            pop(stack, b); 
            mpz_set_si(result, mpz_cmp(b, a) > 0 ? 1 : 0); 
            push(stack, result); 
            break;
        case OP_AND: 
            pop(stack, a); 
            pop(stack, b); 
            mpz_set_si(result, (mpz_cmp_si(b, 0) != 0 && mpz_cmp_si(a, 0) != 0) ? 1 : 0); 
            push(stack, result); 
            break;
        case OP_OR: 
            pop(stack, a); 
            pop(stack, b); 
            mpz_set_si(result, (mpz_cmp_si(b, 0) != 0 || mpz_cmp_si(a, 0) != 0) ? 1 : 0); 
            push(stack, result); 
            break;
        case OP_NOT: 
            pop(stack, a); 
            mpz_set_si(result, mpz_cmp_si(a, 0) == 0 ? 1 : 0); 
            push(stack, result); 
            break;
        case OP_I: 
            if (loop_stack_top >= 0) push(stack, loop_stack[loop_stack_top].index); 
            else printf("I utilisé hors d'une boucle !\n"); 
            break;
        case OP_DO: 
            pop(stack, a); 
            pop(stack, b); 
            if (loop_stack_top < LOOP_STACK_SIZE - 1) {
                mpz_init(loop_stack[++loop_stack_top].index);
                mpz_init(loop_stack[loop_stack_top].limit);
                mpz_set(loop_stack[loop_stack_top].index, b);
                mpz_set(loop_stack[loop_stack_top].limit, a);
                loop_stack[loop_stack_top].addr = *ip + 1;
            } else printf("Débordement de pile de boucles !\n"); 
            break;
        case OP_LOOP: 
            if (loop_stack_top >= 0) {
                mpz_add_ui(loop_stack[loop_stack_top].index, loop_stack[loop_stack_top].index, 1);
                if (mpz_cmp(loop_stack[loop_stack_top].index, loop_stack[loop_stack_top].limit) < 0) {
                    *ip = loop_stack[loop_stack_top].addr - 1;
                } else {
                    mpz_clear(loop_stack[loop_stack_top].index);
                    mpz_clear(loop_stack[loop_stack_top].limit);
                    loop_stack_top--;
                }
            } else printf("LOOP sans DO !\n"); 
            break;
        case OP_BRANCH_FALSE: 
            pop(stack, a); 
            if (mpz_cmp_si(a, 0) == 0) *ip = instr.operand - 1; 
            break;
        case OP_BRANCH: 
            *ip = instr.operand - 1; 
            break;
        case OP_CALL: 
            if (instr.operand >= 0 && instr.operand < word->string_count) {
                FILE *file = fopen(word->strings[instr.operand], "r");
                if (!file) {
                    printf("Impossible d'ouvrir le fichier : %s\n", word->strings[instr.operand]);
                    break;
                }
                char line[MAX_STRING_SIZE];
                while (fgets(line, sizeof(line), file)) {
                    line[strcspn(line, "\n")] = 0;
                    interpret(line, stack);
                }
                fclose(file);
            } else if (instr.operand >= 0 && instr.operand < dict_count) {
                executeCompiledWord(&dictionary[instr.operand], stack);
            } else {
                printf("Index CALL invalide : %ld\n", instr.operand);
            }
            break;
        case OP_END: break;
        case OP_DOT_QUOTE: 
            if (instr.operand >= 0 && instr.operand < word->string_count) {
                printf("%s", word->strings[instr.operand]);
            } else printf("Index de chaîne invalide pour .\": %ld\n", instr.operand);
            break;
        case OP_CR: printf("\n"); break;
        case OP_DOT_S: 
            printf("Pile : ");
            for (int i = 0; i <= stack->top; i++) gmp_printf("%Zd ", stack->data[i]);
            printf("\n"); 
            break;
        case OP_CASE: break;
        case OP_OF: 
            pop(stack, a); 
            pop(stack, b); 
            if (mpz_cmp(a, b) != 0) {
                push(stack, b);
                *ip = instr.operand - 1;
            }
            break;
        case OP_ENDOF: 
            *ip = instr.operand - 1; 
            break;
        case OP_ENDCASE: 
            pop(stack, a); 
            break;
    }
    mpz_clear(a); mpz_clear(b); mpz_clear(result);
}

void addCompiledWord(char *name, Instruction *code, long int code_length, char **strings, long int string_count) {
    if (dict_count < DICT_SIZE) {
        dictionary[dict_count].name = strdup(name);
        memcpy(dictionary[dict_count].code, code, code_length * sizeof(Instruction));
        dictionary[dict_count].code_length = code_length;
        dictionary[dict_count].string_count = string_count;
        for (int i = 0; i < string_count; i++) {
            dictionary[dict_count].strings[i] = strings[i] ? strdup(strings[i]) : NULL;
        }
        dict_count++;
        current_word_index = dict_count - 1;
    } else {
        printf("Dictionnaire plein !\n");
    }
}

int findCompiledWordIndex(char *name) {
    for (int i = 0; i < dict_count; i++) {
        if (strcmp(dictionary[i].name, name) == 0) return i;
    }
    return -1;
}

CompiledWord currentWord;
void compileToken(char *token, char **input_rest) {
    Instruction instr;
    if (strcmp(token, "+") == 0) instr.opcode = OP_ADD;
    else if (strcmp(token, "-") == 0) instr.opcode = OP_SUB;
    else if (strcmp(token, "*") == 0) instr.opcode = OP_MUL;
    else if (strcmp(token, "/") == 0) instr.opcode = OP_DIV;
    else if (strcmp(token, "DUP") == 0) instr.opcode = OP_DUP;
    else if (strcmp(token, "SWAP") == 0) instr.opcode = OP_SWAP;
    else if (strcmp(token, "OVER") == 0) instr.opcode = OP_OVER;
    else if (strcmp(token, "ROT") == 0) instr.opcode = OP_ROT;
    else if (strcmp(token, "DROP") == 0) instr.opcode = OP_DROP;
    else if (strcmp(token, "=") == 0) instr.opcode = OP_EQ;
    else if (strcmp(token, "<") == 0) instr.opcode = OP_LT;
    else if (strcmp(token, ">") == 0) instr.opcode = OP_GT;
    else if (strcmp(token, "AND") == 0) instr.opcode = OP_AND;
    else if (strcmp(token, "OR") == 0) instr.opcode = OP_OR;
    else if (strcmp(token, "NOT") == 0) instr.opcode = OP_NOT;
    else if (strcmp(token, "I") == 0) instr.opcode = OP_I;
    else if (strcmp(token, "CR") == 0) instr.opcode = OP_CR;
    else if (strcmp(token, ".S") == 0) instr.opcode = OP_DOT_S;
    else if (strcmp(token, ".") == 0) instr.opcode = OP_DOT;
    else if (strcmp(token, "FLUSH") == 0) instr.opcode = OP_FLUSH;
    else if (strcmp(token, "LOAD") == 0) {
        char *start = *input_rest;
        while (*start && (*start == ' ' || *start == '\t')) start++;
        if (*start != '"') {
            printf("LOAD attend un nom de fichier entre guillemets\n");
            return;
        }
        start++;
        char *end = strchr(start, '"');
        if (!end) {
            printf("Guillemet fermant manquant pour LOAD\n");
            return;
        }
        long int len = end - start;
        char *filename = malloc(len + 1);
        strncpy(filename, start, len);
        filename[len] = '\0';
        instr.opcode = OP_CALL;
        instr.operand = currentWord.string_count;
        currentWord.strings[currentWord.string_count++] = filename;
        currentWord.code[currentWord.code_length++] = instr;
        *input_rest = end + 1;
        return;
    }
    else if (strcmp(token, ".\"") == 0) {
        char *start = *input_rest;
        char *end = strchr(start, '"');
        if (!end) {
            printf("Guillemet fermant manquant pour .\"\n");
            return;
        }
        long int len = end - start;
        char *str = malloc(len + 1);
        strncpy(str, start, len);
        str[len] = '\0';
        instr.opcode = OP_DOT_QUOTE;
        instr.operand = currentWord.string_count;
        currentWord.strings[currentWord.string_count++] = str;
        currentWord.code[currentWord.code_length++] = instr;
        *input_rest = end + 1;
        return;
    }
    else if (strcmp(token, "CASE") == 0) {
        instr.opcode = OP_CASE;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_CASE, currentWord.code_length - 1};
        return;
    }
    else if (strcmp(token, "OF") == 0) {
        instr.opcode = OP_OF;
        instr.operand = 0;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_OF, currentWord.code_length - 1};
        return;
    }
    else if (strcmp(token, "ENDOF") == 0) {
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_OF) {
            instr.opcode = OP_ENDOF;
            instr.operand = 0;
            currentWord.code[currentWord.code_length++] = instr;
            currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
            control_stack[control_stack_top++] = (ControlEntry){CT_ENDOF, currentWord.code_length - 1};
        } else printf("ENDOF sans OF !\n");
        return;
    }
    else if (strcmp(token, "ENDCASE") == 0) {
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_ENDOF) {
            instr.opcode = OP_ENDCASE;
            currentWord.code[currentWord.code_length++] = instr;
            while (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_ENDOF) {
                currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
            }
            control_stack_top--;
        } else printf("ENDCASE sans CASE !\n");
        return;
    }
    else {
        char *endptr;
        long int value = strtol(token, &endptr, 10);
        if (*endptr == '\0') {
            instr.opcode = OP_PUSH;
            instr.operand = value;
        } else {
            long int index = findCompiledWordIndex(token);
            if (index >= 0) {
                instr.opcode = OP_CALL;
                instr.operand = index;
            } else {
                printf("Mot inconnu : %s\n", token);
                return;
            }
        }
    }
    currentWord.code[currentWord.code_length++] = instr;
}

int compiling = 0;
void interpret(char *input, Stack *stack) {
    char *saveptr;
    char *token = strtok_r(input, " \t\n", &saveptr);
    while (token) {
        if (compiling) {
            if (strcmp(token, ";") == 0) {
                Instruction end = {OP_END, 0};
                currentWord.code[currentWord.code_length++] = end;
                if (current_word_index >= 0 && current_word_index < dict_count) {
                    memcpy(dictionary[current_word_index].code, currentWord.code, currentWord.code_length * sizeof(Instruction));
                    dictionary[current_word_index].code_length = currentWord.code_length;
                    for (int i = 0; i < currentWord.string_count; i++) {
                        dictionary[current_word_index].strings[i] = currentWord.strings[i];
                    }
                    dictionary[current_word_index].string_count = currentWord.string_count;
                }
                free(currentWord.name);
                compiling = 0;
                current_word_index = -1;
            } else if (strcmp(token, "IF") == 0) {
                Instruction instr = {OP_BRANCH_FALSE, 0};
                currentWord.code[currentWord.code_length++] = instr;
                control_stack[control_stack_top++] = (ControlEntry){CT_IF, currentWord.code_length - 1};
            } else if (strcmp(token, "ELSE") == 0) {
                Instruction instr = {OP_BRANCH, 0};
                currentWord.code[currentWord.code_length++] = instr;
                if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_IF) {
                    currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
                    control_stack[control_stack_top++] = (ControlEntry){CT_IF, currentWord.code_length - 1};
                }
            } else if (strcmp(token, "THEN") == 0) {
                if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_IF) {
                    currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
                }
            } else if (strcmp(token, "BEGIN") == 0) {
                control_stack[control_stack_top++] = (ControlEntry){CT_BEGIN, currentWord.code_length};
            } else if (strcmp(token, "UNTIL") == 0) {
                if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_BEGIN) {
                    Instruction instr = {OP_BRANCH_FALSE, control_stack[--control_stack_top].addr};
                    currentWord.code[currentWord.code_length++] = instr;
                }
            } else if (strcmp(token, "DO") == 0) {
                Instruction instr = {OP_DO, 0};
                currentWord.code[currentWord.code_length++] = instr;
                control_stack[control_stack_top++] = (ControlEntry){CT_DO, currentWord.code_length - 1};
            } else if (strcmp(token, "LOOP") == 0) {
                if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_DO) {
                    Instruction instr = {OP_LOOP, 0};
                    currentWord.code[currentWord.code_length++] = instr;
                    control_stack_top--;
                }
            } else {
                compileToken(token, &saveptr);
            }
        } else {
            CompiledWord temp = {.code_length = 1};
            mpz_t big_value;
            mpz_init(big_value);
            if (mpz_set_str(big_value, token, 10) == 0) {
                //printf("Nombre détecté: "); // Débogage
                //gmp_printf("%Zd\n", big_value); // Débogage
                push(stack, big_value);
            } else if (strcmp(token, "LOAD") == 0) {
                char *start = saveptr;
                while (*start && (*start == ' ' || *start == '\t')) start++;
                if (*start != '"') {
                    printf("LOAD attend un nom de fichier entre guillemets\n");
                    return;
                }
                start++;
                char *end = strchr(start, '"');
                if (!end) {
                    printf("Guillemet fermant manquant pour LOAD\n");
                    return;
                }
                long int len = end - start;
                char filename[MAX_STRING_SIZE];
                strncpy(filename, start, len);
                filename[len] = '\0';
                FILE *file = fopen(filename, "r");
                if (!file) {
                    printf("Impossible d'ouvrir le fichier : %s\n", filename);
                } else {
                    char line[MAX_STRING_SIZE];
                    while (fgets(line, sizeof(line), file)) {
                        line[strcspn(line, "\n")] = 0;
                        interpret(line, stack);
                    }
                    fclose(file);
                }
                saveptr = end + 1;
            } else if (strcmp(token, ".") == 0) {
                Instruction instr = {OP_DOT, 0};
                executeInstruction(instr, stack, NULL, NULL);
            } else if (strcmp(token, "FLUSH") == 0) {
                Instruction instr = {OP_FLUSH, 0};
                executeInstruction(instr, stack, NULL, NULL);
            } else if (strcmp(token, ":") == 0) {
                token = strtok_r(NULL, " \t\n", &saveptr);
                if (token) {
                    compiling = 1;
                    currentWord.name = strdup(token);
                    currentWord.code_length = 0;
                    currentWord.string_count = 0;
                    addCompiledWord(currentWord.name, currentWord.code, currentWord.code_length, 
                                    currentWord.strings, currentWord.string_count);
                }
            } else if (strcmp(token, ".\"") == 0) {
                char *start = saveptr;
                char *end = strchr(start, '"');
                if (!end) {
                    printf("Guillemet fermant manquant pour .\"\n");
                    return;
                }
                long int len = end - start;
                char *str = malloc(len + 1);
                strncpy(str, start, len);
                str[len] = '\0';
                temp.code[0] = (Instruction){OP_DOT_QUOTE, 0};
                temp.strings[0] = str;
                temp.string_count = 1;
                executeCompiledWord(&temp, stack);
                free(str);
                saveptr = end + 1;
            } else if (strcmp(token, "+") == 0) {
                temp.code[0] = (Instruction){OP_ADD, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "-") == 0) {
                temp.code[0] = (Instruction){OP_SUB, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "*") == 0) {
                temp.code[0] = (Instruction){OP_MUL, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "/") == 0) {
                temp.code[0] = (Instruction){OP_DIV, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "DUP") == 0) {
                temp.code[0] = (Instruction){OP_DUP, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "SWAP") == 0) {
                temp.code[0] = (Instruction){OP_SWAP, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "OVER") == 0) {
                temp.code[0] = (Instruction){OP_OVER, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "ROT") == 0) {
                temp.code[0] = (Instruction){OP_ROT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "DROP") == 0) {
                temp.code[0] = (Instruction){OP_DROP, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "=") == 0) {
                temp.code[0] = (Instruction){OP_EQ, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "<") == 0) {
                temp.code[0] = (Instruction){OP_LT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, ">") == 0) {
                temp.code[0] = (Instruction){OP_GT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "AND") == 0) {
                temp.code[0] = (Instruction){OP_AND, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "OR") == 0) {
                temp.code[0] = (Instruction){OP_OR, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "NOT") == 0) {
                temp.code[0] = (Instruction){OP_NOT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "I") == 0) {
                temp.code[0] = (Instruction){OP_I, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "CR") == 0) {
                temp.code[0] = (Instruction){OP_CR, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, ".S") == 0) {
                temp.code[0] = (Instruction){OP_DOT_S, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, ".") == 0) {
                temp.code[0] = (Instruction){OP_DOT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "FLUSH") == 0) {
                temp.code[0] = (Instruction){OP_FLUSH, 0};
                executeCompiledWord(&temp, stack);
            } else {
                long int index = findCompiledWordIndex(token);
                if (index >= 0) {
                    temp.code[0] = (Instruction){OP_CALL, index};
                    executeCompiledWord(&temp, stack);
                } else {
                    printf("Mot inconnu : %s\n", token);
                }
            }
            mpz_clear(big_value);
        }
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
}

int main() {
    Stack stack;
    initStack(&stack);
    char input[256];
    printf("Interpréteur Forth-like avec GMP\n");
    while (1) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        interpret(input, &stack);
    }
    clearStack(&stack);
    return 0;
}
