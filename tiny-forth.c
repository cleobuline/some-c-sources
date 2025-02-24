#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
    OP_CR, OP_DOT_S, OP_CASE, OP_OF, OP_ENDOF, OP_ENDCASE
} OpCode;

typedef struct {
    OpCode opcode;
    int operand;
} Instruction;

typedef struct {
    char *name;
    Instruction code[WORD_CODE_SIZE];
    int code_length;
    char *strings[WORD_CODE_SIZE];
    int string_count;
} CompiledWord;

typedef struct {
    int data[STACK_SIZE];
    int top;
} Stack;

typedef enum { CT_IF, CT_BEGIN, CT_DO, CT_CASE, CT_OF, CT_ENDOF } ControlType;
typedef struct {
    ControlType type;
    int addr;
} ControlEntry;

ControlEntry control_stack[CONTROL_STACK_SIZE];
int control_stack_top = 0;

typedef struct {
    int index;
    int limit;
    int addr;
} LoopControl;

LoopControl loop_stack[LOOP_STACK_SIZE];
int loop_stack_top = -1;

CompiledWord dictionary[DICT_SIZE];
int dict_count = 0;
void interpret(char *input, Stack *stack);
void push(Stack *stack, int value) {
    if (stack->top < STACK_SIZE - 1) {
        stack->data[++stack->top] = value;
    } else {
        printf("Stack overflow!\n");
    }
}

int pop(Stack *stack) {
    if (stack->top >= 0) {
        return stack->data[stack->top--];
    } else {
        printf("Stack underflow!\n");
        return 0;
    }
}

void executeInstruction(Instruction instr, Stack *stack, int *ip, CompiledWord *word);

void executeCompiledWord(CompiledWord *word, Stack *stack) {
    int ip = 0;
    while (ip < word->code_length) {
        executeInstruction(word->code[ip], stack, &ip, word);
        ip++;
    }
}

void executeInstruction(Instruction instr, Stack *stack, int *ip, CompiledWord *word) {
    switch (instr.opcode) {
        case OP_PUSH: push(stack, instr.operand); break;
        case OP_ADD: { int a = pop(stack); int b = pop(stack); push(stack, b + a); break; }
        case OP_SUB: { int a = pop(stack); int b = pop(stack); push(stack, b - a); break; }
        case OP_MUL: { int a = pop(stack); int b = pop(stack); push(stack, b * a); break; }
        case OP_DIV: { 
            int a = pop(stack); 
            int b = pop(stack); 
            if (a != 0) push(stack, b / a);
            else printf("Division by zero!\n"); 
            break; 
        }
        case OP_DUP: { int a = pop(stack); push(stack, a); push(stack, a); break; }
        case OP_SWAP: { int a = pop(stack); int b = pop(stack); push(stack, a); push(stack, b); break; }
        case OP_OVER: { int a = pop(stack); int b = pop(stack); push(stack, b); push(stack, a); push(stack, b); break; }
        case OP_ROT: {
            if (stack->top >= 2) {
                int a = stack->data[stack->top - 2];
                int b = stack->data[stack->top - 1];
                int c = stack->data[stack->top];
                stack->data[stack->top - 2] = b;
                stack->data[stack->top - 1] = c;
                stack->data[stack->top] = a;
            } else {
                printf("Stack underflow for ROT!\n");
            }
            break;
        }
        case OP_DROP: pop(stack); break;
        case OP_EQ: { int a = pop(stack); int b = pop(stack); push(stack, (b == a) ? 1 : 0); break; }
        case OP_LT: { int a = pop(stack); int b = pop(stack); push(stack, (b < a) ? 1 : 0); break; }
        case OP_GT: { int a = pop(stack); int b = pop(stack); push(stack, (b > a) ? 1 : 0); break; }
        case OP_AND: { int a = pop(stack); int b = pop(stack); push(stack, (b && a) ? 1 : 0); break; }
        case OP_OR: { int a = pop(stack); int b = pop(stack); push(stack, (b || a) ? 1 : 0); break; }
        case OP_NOT: { int a = pop(stack); push(stack, (a == 0) ? 1 : 0); break; }
        case OP_I: {
            if (loop_stack_top >= 0) push(stack, loop_stack[loop_stack_top].index);
            else printf("I used outside of a loop!\n");
            break;
        }
        case OP_DO: {
            int limit = pop(stack);
            int index = pop(stack);
            if (loop_stack_top < LOOP_STACK_SIZE - 1) {
                loop_stack[++loop_stack_top].index = index;
                loop_stack[loop_stack_top].limit = limit;
                loop_stack[loop_stack_top].addr = *ip + 1;
            } else printf("Loop stack overflow!\n");
            break;
        }
        case OP_LOOP: {
            if (loop_stack_top >= 0) {
                loop_stack[loop_stack_top].index++;
                if (loop_stack[loop_stack_top].index < loop_stack[loop_stack_top].limit) {
                    *ip = loop_stack[loop_stack_top].addr - 1;
                } else loop_stack_top--;
            } else printf("LOOP without DO!\n");
            break;
        }
        case OP_BRANCH_FALSE: { if (!pop(stack)) *ip = instr.operand - 1; break; }
        case OP_BRANCH: *ip = instr.operand - 1; break;
        case OP_CALL: {
            if (instr.operand >= 0 && instr.operand < word->string_count) {
                FILE *file = fopen(word->strings[instr.operand], "r");
                if (!file) {
                    printf("Cannot open file: %s\n", word->strings[instr.operand]);
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
                printf("Invalid CALL index: %d\n", instr.operand);
            }
            break;
        }
        case OP_END: break;
        case OP_DOT_QUOTE: {
            if (instr.operand >= 0 && instr.operand < word->string_count) {
                printf("%s", word->strings[instr.operand]);
            } else printf("Invalid string index for .\": %d\n", instr.operand);
            break;
        }
        case OP_CR: printf("\n"); break;
        case OP_DOT_S: {
            printf("Stack: ");
            for (int i = 0; i <= stack->top; i++) printf("%d ", stack->data[i]);
            printf("\n");
            break;
        }
        case OP_CASE: break;
        case OP_OF: {
            int comparison_value = pop(stack);
            int test_value = pop(stack);
            if (comparison_value != test_value) {
                push(stack, test_value);
                *ip = instr.operand - 1;
            }
            break;
        }
        case OP_ENDOF: { *ip = instr.operand - 1; break; }
        case OP_ENDCASE: pop(stack); break;
    }
}

void addCompiledWord(char *name, Instruction *code, int code_length, char **strings, int string_count) {
    if (dict_count < DICT_SIZE) {
        dictionary[dict_count].name = strdup(name);
        memcpy(dictionary[dict_count].code, code, code_length * sizeof(Instruction));
        dictionary[dict_count].code_length = code_length;
        dictionary[dict_count].string_count = string_count;
        for (int i = 0; i < string_count; i++) {
            dictionary[dict_count].strings[i] = strings[i] ? strdup(strings[i]) : NULL;
        }
        dict_count++;
    } else printf("Dictionary full!\n");
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
    else if (strcmp(token, "LOAD") == 0) {
        char *start = *input_rest;
        while (*start && (*start == ' ' || *start == '\t')) start++;
        if (*start != '"') {
            printf("LOAD expects a quoted filename\n");
            return;
        }
        start++;
        char *end = strchr(start, '"');
        if (!end) {
            printf("Missing closing quote for LOAD\n");
            return;
        }
        int len = end - start;
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
            printf("Missing closing quote for .\"\n");
            return;
        }
        int len = end - start;
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
        } else printf("ENDOF without OF!\n");
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
        } else printf("ENDCASE without CASE!\n");
        return;
    }
    else {
        char *endptr;
        int value = strtol(token, &endptr, 10);
        if (*endptr == '\0') {
            instr.opcode = OP_PUSH;
            instr.operand = value;
        } else {
            int index = findCompiledWordIndex(token);
            if (index >= 0) {
                instr.opcode = OP_CALL;
                instr.operand = index;
            } else {
                printf("Unknown word: %s\n", token);
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
                addCompiledWord(currentWord.name, currentWord.code, currentWord.code_length, 
                              currentWord.strings, currentWord.string_count);
                free(currentWord.name);
                compiling = 0;
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
            if (strcmp(token, "LOAD") == 0) {
                char *start = saveptr;
                while (*start && (*start == ' ' || *start == '\t')) start++;
                if (*start != '"') {
                    printf("LOAD expects a quoted filename\n");
                    return;
                }
                start++;
                char *end = strchr(start, '"');
                if (!end) {
                    printf("Missing closing quote for LOAD\n");
                    return;
                }
                int len = end - start;
                char filename[MAX_STRING_SIZE];
                strncpy(filename, start, len);
                filename[len] = '\0';
                FILE *file = fopen(filename, "r");
                if (!file) {
                    printf("Cannot open file: %s\n", filename);
                } else {
                    char line[MAX_STRING_SIZE];
                    while (fgets(line, sizeof(line), file)) {
                        line[strcspn(line, "\n")] = 0;
                        interpret(line, stack);
                    }
                    fclose(file);
                }
                saveptr = end + 1;
            }
            else if (strcmp(token, ":") == 0) {
                token = strtok_r(NULL, " \t\n", &saveptr);
                if (token) {
                    compiling = 1;
                    currentWord.name = strdup(token);
                    currentWord.code_length = 0;
                    currentWord.string_count = 0;
                }
            }
            else if (strcmp(token, ".\"") == 0) {
                char *start = saveptr;
                char *end = strchr(start, '"');
                if (!end) {
                    printf("Missing closing quote for .\"\n");
                    return;
                }
                int len = end - start;
                char *str = malloc(len + 1);
                strncpy(str, start, len);
                str[len] = '\0';
                CompiledWord temp = {.code_length = 1, .string_count = 1};
                temp.code[0] = (Instruction){OP_DOT_QUOTE, 0};
                temp.strings[0] = str;
                executeCompiledWord(&temp, stack);
                free(str);
                saveptr = end + 1;
            }
            else {
                CompiledWord temp = {.code_length = 1};
                char *endptr;
                int value = strtol(token, &endptr, 10);
                if (*endptr == '\0') temp.code[0] = (Instruction){OP_PUSH, value};
                else if (strcmp(token, "+") == 0) temp.code[0] = (Instruction){OP_ADD, 0};
                else if (strcmp(token, "-") == 0) temp.code[0] = (Instruction){OP_SUB, 0};
                else if (strcmp(token, "*") == 0) temp.code[0] = (Instruction){OP_MUL, 0};
                else if (strcmp(token, "/") == 0) temp.code[0] = (Instruction){OP_DIV, 0};
                else if (strcmp(token, "DUP") == 0) temp.code[0] = (Instruction){OP_DUP, 0};
                else if (strcmp(token, "SWAP") == 0) temp.code[0] = (Instruction){OP_SWAP, 0};
                else if (strcmp(token, "OVER") == 0) temp.code[0] = (Instruction){OP_OVER, 0};
                else if (strcmp(token, "ROT") == 0) temp.code[0] = (Instruction){OP_ROT, 0};
                else if (strcmp(token, "DROP") == 0) temp.code[0] = (Instruction){OP_DROP, 0};
                else if (strcmp(token, "=") == 0) temp.code[0] = (Instruction){OP_EQ, 0};
                else if (strcmp(token, "<") == 0) temp.code[0] = (Instruction){OP_LT, 0};
                else if (strcmp(token, ">") == 0) temp.code[0] = (Instruction){OP_GT, 0};
                else if (strcmp(token, "AND") == 0) temp.code[0] = (Instruction){OP_AND, 0};
                else if (strcmp(token, "OR") == 0) temp.code[0] = (Instruction){OP_OR, 0};
                else if (strcmp(token, "NOT") == 0) temp.code[0] = (Instruction){OP_NOT, 0};
                else if (strcmp(token, "I") == 0) temp.code[0] = (Instruction){OP_I, 0};
                else if (strcmp(token, "CR") == 0) temp.code[0] = (Instruction){OP_CR, 0};
                else if (strcmp(token, ".S") == 0) temp.code[0] = (Instruction){OP_DOT_S, 0};
                else {
                    int index = findCompiledWordIndex(token);
                    if (index >= 0) temp.code[0] = (Instruction){OP_CALL, index};
                    else {
                        printf("Unknown word: %s\n", token);
                        token = strtok_r(NULL, " \t\n", &saveptr);
                        continue;
                    }
                }
                executeCompiledWord(&temp, stack);
            }
        }
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
}

void printStack(Stack *stack) {
    printf("Stack: ");
    for (int i = 0; i <= stack->top; i++) printf("%d ", stack->data[i]);
    printf("\n");
}

int main() {
    Stack stack = {.top = -1};
    char input[256];
    int suppress_stack_print = 0;
    printf("Forth-like interpreter\n");
    while (1) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        suppress_stack_print = 0;
        interpret(input, &stack);
        if (strncmp(input, "LOAD ", 5) == 0) {
            suppress_stack_print = 1;
        }
        // if (!compiling && !suppress_stack_print) printStack(&stack);
    }
    return 0;
}
