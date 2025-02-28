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
#define VAR_SIZE 100
#define MAX_STRING_SIZE 256
#define MPZ_POOL_SIZE 3

typedef enum {
    OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_DUP, OP_SWAP, OP_OVER,
    OP_ROT, OP_DROP, OP_EQ, OP_LT, OP_GT, OP_AND, OP_OR, OP_NOT, OP_I,
    OP_DO, OP_LOOP, OP_BRANCH_FALSE, OP_BRANCH, OP_CALL, OP_END, OP_DOT_QUOTE,
    OP_CR, OP_DOT_S, OP_FLUSH, OP_DOT, OP_CASE, OP_OF, OP_ENDOF, OP_ENDCASE,
    OP_EXIT, OP_BEGIN, OP_WHILE, OP_REPEAT,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT, OP_LSHIFT, OP_RSHIFT,
    OP_WORDS, OP_FORGET, OP_VARIABLE, OP_FETCH, OP_STORE,
    OP_PICK, OP_ROLL, OP_PLUSSTORE, OP_DEPTH, OP_TOP,OP_NIP, OP_MOD ,OP_SEE,OP_ALLOT,OP_CREATE // Inclut MOD
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

typedef enum { CT_IF, CT_DO, CT_CASE, CT_OF, CT_ENDOF } ControlType;

typedef struct {
    ControlType type;
    long int addr;
} ControlEntry;

typedef struct {
    char *name;
    mpz_t value;
} Variable;

ControlEntry control_stack[CONTROL_STACK_SIZE];
int control_stack_top = 0;

typedef struct {
    mpz_t index;
    mpz_t limit;
    long int addr;
} LoopControl;

typedef enum {
    MEMORY_VARIABLE,
    MEMORY_ARRAY
} MemoryType;

typedef struct {
    char *name;
    MemoryType type;  // VARIABLE ou ARRAY
    mpz_t *values;    // Pointeur vers un tableau de mpz_t
    long int size;    // Taille (1 pour VARIABLE, n pour ARRAY)
} Memory;

Memory memory[VAR_SIZE];

long int memory_count = 0;

LoopControl loop_stack[LOOP_STACK_SIZE];
long int loop_stack_top = -1;

CompiledWord dictionary[DICT_SIZE];
long int dict_count = 0;

Variable variables[VAR_SIZE];
long int var_count = 0;

CompiledWord currentWord;
int compiling = 0;
long int current_word_index = -1;

int error_flag = 0;
mpz_t mpz_pool[MPZ_POOL_SIZE];

void initStack(Stack *stack);
void clearStack(Stack *stack);
void push(Stack *stack, mpz_t value);
void pop(Stack *stack, mpz_t result);
int findCompiledWordIndex(char *name);
int findVariableIndex(char *name);
void set_error(const char *msg);
void init_mpz_pool();
void clear_mpz_pool();
void exec_arith(Instruction instr, Stack *stack);
void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word);
void executeCompiledWord(CompiledWord *word, Stack *stack);
void addCompiledWord(char *name, Instruction *code, long int code_length, char **strings, long int string_count);
void compileToken(char *token, char **input_rest);
void interpret(char *input, Stack *stack);

void initStack(Stack *stack) {
    stack->top = -1;
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_init(stack->data[i]);
    }
    for (int i = 0; i < VAR_SIZE; i++) {
        memory[i].name = NULL;
        memory[i].type = MEMORY_VARIABLE; // Par défaut, mais non utilisé si pas alloué
        memory[i].values = NULL;
        memory[i].size = 0;
    }
}

void clearStack(Stack *stack) {
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(stack->data[i]);
    }
    for (int i = 0; i < memory_count; i++) {
        if (memory[i].name) free(memory[i].name);
        if (memory[i].values) {
            for (int j = 0; j < memory[i].size; j++) {
                mpz_clear(memory[i].values[j]);
            }
            free(memory[i].values);
        }
    }
    memory_count = 0;
    for (int i = 0; i < dict_count; i++) {
        if (dictionary[i].name) free(dictionary[i].name);
        for (int j = 0; j < dictionary[i].string_count; j++) {
            if (dictionary[i].strings[j]) free(dictionary[i].strings[j]);
        }
    }
    dict_count = 0;
}

int findMemoryIndex(char *name) {
    for (int i = 0; i < memory_count; i++) {
        if (memory[i].name && strcmp(memory[i].name, name) == 0) return i;
    }
    return -1;
}
void push(Stack *stack, mpz_t value) {
    if (stack->top < STACK_SIZE - 1) {
        mpz_set(stack->data[++stack->top], value);
    } else {
        set_error("Stack overflow");
    }
}

void pop(Stack *stack, mpz_t result) {
    if (stack->top >= 0) {
        mpz_set(result, stack->data[stack->top--]);
    } else {
        set_error("Stack underflow");
        mpz_set_ui(result, 0);
    }
}

int findVariableIndex(char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0) return i;
    }
    return -1;
}

void set_error(const char *msg) {
    printf("Error: %s\n", msg);
    error_flag = 1;
}

void init_mpz_pool() {
    for (int i = 0; i < MPZ_POOL_SIZE; i++) {
        mpz_init(mpz_pool[i]);
    }
}

void clear_mpz_pool() {
    for (int i = 0; i < MPZ_POOL_SIZE; i++) {
        mpz_clear(mpz_pool[i]);
    }
}

void print_word_definition(int index, Stack *stack) {
    if (index < 0 || index >= dict_count) {
        printf("SEE: Unknown word\n");
        return;
    }
    CompiledWord *word = &dictionary[index];
    char def_msg[512] = "";
    snprintf(def_msg, sizeof(def_msg), ": %s ", word->name);
    for (int i = 0; i < word->code_length; i++) {
        Instruction instr = word->code[i];
        char instr_str[64];
        switch (instr.opcode) {
            case OP_PUSH:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "%s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "%ld ", instr.operand);
                }
                break;
            case OP_ADD: snprintf(instr_str, sizeof(instr_str), "+ "); break;
            case OP_SUB: snprintf(instr_str, sizeof(instr_str), "- "); break;
            case OP_MUL: snprintf(instr_str, sizeof(instr_str), "* "); break;
            case OP_DIV: snprintf(instr_str, sizeof(instr_str), "/ "); break;
            case OP_MOD: snprintf(instr_str, sizeof(instr_str), "MOD "); break;
            case OP_DUP: snprintf(instr_str, sizeof(instr_str), "DUP "); break;
            case OP_SWAP: snprintf(instr_str, sizeof(instr_str), "SWAP "); break;
            case OP_OVER: snprintf(instr_str, sizeof(instr_str), "OVER "); break;
            case OP_ROT: snprintf(instr_str, sizeof(instr_str), "ROT "); break;
            case OP_DROP: snprintf(instr_str, sizeof(instr_str), "DROP "); break;
            case OP_NIP: snprintf(instr_str, sizeof(instr_str), "NIP "); break;
            case OP_DOT: snprintf(instr_str, sizeof(instr_str), ". "); break;
            case OP_DOT_S: snprintf(instr_str, sizeof(instr_str), ".S "); break;
            case OP_DOT_QUOTE:
                if (instr.operand < word->string_count) {
                    snprintf(instr_str, sizeof(instr_str), ".\" %s\" ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), ".\" ???\" ");
                }
                break;
            case OP_CR: snprintf(instr_str, sizeof(instr_str), "CR "); break;
            case OP_FLUSH: snprintf(instr_str, sizeof(instr_str), "FLUSH "); break;
            case OP_EQ: snprintf(instr_str, sizeof(instr_str), "= "); break;
            case OP_LT: snprintf(instr_str, sizeof(instr_str), "< "); break;
            case OP_GT: snprintf(instr_str, sizeof(instr_str), "> "); break;
            case OP_AND: snprintf(instr_str, sizeof(instr_str), "AND "); break;
            case OP_OR: snprintf(instr_str, sizeof(instr_str), "OR "); break;
            case OP_NOT: snprintf(instr_str, sizeof(instr_str), "NOT "); break;
            case OP_I: snprintf(instr_str, sizeof(instr_str), "I "); break;
            case OP_DO: snprintf(instr_str, sizeof(instr_str), "DO "); break;
            case OP_LOOP: snprintf(instr_str, sizeof(instr_str), "LOOP "); break;
            case OP_BRANCH_FALSE: snprintf(instr_str, sizeof(instr_str), "IF "); break;
            case OP_BRANCH: snprintf(instr_str, sizeof(instr_str), "ELSE "); break;
            case OP_END: snprintf(instr_str, sizeof(instr_str), "; "); break;
            case OP_CALL:
                if (instr.operand < dict_count) {
                    snprintf(instr_str, sizeof(instr_str), "%s ", dictionary[instr.operand].name);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "(CALL %ld) ", instr.operand);
                }
                break;
            case OP_VARIABLE: snprintf(instr_str, sizeof(instr_str), "VARIABLE "); break;
            case OP_CREATE: snprintf(instr_str, sizeof(instr_str), "CREATE "); break;
            case OP_ALLOT: snprintf(instr_str, sizeof(instr_str), "ALLOT "); break;
            case OP_FETCH: snprintf(instr_str, sizeof(instr_str), "@ "); break;
            case OP_STORE: snprintf(instr_str, sizeof(instr_str), "! "); break;
            case OP_PICK: snprintf(instr_str, sizeof(instr_str), "PICK "); break;
            case OP_ROLL: snprintf(instr_str, sizeof(instr_str), "ROLL "); break;
            case OP_PLUSSTORE: snprintf(instr_str, sizeof(instr_str), "+! "); break;
            case OP_DEPTH: snprintf(instr_str, sizeof(instr_str), "DEPTH "); break;
            case OP_TOP: snprintf(instr_str, sizeof(instr_str), "TOP "); break;
            case OP_SEE: snprintf(instr_str, sizeof(instr_str), "SEE "); break;
            default: snprintf(instr_str, sizeof(instr_str), "(OP_%d) ", instr.opcode); break;
        }
        strncat(def_msg, instr_str, sizeof(def_msg) - strlen(def_msg) - 1);
    }
    printf("%s\n", def_msg);
}
void exec_arith(Instruction instr, Stack *stack) {
    mpz_t *a = &mpz_pool[0], *b = &mpz_pool[1], *result = &mpz_pool[2];
    switch (instr.opcode) {
        case OP_ADD:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_add(*result, *b, *a);
                push(stack, *result);
            }
            break;
        case OP_SUB:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_sub(*result, *b, *a);
                push(stack, *result);
            }
            break;
        case OP_MUL:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_mul(*result, *b, *a);
                push(stack, *result);
            }
            break;
        case OP_DIV:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                if (mpz_cmp_si(*a, 0) != 0) {
                    mpz_div(*result, *b, *a);
                    push(stack, *result);
                } else {
                    set_error("Division by zero");
                }
            }
            break;
        case OP_MOD:  // Nouvelle primitive MOD
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                if (mpz_cmp_si(*a, 0) != 0) {
                    mpz_mod(*result, *b, *a);
                    push(stack, *result);
                } else {
                    set_error("Modulo by zero");
                }
            }
            break;
    }
}

void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word) {
    if (error_flag) return;
    mpz_t *a = &mpz_pool[0], *b = &mpz_pool[1], *result = &mpz_pool[2];

    switch (instr.opcode) {
        case OP_PUSH:
            if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
                if (mpz_set_str(*result, word->strings[instr.operand], 10) != 0) {
                    set_error("Failed to parse number");
                }
                push(stack, *result);
            } else {
                mpz_set_si(*result, instr.operand);
                push(stack, *result);
            }
            break;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
            exec_arith(instr, stack);
            break;
        case OP_DUP:
            pop(stack, *a);
            if (!error_flag) {
                push(stack, *a);
                push(stack, *a);
            }
            break;
        case OP_SWAP:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                push(stack, *a);
                push(stack, *b);
            }
            break;
        case OP_OVER:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                push(stack, *b);
                push(stack, *a);
                push(stack, *b);
            }
            break;
        case OP_ROT:
            if (stack->top >= 2) {
                mpz_set(*a, stack->data[stack->top - 2]);
                mpz_set(*b, stack->data[stack->top - 1]);
                mpz_set(*result, stack->data[stack->top]);
                mpz_set(stack->data[stack->top - 2], *b);
                mpz_set(stack->data[stack->top - 1], *result);
                mpz_set(stack->data[stack->top], *a);
            } else {
                set_error("Stack underflow for ROT");
            }
            break;
        case OP_DROP:
            pop(stack, *a);
            break;
        case OP_NIP:  // Ajout de NIP
            pop(stack, *a);  // Pop le sommet (y)
            pop(stack, *b);  // Pop l'élément en dessous (x)
            if (!error_flag) {
                push(stack, *a);  // Repousse uniquement le sommet (y)
            }
            break;
        case OP_DOT:
            pop(stack, *a);
            if (!error_flag) gmp_printf("%Zd\n", *a);
            break;
        case OP_FLUSH:
            stack->top = -1;
            break;
        case OP_EQ:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_set_si(*result, mpz_cmp(*b, *a) == 0 ? 1 : 0);
                push(stack, *result);
            }
            break;
        case OP_LT:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_set_si(*result, mpz_cmp(*b, *a) < 0 ? 1 : 0);
                push(stack, *result);
            }
            break;
        case OP_GT:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_set_si(*result, mpz_cmp(*b, *a) > 0 ? 1 : 0);
                push(stack, *result);
            }
            break;
        case OP_AND:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_set_si(*result, (mpz_cmp_si(*b, 0) != 0 && mpz_cmp_si(*a, 0) != 0) ? 1 : 0);
                push(stack, *result);
            }
            break;
        case OP_OR:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_set_si(*result, (mpz_cmp_si(*b, 0) != 0 || mpz_cmp_si(*a, 0) != 0) ? 1 : 0);
                push(stack, *result);
            }
            break;
        case OP_NOT:
            pop(stack, *a);
            if (!error_flag) {
                mpz_set_si(*result, mpz_cmp_si(*a, 0) == 0 ? 1 : 0);
                push(stack, *result);
            }
            break;
        case OP_I:
            if (loop_stack_top >= 0) push(stack, loop_stack[loop_stack_top].index);
            else set_error("I used outside of a loop");
            break;
        case OP_DO:
            pop(stack, *b); pop(stack, *a);
            if (!error_flag && loop_stack_top < LOOP_STACK_SIZE - 1) {
                loop_stack_top++;
                mpz_init_set(loop_stack[loop_stack_top].index, *b);
                mpz_init_set(loop_stack[loop_stack_top].limit, *a);
                loop_stack[loop_stack_top].addr = *ip + 1;
            } else if (!error_flag) {
                set_error("Loop stack overflow");
            }
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
            } else {
                set_error("LOOP without DO");
            }
            break;
        case OP_BRANCH_FALSE:
            pop(stack, *a);
            if (!error_flag && mpz_cmp_si(*a, 0) == 0) *ip = instr.operand - 1;
            break;
        case OP_BRANCH:
            *ip = instr.operand - 1;
            break;
        case OP_CALL:
            if (instr.operand >= 0 && instr.operand < dict_count) {
                executeCompiledWord(&dictionary[instr.operand], stack);
            } else if (instr.operand >= 0 && instr.operand < word->string_count) {
                FILE *file = fopen(word->strings[instr.operand], "r");
                if (!file) {
                    set_error("Cannot open file");
                    break;
                }
                char line[MAX_STRING_SIZE];
                while (fgets(line, sizeof(line), file)) {
                    line[strcspn(line, "\n")] = 0;
                    interpret(line, stack);
                }
                fclose(file);
            } else {
                set_error("Invalid CALL index");
            }
            break;
        case OP_END:
            break;
        case OP_DOT_QUOTE:
            if (instr.operand >= 0 && instr.operand < word->string_count) {
                printf("%s", word->strings[instr.operand]);
            } else set_error("Invalid string index for .\"");
            break;
        case OP_CR:
            printf("\n");
            break;
        case OP_DOT_S:
            printf("Stack: ");
            for (int i = 0; i <= stack->top; i++) gmp_printf("%Zd ", stack->data[i]);
            printf("\n");
            break;
        case OP_CASE:
            break;
        case OP_OF:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag && mpz_cmp(*a, *b) != 0) {
                push(stack, *b);
                *ip = instr.operand - 1;
            }
            break;
        case OP_ENDOF:
            *ip = instr.operand - 1;
            break;
        case OP_ENDCASE:
            pop(stack, *a);
            break;
        case OP_EXIT:
            *ip = word->code_length - 1;
            break;
        case OP_BEGIN:
            break;
        case OP_WHILE:
            pop(stack, *a);
            if (!error_flag && mpz_cmp_si(*a, 0) == 0) {
                *ip = instr.operand - 1;
            }
            break;
        case OP_REPEAT:
            *ip = instr.operand - 1;
            break;
        case OP_BIT_AND:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_and(*result, *b, *a);
                push(stack, *result);
            }
            break;
        case OP_BIT_OR:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_ior(*result, *b, *a);
                push(stack, *result);
            }
            break;
        case OP_BIT_XOR:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_xor(*result, *b, *a);
                push(stack, *result);
            }
            break;
        case OP_BIT_NOT:
            pop(stack, *a);
            if (!error_flag) {
                mpz_com(*result, *a);
                push(stack, *result);
            }
            break;
        case OP_LSHIFT:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_mul_2exp(*result, *b, mpz_get_ui(*a));
                push(stack, *result);
            }
            break;
        case OP_RSHIFT:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                mpz_tdiv_q_2exp(*result, *b, mpz_get_ui(*a));
                push(stack, *result);
            }
            break;
        case OP_WORDS:
            for (int i = 0; i < dict_count; i++) {
                printf("%s ", dictionary[i].name);
            }
            printf("\n");
            break;
case OP_FORGET:
    if (instr.operand >= 0 && instr.operand < dict_count) {
        for (int i = instr.operand; i < dict_count; i++) {
            if (dictionary[i].name) {
                free(dictionary[i].name);
                dictionary[i].name = NULL;  // Évite un double free
            }
            for (int j = 0; j < dictionary[i].string_count; j++) {
                if (dictionary[i].strings[j]) {
                    free(dictionary[i].strings[j]);
                    dictionary[i].strings[j] = NULL;  // Évite un double free
                }
            }
        }
        dict_count = instr.operand;
    } else {
        set_error("FORGET: Word index out of range");
    }
    break;
case OP_VARIABLE:
    if (memory_count < VAR_SIZE) {
        memory[memory_count].name = strdup(word->strings[instr.operand]);
        memory[memory_count].type = MEMORY_VARIABLE;
        memory[memory_count].size = 1;
        memory[memory_count].values = malloc(1 * sizeof(mpz_t));
        if (!memory[memory_count].values) {
            set_error("VARIABLE: Memory allocation failed");
            break;
        }
        mpz_init(memory[memory_count].values[0]);
        mpz_set_ui(memory[memory_count].values[0], 0);
        Instruction var_code[1] = {{OP_PUSH, memory_count}};
        char *var_strings[1] = {NULL};
        addCompiledWord(memory[memory_count].name, var_code, 1, var_strings, 0);
        mpz_set_si(*result, memory_count);
        push(stack, *result);
        memory_count++;
    } else {
        set_error("Memory table full");
    }
    break;

case OP_CREATE:
    if (memory_count < VAR_SIZE) {
        memory[memory_count].name = strdup(word->strings[instr.operand]);
        memory[memory_count].type = MEMORY_ARRAY;
        memory[memory_count].size = 0; // Taille définie par ALLOT
        memory[memory_count].values = NULL;
        Instruction var_code[1] = {{OP_PUSH, memory_count}};
        char *var_strings[1] = {NULL};
        addCompiledWord(memory[memory_count].name, var_code, 1, var_strings, 0);
        mpz_set_si(*result, memory_count);
        push(stack, *result);
        memory_count++;
    } else {
        set_error("Memory table full");
    }
    break;

case OP_ALLOT:
    pop(stack, *a); // Size
    pop(stack, *b); // Memory index
    if (!error_flag && mpz_fits_slong_p(*a) && mpz_fits_slong_p(*b)) {
        long int size = mpz_get_si(*a);
        int index = mpz_get_si(*b);
        if (index >= 0 && index < memory_count && memory[index].type == MEMORY_ARRAY) {
            if (memory[index].values) {
                for (int i = 0; i < memory[index].size; i++) {
                    mpz_clear(memory[index].values[i]);
                }
                free(memory[index].values);
            }
            memory[index].values = malloc(size * sizeof(mpz_t));
            if (!memory[index].values) {
                set_error("ALLOT: Memory allocation failed");
                break;
            }
            memory[index].size = size;
            for (int i = 0; i < size; i++) {
                mpz_init(memory[index].values[i]);
                mpz_set_ui(memory[index].values[i], 0);
            }
        } else if (!error_flag) {
            set_error("ALLOT: Invalid memory index or not an array");
        }
    } else if (!error_flag) {
        set_error("ALLOT: Invalid size or memory index");
    }
    break;

case OP_FETCH: // @ : [index array -- value] ou [array -- value]
    pop(stack, *b); // Memory index
    if (!error_flag && mpz_fits_slong_p(*b) && mpz_get_si(*b) >= 0 && mpz_get_si(*b) < memory_count) {
        int idx = mpz_get_si(*b);
        if (memory[idx].type == MEMORY_VARIABLE) {
            push(stack, memory[idx].values[0]); // Variable : indice implicite 0
        } else if (memory[idx].type == MEMORY_ARRAY) {
            pop(stack, *a); // Index explicite pour tableau
            if (!error_flag && mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) < memory[idx].size) {
                push(stack, memory[idx].values[mpz_get_si(*a)]);
            } else if (!error_flag) {
                set_error("FETCH: Index out of bounds for array");
            }
        }
    } else if (!error_flag) {
        set_error("FETCH: Invalid memory index");
    }
    break;

case OP_STORE: // ! : [value index array -- ] ou [value array -- ]
    pop(stack, *result); // Memory index
    if (!error_flag && mpz_fits_slong_p(*result) && mpz_get_si(*result) >= 0 && mpz_get_si(*result) < memory_count) {
        int idx = mpz_get_si(*result);
        if (memory[idx].type == MEMORY_VARIABLE) {
            pop(stack, *a); // Valeur
            if (!error_flag) {
                mpz_set(memory[idx].values[0], *a); // Variable : indice implicite 0
            }
        } else if (memory[idx].type == MEMORY_ARRAY) {
            pop(stack, *b); // Index explicite pour tableau
            pop(stack, *a); // Valeur
            if (!error_flag && mpz_fits_slong_p(*b) && mpz_get_si(*b) >= 0 && mpz_get_si(*b) < memory[idx].size) {
                mpz_set(memory[idx].values[mpz_get_si(*b)], *a);
            } else if (!error_flag) {
                set_error("STORE: Index out of bounds for array");
            }
        }
    } else if (!error_flag) {
        set_error("STORE: Invalid memory index");
    }
    break;
        case OP_PICK:
            pop(stack, *a);
            if (!error_flag) {
                long int n = mpz_get_si(*a);
                if (n >= 0 && n <= stack->top) {
                    mpz_set(*result, stack->data[stack->top - n]);
                    push(stack, *result);
                } else {
                    set_error("PICK: Stack underflow or invalid index");
                    push(stack, *a);
                }
            }
            break;
        case OP_ROLL:
            pop(stack, *a);
            if (!error_flag) {
                long int n = mpz_get_si(*a);
                if (n < 0 || n > stack->top + 1) {
                    set_error("ROLL: Invalid index or stack underflow");
                    push(stack, *a);
                } else if (n > 0) {
                    int index = stack->top + 1 - n;
                    mpz_set(*result, stack->data[index]);
                    for (int i = index; i < stack->top; i++) {
                        mpz_set(stack->data[i], stack->data[i + 1]);
                    }
                    mpz_set(stack->data[stack->top], *result);
                }
            }
            break;
        case OP_PLUSSTORE:
            pop(stack, *a);
            pop(stack, *b);
            if (!error_flag && mpz_fits_slong_p(*b) && mpz_get_si(*b) >= 0 && mpz_get_si(*b) < var_count) {
                mpz_add(variables[mpz_get_si(*b)].value, variables[mpz_get_si(*b)].value, *a);
            } else if (!error_flag) {
                set_error("PLUSSTORE: Invalid variable index");
            }
            break;
        case OP_DEPTH:
            mpz_set_si(*result, stack->top + 1);
            push(stack, *result);
            break;
        case OP_TOP:
            if (stack->top >= 0) {
                gmp_printf("%Zd\n", stack->data[stack->top]);
            } else {
                set_error("TOP: Stack underflow");
            }
            break;
case OP_SEE:
            pop(stack, *a);
            if (stack->top >= -1 && mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) < dict_count) {
                print_word_definition(mpz_get_si(*a), stack);
            } else {
                printf("SEE: Invalid word index\n");
            }
            break;
    }
}

void executeCompiledWord(CompiledWord *word, Stack *stack) {
    long int ip = 0;
    while (ip < word->code_length && !error_flag) {
        executeInstruction(word->code[ip], stack, &ip, word);
        ip++;
    }
}

void addCompiledWord(char *name, Instruction *code, long int code_length, char **strings, long int string_count) {
    int existing_index = findCompiledWordIndex(name);
    if (existing_index >= 0) {
        CompiledWord *word = &dictionary[existing_index];
        if (word->name) free(word->name);
        for (int i = 0; i < word->string_count; i++) {
            if (word->strings[i]) free(word->strings[i]);
        }
        word->name = strdup(name);
        memcpy(word->code, code, code_length * sizeof(Instruction));
        word->code_length = code_length;
        word->string_count = string_count;
        for (int i = 0; i < string_count; i++) {
            word->strings[i] = strings[i] ? strdup(strings[i]) : NULL;
        }
    } else if (dict_count < DICT_SIZE) {
        dictionary[dict_count].name = strdup(name);
        memcpy(dictionary[dict_count].code, code, code_length * sizeof(Instruction));
        dictionary[dict_count].code_length = code_length;
        dictionary[dict_count].string_count = string_count;
        for (int i = 0; i < string_count; i++) {
            dictionary[dict_count].strings[i] = strings[i] ? strdup(strings[i]) : NULL;
        }
        dict_count++;
    } else {
        set_error("Dictionary full");
    }
}
int findCompiledWordIndex(char *name) {
    for (int i = 0; i < dict_count; i++) {
        if (strcmp(dictionary[i].name, name) == 0) return i;
    }
    return -1;
}

void compileToken(char *token, char **input_rest) {
    Instruction instr = {0};
    if (strcmp(token, "+") == 0) {
        instr.opcode = OP_ADD;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "-") == 0) {
        instr.opcode = OP_SUB;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "*") == 0) {
        instr.opcode = OP_MUL;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "/") == 0) {
        instr.opcode = OP_DIV;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "MOD") == 0) {  // Ajout de MOD
        instr.opcode = OP_MOD;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "DUP") == 0) {
        instr.opcode = OP_DUP;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "SWAP") == 0) {
        instr.opcode = OP_SWAP;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "OVER") == 0) {
        instr.opcode = OP_OVER;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "ROT") == 0) {
        instr.opcode = OP_ROT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "DROP") == 0) {
        instr.opcode = OP_DROP;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "NIP") == 0) {  // Ajout de NIP
        instr.opcode = OP_NIP;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "=") == 0) {
        instr.opcode = OP_EQ;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "<") == 0) {
        instr.opcode = OP_LT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, ">") == 0) {
        instr.opcode = OP_GT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "AND") == 0) {
        instr.opcode = OP_AND;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "OR") == 0) {
        instr.opcode = OP_OR;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "NOT") == 0) {
        instr.opcode = OP_NOT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "I") == 0) {
        instr.opcode = OP_I;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "CR") == 0) {
        instr.opcode = OP_CR;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, ".S") == 0) {
        instr.opcode = OP_DOT_S;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, ".") == 0) {
        instr.opcode = OP_DOT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "FLUSH") == 0) {
        instr.opcode = OP_FLUSH;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "IF") == 0) {
        instr.opcode = OP_BRANCH_FALSE;
        instr.operand = 0;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_IF, currentWord.code_length - 1};
    } else if (strcmp(token, "ELSE") == 0) {
        instr.opcode = OP_BRANCH;
        instr.operand = 0;
        currentWord.code[currentWord.code_length++] = instr;
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_IF) {
            currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
            control_stack[control_stack_top++] = (ControlEntry){CT_IF, currentWord.code_length - 1};
        }
    } else if (strcmp(token, "THEN") == 0) {
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_IF) {
            currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
        }
    } else if (strcmp(token, "DO") == 0) {
        instr.opcode = OP_DO;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_DO, currentWord.code_length - 1};
    } else if (strcmp(token, "LOOP") == 0) {
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_DO) {
            instr.opcode = OP_LOOP;
            currentWord.code[currentWord.code_length++] = instr;
            control_stack_top--;
        }
    } else if (strcmp(token, "EXIT") == 0) {
        instr.opcode = OP_EXIT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "LOAD") == 0) {
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
    } else if (strcmp(token, ".\"") == 0) {
        char *start = *input_rest;
        char *end = strchr(start, '"');
        if (!end) {
            printf("Missing closing quote for .\"\n");
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
    } else if (strcmp(token, "CASE") == 0) {
        instr.opcode = OP_CASE;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_CASE, currentWord.code_length - 1};
    } else if (strcmp(token, "OF") == 0) {
        instr.opcode = OP_OF;
        instr.operand = 0;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_OF, currentWord.code_length - 1};
    } else if (strcmp(token, "ENDOF") == 0) {
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_OF) {
            instr.opcode = OP_ENDOF;
            instr.operand = 0;
            currentWord.code[currentWord.code_length++] = instr;
            currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
            control_stack[control_stack_top++] = (ControlEntry){CT_ENDOF, currentWord.code_length - 1};
        } else printf("ENDOF without OF!\n");
    } else if (strcmp(token, "ENDCASE") == 0) {
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_ENDOF) {
            instr.opcode = OP_ENDCASE;
            currentWord.code[currentWord.code_length++] = instr;
            while (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_ENDOF) {
                currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
            }
            if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_CASE) {
                control_stack_top--;
            }
        } else printf("ENDCASE without CASE!\n");
    } else if (strcmp(token, "BEGIN") == 0) {
        instr.opcode = OP_BEGIN;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_DO, currentWord.code_length - 1};
    } else if (strcmp(token, "WHILE") == 0) {
        instr.opcode = OP_WHILE;
        instr.operand = 0;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_IF, currentWord.code_length - 1};
    } else if (strcmp(token, "REPEAT") == 0) {
        instr.opcode = OP_REPEAT;
        instr.operand = control_stack[control_stack_top - 2].addr;
        currentWord.code[currentWord.code_length++] = instr;
        currentWord.code[control_stack[control_stack_top - 1].addr].operand = currentWord.code_length;
        control_stack_top -= 2;
    } else if (strcmp(token, "&") == 0) {
        instr.opcode = OP_BIT_AND;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "|") == 0) {
        instr.opcode = OP_BIT_OR;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "^") == 0) {
        instr.opcode = OP_BIT_XOR;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "~") == 0) {
        instr.opcode = OP_BIT_NOT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "LSHIFT") == 0) {
        instr.opcode = OP_LSHIFT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "RSHIFT") == 0) {
        instr.opcode = OP_RSHIFT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "WORDS") == 0) {
        instr.opcode = OP_WORDS;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "FORGET") == 0) {
        char *next_token = strtok_r(NULL, " \t\n", input_rest);
        if (!next_token) {
            printf("FORGET requires a word name\n");
            return;
        }
        int index = findCompiledWordIndex(next_token);
        if (index >= 0) {
            instr.opcode = OP_FORGET;
            instr.operand = index;
            currentWord.code[currentWord.code_length++] = instr;
        } else {
            printf("FORGET: Unknown word: %s\n", next_token);
        }
    } else if (strcmp(token, "VARIABLE") == 0) {
        char *next_token = strtok_r(NULL, " \t\n", input_rest);
        if (!next_token) {
            printf("VARIABLE requires a name\n");
            return;
        }
        instr.opcode = OP_VARIABLE;
        instr.operand = currentWord.string_count;
        currentWord.strings[currentWord.string_count++] = strdup(next_token);
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "@") == 0) {
        instr.opcode = OP_FETCH;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "!") == 0) {
        instr.opcode = OP_STORE;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "PICK") == 0) {
        instr.opcode = OP_PICK;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "ROLL") == 0) {
        instr.opcode = OP_ROLL;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "+!") == 0) {
        instr.opcode = OP_PLUSSTORE;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "DEPTH") == 0) {
        instr.opcode = OP_DEPTH;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "TOP") == 0) {
        instr.opcode = OP_TOP;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "SEE") == 0) {
    char *next_token = strtok_r(NULL, " \t\n", input_rest);
    if (!next_token) {
        printf("SEE requires a word name\n");
        return;
    }
    int index = findCompiledWordIndex(next_token);
    if (index >= 0) {
        instr.opcode = OP_PUSH;
        instr.operand = currentWord.string_count;
        currentWord.strings[currentWord.string_count++] = strdup(next_token);
        currentWord.code[currentWord.code_length++] = instr;
        instr.opcode = OP_SEE;
        instr.operand = 0;
        currentWord.code[currentWord.code_length++] = instr;
    } else {
        printf("SEE: Unknown word: %s\n", next_token);
    }
    } else {
        long int index = findCompiledWordIndex(token);
        if (index >= 0) {
            instr.opcode = OP_CALL;
            instr.operand = index;
            currentWord.code[currentWord.code_length++] = instr;
        } else {
            mpz_t test_num;
            mpz_init(test_num);
            if (mpz_set_str(test_num, token, 10) == 0) {
                instr.opcode = OP_PUSH;
                instr.operand = currentWord.string_count;
                currentWord.strings[currentWord.string_count++] = strdup(token);
                currentWord.code[currentWord.code_length++] = instr;
            } else {
                printf("Unknown word: %s\n", token);
            }
            mpz_clear(test_num);
        }
    }
}

void interpret(char *input, Stack *stack) {
    error_flag = 0;
    char *saveptr;
    char *token = strtok_r(input, " \t\n", &saveptr);
    while (token && !error_flag) {
        if (compiling) {
            if (strcmp(token, ";") == 0) {
                Instruction end = {OP_END, 0};
                currentWord.code[currentWord.code_length++] = end;
                if (current_word_index >= 0 && current_word_index < dict_count) {
                    memcpy(dictionary[current_word_index].code, currentWord.code, currentWord.code_length * sizeof(Instruction));
                    dictionary[current_word_index].code_length = currentWord.code_length;
                    for (int i = 0; i < currentWord.string_count; i++) {
                        if (dictionary[current_word_index].strings[i]) free(dictionary[current_word_index].strings[i]);
                        dictionary[current_word_index].strings[i] = currentWord.strings[i];
                        currentWord.strings[i] = NULL;
                    }
                    dictionary[current_word_index].string_count = currentWord.string_count;
                }
                free(currentWord.name);
                for (int i = 0; i < currentWord.string_count; i++) {
                    if (currentWord.strings[i]) free(currentWord.strings[i]);
                }
                compiling = 0;
                current_word_index = -1;
            } else {
                compileToken(token, &saveptr);
            }
        } else {
            CompiledWord temp = {.code_length = 0, .string_count = 0};
            mpz_t big_value;
            mpz_init(big_value);
            if (mpz_set_str(big_value, token, 10) == 0) {
                push(stack, big_value);
            } else if (strcmp(token, ":") == 0) {
                token = strtok_r(NULL, " \t\n", &saveptr);
                if (token) {
                    compiling = 1;
                    currentWord.name = strdup(token);
                    currentWord.code_length = 0;
                    currentWord.string_count = 0;
                    addCompiledWord(currentWord.name, currentWord.code, currentWord.code_length, 
                                    currentWord.strings, currentWord.string_count);
                    current_word_index = dict_count - 1;
                }
            } else if (strcmp(token, "LOAD") == 0) {
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
                long int len = end - start;
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
            } else if (strcmp(token, ".\"") == 0) {
                char *start = saveptr;
                char *end = strchr(start, '"');
                if (!end) {
                    printf("Missing closing quote for .\"\n");
                    return;
                }
                long int len = end - start;
                char *str = malloc(len + 1);
                strncpy(str, start, len);
                str[len] = '\0';
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DOT_QUOTE, 0};
                temp.strings[0] = str;
                temp.string_count = 1;
                executeCompiledWord(&temp, stack);
                free(str);
                saveptr = end + 1;
            } else if (strcmp(token, "+") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_ADD, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "-") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_SUB, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "*") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_MUL, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "/") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DIV, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "MOD") == 0) {  // Ajout de MOD
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_MOD, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "DUP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DUP, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "SWAP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_SWAP, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "OVER") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_OVER, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "ROT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_ROT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "DROP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DROP, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "NIP") == 0) {  // Ajout de NIP
        		temp.code_length = 1;
        		temp.code[0] = (Instruction){OP_NIP, 0};
        		executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "=") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_EQ, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "<") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_LT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, ">") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_GT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "AND") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_AND, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "OR") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_OR, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "NOT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_NOT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "I") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_I, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "CR") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_CR, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, ".S") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DOT_S, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, ".") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DOT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "FLUSH") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_FLUSH, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "EXIT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_EXIT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "&") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_BIT_AND, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "|") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_BIT_OR, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "^") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_BIT_XOR, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "~") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_BIT_NOT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "LSHIFT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_LSHIFT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "RSHIFT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_RSHIFT, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "WORDS") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_WORDS, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "FORGET") == 0) {
                char *next_token = strtok_r(NULL, " \t\n", &saveptr);
                if (!next_token) {
                    printf("FORGET requires a word name\n");
                    return;
                }
                int index = findCompiledWordIndex(next_token);
                if (index >= 0) {
                    temp.code_length = 1;
                    temp.code[0] = (Instruction){OP_FORGET, index};
                    executeCompiledWord(&temp, stack);
                } else {
                    printf("FORGET: Unknown word: %s\n", next_token);
                }
            } else if (strcmp(token, "VARIABLE") == 0) {
    char *next_token = strtok_r(NULL, " \t\n", &saveptr);
    if (!next_token) {
        printf("VARIABLE requires a name\n");
        mpz_clear(big_value);
        return;
    }
    temp.code_length = 1;
    temp.code[0].opcode = OP_VARIABLE;
    temp.code[0].operand = temp.string_count;
    temp.strings[temp.string_count++] = strdup(next_token);
    executeCompiledWord(&temp, stack);
} else if (strcmp(token, "CREATE") == 0) {
    char *next_token = strtok_r(NULL, " \t\n", &saveptr);
    if (!next_token) {
        printf("CREATE requires a name\n");
        mpz_clear(big_value);
        return;
    }
    temp.code_length = 1;
    temp.code[0].opcode = OP_CREATE;
    temp.code[0].operand = temp.string_count;
    temp.strings[temp.string_count++] = strdup(next_token);
    executeCompiledWord(&temp, stack);
} else if (strcmp(token, "ALLOT") == 0) {
    temp.code_length = 1;
    temp.code[0] = (Instruction){OP_ALLOT, 0};
    executeCompiledWord(&temp, stack);
} else if (strcmp(token, "!") == 0) {
    temp.code_length = 1;
    temp.code[0] = (Instruction){OP_STORE, 0};
    executeCompiledWord(&temp, stack);
} else if (strcmp(token, "@") == 0) {
    temp.code_length = 1;
    temp.code[0] = (Instruction){OP_FETCH, 0};
    executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "PICK") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_PICK, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "ROLL") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_ROLL, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "+!") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_PLUSSTORE, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "DEPTH") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DEPTH, 0};
                executeCompiledWord(&temp, stack);
            } else if (strcmp(token, "TOP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_TOP, 0};
                executeCompiledWord(&temp, stack);
                } else if (strcmp(token, "SEE") == 0) {
    					char *next_token = strtok_r(NULL, " \t\n", &saveptr);
    			if (!next_token) {
        			printf("SEE requires a word name\n");
        			return;
   					 }
    			int index = findCompiledWordIndex(next_token);
    			if (index >= 0) {
        			mpz_set_si(big_value, index);
        			push(stack, big_value);
        			temp.code_length = 1;
        			temp.code[0] = (Instruction){OP_SEE, 0};
        			executeCompiledWord(&temp, stack);
    			} else {
        		printf("SEE: Unknown word: %s\n", next_token);
    			}
            } else {
                long int index = findCompiledWordIndex(token);
                if (index >= 0) {
                    temp.code_length = 1;
                    temp.code[0] = (Instruction){OP_CALL, index};
                    executeCompiledWord(&temp, stack);
                } else {
                    printf("Unknown word: %s\n", token);
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
    init_mpz_pool();
    char input[256];
    int suppress_stack_print = 0;
    printf("Forth-like interpreter with GMP\n");
    while (1) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        suppress_stack_print = 0;
        interpret(input, &stack);
        if (strncmp(input, "LOAD ", 5) == 0) {
            suppress_stack_print = 1;
        }
        if (!compiling && !suppress_stack_print) {
            printf("Stack: ");
            for (int i = 0; i <= stack.top; i++) gmp_printf("%Zd ", stack.data[i]);
            printf("\n");
        }
    }
    clearStack(&stack);
    clear_mpz_pool();
    return 0;
}
