#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include <time.h>

#define STACK_SIZE 1000
#define DICT_SIZE 100
#define WORD_CODE_SIZE 512
#define CONTROL_STACK_SIZE 100
#define LOOP_STACK_SIZE 500
#define VAR_SIZE 100
#define MAX_STRING_SIZE 256
#define MPZ_POOL_SIZE 3

#define BOT_NAME "ForthBot"
#define CHANNEL "##forth"

typedef enum {
    OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_DUP, OP_SWAP, OP_OVER,
    OP_ROT, OP_DROP, OP_EQ, OP_LT, OP_GT, OP_AND, OP_OR, OP_NOT, OP_I,OP_J ,
    OP_DO, OP_LOOP, OP_BRANCH_FALSE, OP_BRANCH,
    OP_UNLOOP, OP_PLOOP,OP_SQRT,OP_CALL, OP_END, OP_DOT_QUOTE,
    OP_CR, OP_DOT_S, OP_FLUSH, OP_DOT, OP_CASE, OP_OF, OP_ENDOF, OP_ENDCASE,
    OP_EXIT, OP_BEGIN, OP_WHILE, OP_REPEAT,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT, OP_LSHIFT, OP_RSHIFT,
    OP_WORDS, OP_FORGET, OP_VARIABLE, OP_FETCH, OP_STORE,
    OP_PICK, OP_ROLL, OP_PLUSSTORE, OP_DEPTH, OP_TOP, OP_NIP, OP_MOD,
    OP_CREATE, OP_ALLOT, OP_SEE, OP_RECURSE, OP_IRC_CONNECT, OP_IRC_SEND,OP_EMIT, 
    OP_STRING, OP_QUOTE, OP_PRINT ,OP_STORE_STRING,OP_AGAIN ,
    OP_TO_R, OP_FROM_R, OP_R_FETCH, OP_UNTIL, OP_CLOCK 
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

 

typedef enum {
    MEMORY_VARIABLE,
    MEMORY_ARRAY,
    MEMORY_STRING  // Nouveau type pour les chaînes
} MemoryType;

typedef struct {
    char *name;
    MemoryType type;
    mpz_t *values;      // Pour MEMORY_VARIABLE et MEMORY_ARRAY
    char *string;       // Pour MEMORY_STRING
    long int size;      // Pour MEMORY_ARRAY, ignoré pour MEMORY_STRING
} Memory;

char *string_stack[STACK_SIZE];
int string_stack_top = -1;

Memory memory[VAR_SIZE];
long int memory_count = 0;

 

CompiledWord dictionary[DICT_SIZE];
long int dict_count = 0;

Variable variables[VAR_SIZE];
long int var_count = 0;

CompiledWord currentWord;
int compiling = 0;
long int current_word_index = -1;

int error_flag = 0;
mpz_t mpz_pool[MPZ_POOL_SIZE];
static int irc_socket = -1;
char emit_buffer[512] = "";
int emit_buffer_pos = 0;

void initStack(Stack *stack);
void clearStack(Stack *stack);
void push(Stack *stack, mpz_t value);
void pop(Stack *stack, mpz_t result);
int findCompiledWordIndex(char *name);
int findVariableIndex(char *name);
int findMemoryIndex(char *name);
void set_error(const char *msg);
void init_mpz_pool();
void clear_mpz_pool();
void exec_arith(Instruction instr, Stack *stack);
void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word, int word_index);
void executeCompiledWord(CompiledWord *word, Stack *stack, int word_index);
void addCompiledWord(char *name, Instruction *code, long int code_length, char **strings, long int string_count);
void compileToken(char *token, char **input_rest, int *compile_error);
void interpret(char *input, Stack *stack);
void irc_connect(Stack *stack);
void send_to_channel(const char *msg);
void print_word_definition_irc(int index, Stack *stack);

// Déclarations globales  
Stack main_stack;    // Pile principale (data stack)
Stack return_stack;  // Pile de retour (return stack)


void initStack(Stack *stack) {
    stack->top = -1;
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_init(stack->data[i]);
    }
    for (int i = 0; i < VAR_SIZE; i++) {
        memory[i].name = NULL;
        memory[i].type = MEMORY_VARIABLE;
        memory[i].values = NULL;
        memory[i].string = NULL;  // Initialisé à NULL pour MEMORY_STRING
        memory[i].size = 0;
    }
}

void push_string(char *str) {
    if (string_stack_top < STACK_SIZE - 1) {
        string_stack[++string_stack_top] = str;
    } else {
        set_error("String stack overflow");
    }
}

char *pop_string() {
    if (string_stack_top >= 0) {
        return string_stack[string_stack_top--];
    } else {
        set_error("String stack underflow");
        return NULL;
    }
}
void clearStack(Stack *stack) {
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(stack->data[i]);
    }
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(return_stack.data[i]);
    }
    return_stack.top = -1;
    for (int i = 0; i < memory_count; i++) {
        if (memory[i].name) free(memory[i].name);
        if (memory[i].type == MEMORY_VARIABLE || memory[i].type == MEMORY_ARRAY) {
            if (memory[i].values) {
                for (int j = 0; j < memory[i].size; j++) {
                    mpz_clear(memory[i].values[j]);
                }
                free(memory[i].values);
            }
        } else if (memory[i].type == MEMORY_STRING) {
            if (memory[i].string) free(memory[i].string);
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
void set_error(const char *msg) {
    char err_msg[512];
    snprintf(err_msg, sizeof(err_msg), "Error: %s", msg);
    send_to_channel(err_msg);
    error_flag = 1;
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
        if (variables[i].name && strcmp(variables[i].name, name) == 0) return i;
    }
    return -1;
}

int findCompiledWordIndex(char *name) {
    for (int i = 0; i < dict_count; i++) {
        if (dictionary[i].name && strcmp(dictionary[i].name, name) == 0) return i;
    }
    return -1;
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
        case OP_MOD:
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

void print_word_definition_irc(int index, Stack *stack) {
    if (index < 0 || index >= dict_count) {
        send_to_channel("SEE: Unknown word");
        return;
    }
    CompiledWord *word = &dictionary[index];
    char def_msg[512] = "";
    snprintf(def_msg, sizeof(def_msg), ": %s ", word->name);

    long int branch_targets[WORD_CODE_SIZE];
    int branch_depth = 0;
    int has_semicolon = 0;

    for (int i = 0; i < word->code_length; i++) {
        Instruction instr = word->code[i];
        char instr_str[64] = "";

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
            case OP_BRANCH_FALSE:
                snprintf(instr_str, sizeof(instr_str), "IF ");
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_BRANCH:
                if (branch_depth > 0) {
                    snprintf(instr_str, sizeof(instr_str), "ELSE ");
                    branch_targets[branch_depth - 1] = instr.operand;
                }
                break;
            case OP_END:
                if (branch_depth > 0 && i + 1 == branch_targets[branch_depth - 1]) {
                    snprintf(instr_str, sizeof(instr_str), "THEN ");
                    branch_depth--;
                    if (i + 1 == word->code_length) {
                        strncat(instr_str, ";", sizeof(instr_str) - strlen(instr_str) - 1);
                        has_semicolon = 1;
                    }
                } else if (i + 1 == word->code_length) {
                    snprintf(instr_str, sizeof(instr_str), "; ");
                    has_semicolon = 1;
                }
                break;
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
            case OP_EXIT: snprintf(instr_str, sizeof(instr_str), "EXIT "); break;
            case OP_RECURSE: snprintf(instr_str, sizeof(instr_str), "RECURSE "); break;
            case OP_BEGIN: snprintf(instr_str, sizeof(instr_str), "BEGIN "); break;
            case OP_EMIT: snprintf(instr_str, sizeof(instr_str), "EMIT "); break;
            case OP_AGAIN: snprintf(instr_str, sizeof(instr_str), "AGAIN "); break;
            case OP_WHILE: 
                snprintf(instr_str, sizeof(instr_str), "WHILE ");
                branch_targets[branch_depth++] = instr.operand; // Sauvegarde la cible pour REPEAT
                break;
            case OP_REPEAT:
                snprintf(instr_str, sizeof(instr_str), "REPEAT ");
                if (branch_depth > 0) branch_depth--; // Ferme le WHILE
                break;
            case OP_CASE: snprintf(instr_str, sizeof(instr_str), "CASE "); break;
            case OP_OF: 
                snprintf(instr_str, sizeof(instr_str), "OF ");
                branch_targets[branch_depth++] = instr.operand; // Sauvegarde la cible pour ENDOF
                break;
            case OP_ENDOF:
                snprintf(instr_str, sizeof(instr_str), "ENDOF ");
                if (branch_depth > 0) branch_depth--; // Ferme l’OF
                break;
            case OP_ENDCASE: snprintf(instr_str, sizeof(instr_str), "ENDCASE "); break;
            default: snprintf(instr_str, sizeof(instr_str), "(OP_%d) ", instr.opcode); break;
        }

        strncat(def_msg, instr_str, sizeof(def_msg) - strlen(def_msg) - 1);

        if (branch_depth > 0 && i + 1 == branch_targets[branch_depth - 1] && instr.opcode != OP_END && instr.opcode != OP_REPEAT && instr.opcode != OP_ENDOF) {
            strncat(def_msg, "THEN ", sizeof(def_msg) - strlen(def_msg) - 1);
            branch_depth--;
        }
    }

    if (!has_semicolon) {
        strncat(def_msg, ";", sizeof(def_msg) - strlen(def_msg) - 1);
    }
    send_to_channel(def_msg);
}
void send_to_channel(const char *msg) {
    if (irc_socket != -1) {
        char response[512];
        size_t msg_len = strlen(msg);
        size_t chunk_size = 400;
        size_t offset = 0;

        while (offset < msg_len) {
            size_t remaining = msg_len - offset;
            size_t current_chunk_size = (remaining > chunk_size) ? chunk_size : remaining;
            // Si présence d'espaces, couper au dernier espace avant chunk_size
            if (current_chunk_size < remaining && strchr(msg + offset, ' ') != NULL) {
                for (size_t i = current_chunk_size; i > 0; i--) {
                    if (msg[offset + i] == ' ') {
                        current_chunk_size = i;
                        break;
                    }
                }
            }
            char chunk[401];
            strncpy(chunk, msg + offset, current_chunk_size);
            chunk[current_chunk_size] = '\0';
            snprintf(response, sizeof(response), "PRIVMSG %s :%s\r\n", CHANNEL, chunk);
            if (send(irc_socket, response, strlen(response), 0) < 0) {
                printf("Failed to send to channel: %s\n", chunk);
                break;
            }
            offset += current_chunk_size;
            // Passe les espaces uniquement si on en a trouvé
            while (offset < msg_len && msg[offset] == ' ') offset++;
        }
    } else {
        printf("IRC socket not initialized\n");
    }
}
void buffer_char(char c) {
    if (emit_buffer_pos < sizeof(emit_buffer) - 1) {
        emit_buffer[emit_buffer_pos++] = c;
        emit_buffer[emit_buffer_pos] = '\0'; // Terminateur
    } else {
        set_error("EMIT: Buffer full");
    }
}
void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word, int word_index) {
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
        case OP_NIP:
            pop(stack, *a); pop(stack, *b);
            if (!error_flag) {
                push(stack, *a);
            }
            break;
case OP_DOT:
    if (stack->top >= 0) {
        pop(stack, *a);
        char dot_msg[1024] = {0}; // Zéro pour éviter résidus
        int written = gmp_snprintf(dot_msg, sizeof(dot_msg), "%Zd", *a);
        if (written < 0 || written >= sizeof(dot_msg)) {
            send_to_channel("<overflow>");
        } else {
            send_to_channel(dot_msg); // Chunking géré par send_to_channel
        }
    } else {
        send_to_channel("Stack empty");
    }
    break;
        case OP_DOT_S:
            if (stack->top >= 0) {
                char stack_msg[4096] = "Stack: ";
                for (int i = 0; i <= stack->top; i++) {
                    char num[4096];
                    gmp_snprintf(num, sizeof(num), "%Zd ", stack->data[i]);
                    strncat(stack_msg, num, sizeof(stack_msg) - strlen(stack_msg) - 1);
                }
                send_to_channel(stack_msg);
            } else {
                send_to_channel("Stack empty");
            }
            break;
case OP_DOT_QUOTE:
            if (instr.operand >= 0 && instr.operand < word->string_count) {
                send_to_channel(word->strings[instr.operand]);
            } else {
                send_to_channel("Invalid string index for .\"");
            }
            break;
case OP_CR:
    if (emit_buffer_pos > 0) {
        send_to_channel(emit_buffer);
        emit_buffer[0] = '\0'; // Réinitialise le buffer
        emit_buffer_pos = 0;
    } else {
        send_to_channel(""); // Envoie une ligne vide si rien dans le buffer
    }
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
    if (return_stack.top >= 1) {  // index à top-1, limit à top
        mpz_set(*result, return_stack.data[return_stack.top - 1]);
        push(stack, *result);
    } else {
        set_error("I used outside of a loop");
    }
    break;
case OP_J:
    if (return_stack.top >= 4) {  // Deux boucles = 6 éléments (limit1, index1, addr1, limit2, index2, addr2)
        mpz_set(*result, return_stack.data[return_stack.top - 4]);  // Index de la boucle externe
        push(stack, *result);
    } else {
        set_error("J used outside of a nested loop");
    }
    break;
case OP_DO:
    if (stack->top < 1) {
        set_error("DO: Stack underflow");
        break;
    }
    pop(stack, *b);  // Start (index initial)
    pop(stack, *a);  // Limit
    if (!error_flag && return_stack.top < STACK_SIZE - 3) {  // Réserve 3 places : limit, index, addr
        return_stack.top += 3;  // Alloue espace
        mpz_set(return_stack.data[return_stack.top - 2], *a);  // limit
        mpz_set(return_stack.data[return_stack.top - 1], *b);  // index
        mpz_set_si(return_stack.data[return_stack.top], *ip + 1);  // addr
    } else if (!error_flag) {
        set_error("DO: Return stack overflow");
        push(stack, *a);  // Remet les valeurs si erreur
        push(stack, *b);
    }
    break;

case OP_LOOP:
    if (return_stack.top >= 2) {
        mpz_add_ui(return_stack.data[return_stack.top - 1], return_stack.data[return_stack.top - 1], 1);  // Incrémente index
        if (mpz_cmp(return_stack.data[return_stack.top - 1], return_stack.data[return_stack.top - 2]) < 0) {
            *ip = mpz_get_si(return_stack.data[return_stack.top]) - 1;  // Retour à addr
        } else {
            return_stack.top -= 3;  // Dépile limit, index, addr
        }
    } else {
        set_error("LOOP without DO");
    }
    break;

case OP_PLOOP:
    if (return_stack.top >= 2) {
        pop(stack, *a);  // Incrément
        if (!error_flag) {
            mpz_add(return_stack.data[return_stack.top - 1], return_stack.data[return_stack.top - 1], *a);
            if (mpz_cmp(return_stack.data[return_stack.top - 1], return_stack.data[return_stack.top - 2]) < 0) {
                *ip = mpz_get_si(return_stack.data[return_stack.top]) - 1;
            } else {
                return_stack.top -= 3;
            }
        }
    } else {
        set_error("+LOOP without DO");
    }
    break;

case OP_UNLOOP:
    if (return_stack.top >= 2) {
        return_stack.top -= 3;  // Dépile limit, index, addr
    } else {
        set_error("UNLOOP without DO");
    }
    break;
case OP_SQRT:
            pop(stack, *a); // Prend le nombre de la pile
            if (!error_flag) {
                if (mpz_sgn(*a) < 0) {
                    set_error("SQRT of negative number");
                } else {
                    mpz_sqrt(*result, *a); // Calcule la racine carrée
                    push(stack, *result);
                }
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
                executeCompiledWord(&dictionary[instr.operand], stack, instr.operand);
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
    *ip = word->code_length; // Saute après la fin, pas juste à OP_END
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
    if (dict_count > 0) {
        char words_msg[512] = "";
        size_t remaining = sizeof(words_msg) - 1;
        for (int i = 0; i < dict_count && remaining > 1; i++) {
            if (dictionary[i].name) {
                size_t name_len = strlen(dictionary[i].name);
                if (name_len + 1 < remaining) {
                    strncat(words_msg, dictionary[i].name, remaining);
                    strncat(words_msg, " ", remaining - name_len);
                    remaining -= (name_len + 1);
                } else {
                    send_to_channel("WORDS truncated: buffer full");
                    break;
                }
            } else {
                set_error("WORDS: Null name in dictionary");
                break;
            }
        }
        send_to_channel(words_msg);
    } else {
        send_to_channel("Dictionary empty");
    }
    break;
        case OP_FORGET:
            if (instr.operand >= 0 && instr.operand < dict_count) {
                for (int i = instr.operand; i < dict_count; i++) {
                    if (dictionary[i].name) {
                        free(dictionary[i].name);
                        dictionary[i].name = NULL;
                    }
                    for (int j = 0; j < dictionary[i].string_count; j++) {
                        if (dictionary[i].strings[j]) {
                            free(dictionary[i].strings[j]);
                            dictionary[i].strings[j] = NULL;
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
                memory[memory_count].size = 0;
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
case OP_FETCH:
            pop(stack, *b); // Index de la mémoire (ex. GREET)
            if (!error_flag && mpz_fits_slong_p(*b) && mpz_get_si(*b) >= 0 && mpz_get_si(*b) < memory_count) {
                int idx = mpz_get_si(*b);
                if (memory[idx].type == MEMORY_VARIABLE) {
                    push(stack, memory[idx].values[0]);
                } else if (memory[idx].type == MEMORY_ARRAY) {
                    pop(stack, *a);
                    if (!error_flag && mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) < memory[idx].size) {
                        push(stack, memory[idx].values[mpz_get_si(*a)]);
                    } else if (!error_flag) {
                        set_error("FETCH: Index out of bounds for array");
                    }
                } else if (memory[idx].type == MEMORY_STRING) {
                    push(stack, *b); // Pousse l’index de la mémoire sur la pile
                }
            } else if (!error_flag) {
                set_error("FETCH: Invalid memory index");
            }
            break;
case OP_STORE:
    pop(stack, *result); // Index mémoire (ex. TEST, sommet de la pile)
    if (!error_flag && mpz_fits_slong_p(*result) && mpz_get_si(*result) >= 0 && mpz_get_si(*result) < memory_count) {
        int idx = mpz_get_si(*result);
        if (memory[idx].type == MEMORY_VARIABLE) {
            // Cas variable simple : 12345 ZAZA !
            pop(stack, *a); // Valeur (ex. 12345)
            if (!error_flag) {
                mpz_set(memory[idx].values[0], *a); // Stocke la valeur dans la variable
            }
        } else if (memory[idx].type == MEMORY_ARRAY) {
            // Cas tableau : 12345 5 ZAZA !
            pop(stack, *b); // Index dans le tableau (ex. 5)
            pop(stack, *a); // Valeur (ex. 12345)
            if (!error_flag) {
                if (mpz_fits_slong_p(*b) && mpz_get_si(*b) >= 0 && mpz_get_si(*b) < memory[idx].size) {
                    mpz_set(memory[idx].values[mpz_get_si(*b)], *a); // Stocke la valeur à l’index du tableau
                } else {
                    set_error("STORE: Index out of bounds for array");
                }
            }
        } else if (memory[idx].type == MEMORY_STRING) {
            // Cas chaîne : "A string" TEST !
            pop(stack, *a); // Index de la chaîne dans string_stack
            if (!error_flag && mpz_fits_slong_p(*a) && 
                mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= string_stack_top) {
                char *str = string_stack[mpz_get_si(*a)];
                if (str) {
                    if (memory[idx].string) free(memory[idx].string);
                    memory[idx].string = strdup(str);
                    string_stack_top--; // Retire la chaîne de string_stack après stockage
                } else {
                    set_error("STORE: No string at stack index");
                }
            } else if (!error_flag) {
                set_error("STORE: Invalid string stack index");
            }
        } else {
            set_error("STORE: Unknown memory type");
        }
    } else if (!error_flag) {
        set_error("STORE: Invalid memory index");
    }
    break;
case OP_STRING:
            if (memory_count < VAR_SIZE) {
                memory[memory_count].name = strdup(word->strings[instr.operand]);
                memory[memory_count].type = MEMORY_STRING;
                memory[memory_count].string = strdup("");
                memory[memory_count].values = NULL;
                memory[memory_count].size = 0;
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
case OP_PRINT:
            pop(stack, *a); // Index de la mémoire (ex. GREET)
            if (!error_flag && mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) < memory_count) {
                int idx = mpz_get_si(*a);
                if (memory[idx].type == MEMORY_STRING && memory[idx].string) {
                    send_to_channel(memory[idx].string);
                } else {
                    set_error("PRINT: Not a string or empty");
                }
            } else if (!error_flag) {
                set_error("PRINT: Invalid memory index");
            }
            break;
case OP_STORE_STRING:
    pop(stack, *a); // Index dans string_stack
    pop(stack, *result); // Index de la mémoire (ex. toto)
    if (!error_flag && mpz_fits_slong_p(*result) && mpz_get_si(*result) >= 0 && mpz_get_si(*result) < memory_count) {
        int idx = mpz_get_si(*result);
        if (memory[idx].type == MEMORY_STRING) {
            if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= string_stack_top) {
                char *str = pop_string();
                if (str) {
                    if (memory[idx].string) free(memory[idx].string);
                    memory[idx].string = strdup(str);
                } else {
                    set_error("S!: No string to store");
                }
            } else {
                set_error("S!: Invalid string stack index");
            }
        } else {
            set_error("S!: Not a string variable");
        }
    } else if (!error_flag) {
        set_error("S!: Invalid memory index");
    }
    break;
    case OP_UNTIL:
    pop(stack, *a);
    if (!error_flag && mpz_cmp_si(*a, 0) == 0) {
        *ip = instr.operand - 1;
    }
    break;
case OP_AGAIN:
        *ip = instr.operand - 1; // Boucle vers BEGIN
        break;
case OP_QUOTE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        push_string(word->strings[instr.operand]);
        mpz_set_si(*result, string_stack_top);
        push(stack, *result);
    } else {
        set_error("QUOTE: Invalid string index");
    }
    break;
case OP_EMIT:
    pop(stack, *a);
    if (!error_flag) {
        long val = mpz_get_si(*a);
        if (val >= 0 && val <= 255) { // Vérification ASCII
            buffer_char((char)val);
        } else {
            set_error("EMIT: Value out of ASCII range");
        }
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
            pop(stack, *a); pop(stack, *b);
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
                char top_msg[512];
                gmp_snprintf(top_msg, sizeof(top_msg), "%Zd", stack->data[stack->top]);
                send_to_channel(top_msg);
            } else {
                set_error("TOP: Stack underflow");
            }
            break;
        case OP_IRC_CONNECT:
            irc_connect(stack);
            break;
        case OP_IRC_SEND:
            // Non implémenté ici, laissé pour compatibilité si tu veux l’ajouter
            set_error("IRC-SEND not implemented");
            break;
        case OP_RECURSE:
            if (word_index >= 0 && word_index < dict_count) {
                executeCompiledWord(&dictionary[word_index], stack, word_index);
            } else {
                set_error("RECURSE called with invalid word index");
            }
            break;
case OP_TO_R:
    pop(&main_stack, *a);  // Retire de la pile principale
    if (!error_flag) {
        if (return_stack.top < STACK_SIZE - 1) {
            mpz_set(return_stack.data[++return_stack.top], *a);  // Ajoute à return_stack
        } else {
            set_error(">R: Return stack overflow");
            push(&main_stack, *a);  // Remet la valeur si erreur
        }
    }
    break;

case OP_FROM_R:
    if (return_stack.top >= 0) {
        mpz_set(*result, return_stack.data[return_stack.top--]);  // Retire de return_stack
        push(&main_stack, *result);  // Ajoute à la pile principale
    } else {
        set_error("R>: Return stack underflow");
    }
    break;

case OP_R_FETCH:
    if (return_stack.top >= 0) {
        mpz_set(*result, return_stack.data[return_stack.top]);  // Copie sans dépiler
        push(&main_stack, *result);
    } else {
        set_error("R@: Return stack underflow");
    }
    break;
case OP_SEE:
    if (compiling || word_index >= 0) { // Mode compilé ou dans une définition
        print_word_definition_irc(instr.operand, stack);
    } else { // Mode immédiat
        pop(stack, *a);
        if (!error_flag && mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) < dict_count) {
            print_word_definition_irc(mpz_get_si(*a), stack);
        } else if (!error_flag) {
            set_error("SEE: Invalid word index");
        }
    }
    break;
    case OP_CLOCK:
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        set_error("CLOCK: Failed to get system time");
    } else {
        mpz_set_si(*result, (long)now);
        push(stack, *result);
    }
    break;

    }
}

void executeCompiledWord(CompiledWord *word, Stack *stack, int word_index) {
    long int ip = 0;
    while (ip < word->code_length && !error_flag) {
        executeInstruction(word->code[ip], stack, &ip, word, word_index);
        ip++;
    }
    if (error_flag) {
        send_to_channel("Execution aborted due to error");
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
        if (code_length <= WORD_CODE_SIZE) {
            memcpy(word->code, code, code_length * sizeof(Instruction));
            word->code_length = code_length;
            word->string_count = string_count;
            for (int i = 0; i < string_count; i++) {
                word->strings[i] = strings[i] ? strdup(strings[i]) : NULL;
            }
        } else {
            set_error("addCompiledWord: Code length exceeds limit");
        }
    } else if (dict_count < DICT_SIZE) {
        dictionary[dict_count].name = strdup(name);
        if (code_length <= WORD_CODE_SIZE) {
            memcpy(dictionary[dict_count].code, code, code_length * sizeof(Instruction));
            dictionary[dict_count].code_length = code_length;
            dictionary[dict_count].string_count = string_count;
            for (int i = 0; i < string_count; i++) {
                dictionary[dict_count].strings[i] = strings[i] ? strdup(strings[i]) : NULL;
            }
            dict_count++;
            if (findMemoryIndex("DP") >= 0) {
                mpz_set_si(memory[findMemoryIndex("DP")].values[0], dict_count);
            }
        } else {
            set_error("addCompiledWord: Code length exceeds limit");
        }
    } else {
        set_error("Dictionary full");
    }
}
void compileToken(char *token, char **input_rest, int *compile_error) {
    Instruction instr = {0};
 
if (strcmp(token, "(") == 0) {
    char *start = *input_rest;
    char *end = strstr(start, ")");
    if (!end) {
        send_to_channel("Missing closing parenthesis in comment");
        *compile_error = 1;
        return;
    }
    *input_rest = end + 1;
    return;
}
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
    } else if (strcmp(token, "MOD") == 0) {
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
    } else if (strcmp(token, "NIP") == 0) {
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
    } else if (strcmp(token, "J") == 0) {
    instr.opcode = OP_J;
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
} else if (strcmp(token, "+LOOP") == 0) { // Changement ici
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_DO) {
            instr.opcode = OP_PLOOP;
            currentWord.code[currentWord.code_length++] = instr;
            control_stack_top--;
        } else {
            set_error("+LOOP without DO");
            *compile_error = 1;
        }
    } else if (strcmp(token, "SQRT") == 0) {
        instr.opcode = OP_SQRT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "UNLOOP") == 0) {
        instr.opcode = OP_UNLOOP;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "EXIT") == 0) {
        instr.opcode = OP_EXIT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "LOAD") == 0) {
        char *start = *input_rest;
        while (*start && (*start == ' ' || *start == '\t')) start++;
        if (*start != '"') {
            send_to_channel("LOAD expects a quoted filename");
            return;
        }
        start++;
        char *end = strchr(start, '"');
        if (!end) {
            send_to_channel("Missing closing quote for LOAD");
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

    } else if (strcmp(token, "EMIT") == 0) {
        instr.opcode = OP_EMIT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "PRINT") == 0) {
        instr.opcode = OP_PRINT;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "S!") == 0) {
        instr.opcode = OP_STORE_STRING;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "PICK") == 0) { // Déjà ajouté
        instr.opcode = OP_PICK;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "RECURSE") == 0) { // Déjà ajouté
        instr.opcode = OP_RECURSE;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "@") == 0) {
    instr.opcode = OP_FETCH;
    currentWord.code[currentWord.code_length++] = instr;
} else if (strcmp(token, "!") == 0) {
    instr.opcode = OP_STORE;
    currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "BEGIN") == 0) {
        instr.opcode = OP_BEGIN;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_DO, currentWord.code_length - 1};
    } else if (strcmp(token, "WHILE") == 0) {
        instr.opcode = OP_WHILE;
        instr.operand = 0;
        currentWord.code[currentWord.code_length++] = instr;
        control_stack[control_stack_top++] = (ControlEntry){CT_DO, currentWord.code_length - 1};
} else if (strcmp(token, "REPEAT") == 0) {
    if (control_stack_top >= 2 && control_stack[control_stack_top-1].type == CT_DO && control_stack[control_stack_top-2].type == CT_DO) {
        instr.opcode = OP_REPEAT;
        currentWord.code[currentWord.code_length++] = instr;
        int begin_addr = control_stack[control_stack_top-2].addr; // BEGIN, pas WHILE
        currentWord.code[currentWord.code_length-1].operand = begin_addr;
        instr.opcode = OP_BRANCH;
        instr.operand = currentWord.code_length + 1; // Après le BRANCH
        currentWord.code[control_stack[control_stack_top-1].addr].operand = currentWord.code_length; // Met à jour WHILE
        currentWord.code[currentWord.code_length++] = instr;
        control_stack_top -= 2;
    } else {
        set_error("REPEAT without BEGIN/WHILE");
        *compile_error = 1;
    }
        } else if (strcmp(token, "AGAIN") == 0) {
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_DO) {
            instr.opcode = OP_AGAIN;
            currentWord.code[currentWord.code_length++] = instr;
            int begin_addr = control_stack[control_stack_top-1].addr; // Adresse du BEGIN
            currentWord.code[currentWord.code_length-1].operand = begin_addr;
            control_stack_top--; // Ferme le BEGIN
        } else {
            set_error("AGAIN without BEGIN");
            *compile_error = 1;
        }
        } else if (strcmp(token, "UNTIL") == 0) {
    if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_DO) {
        instr.opcode = OP_UNTIL;
        instr.operand = control_stack[control_stack_top-1].addr; // Retour au BEGIN
        currentWord.code[currentWord.code_length++] = instr;
        control_stack_top--; // Ferme le BEGIN
    } else {
        set_error("UNTIL without BEGIN");
        *compile_error = 1;
    }
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
            currentWord.code[control_stack[control_stack_top-1].addr].operand = currentWord.code_length;
            control_stack[control_stack_top-1].type = CT_ENDOF;
            control_stack[control_stack_top-1].addr = currentWord.code_length - 1;
        } else {
            set_error("ENDOF without OF");
            *compile_error = 1;
        }
    } else if (strcmp(token, "ENDCASE") == 0) {
        if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_ENDOF) {
            instr.opcode = OP_ENDCASE;
            currentWord.code[currentWord.code_length++] = instr;
            while (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_ENDOF) {
                currentWord.code[control_stack[--control_stack_top].addr].operand = currentWord.code_length;
            }
            if (control_stack_top > 0 && control_stack[control_stack_top-1].type == CT_CASE) {
                control_stack_top--; // Ferme le CASE
            } else {
                set_error("ENDCASE without CASE");
                *compile_error = 1;
            }
        } else {
            set_error("ENDCASE without matching OF");
            *compile_error = 1;
        }
    } else if (strcmp(token, "SEE") == 0) {
    char *next_token = strtok_r(NULL, " \t\n", input_rest);
    if (!next_token) {
        send_to_channel("SEE requires a word name");
        *compile_error = 1;
        return;
    }
     int index = findCompiledWordIndex(next_token);
    if (index >= 0) {
        instr.opcode = OP_SEE;
        instr.operand = index;
        currentWord.code[currentWord.code_length++] = instr;
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "SEE: Unknown word: %s", next_token);
        send_to_channel(msg);
        *compile_error = 1;
    }
        } else if (strcmp(token, "VARIABLE") == 0) {
        char *next_token = strtok_r(NULL, " \t\n", input_rest);
        if (!next_token) {
            send_to_channel("VARIABLE requires a name");
            return;
        }
        instr.opcode = OP_VARIABLE;
        instr.operand = currentWord.string_count;
        currentWord.strings[currentWord.string_count++] = strdup(next_token);
        currentWord.code[currentWord.code_length++] = instr;
} else if (strcmp(token, ">R") == 0) {
        instr.opcode = OP_TO_R;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "R>") == 0) {
        instr.opcode = OP_FROM_R;
        currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "R@") == 0) {
        instr.opcode = OP_R_FETCH;
        currentWord.code[currentWord.code_length++] = instr;
        } else if (strcmp(token, "CLOCK") == 0) {
    instr.opcode = OP_CLOCK;
    currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "&") == 0) {
    instr.opcode = OP_BIT_AND;
    currentWord.code[currentWord.code_length++] = instr;
    } else if (strcmp(token, "LSHIFT") == 0) {
    instr.opcode = OP_LSHIFT;
    currentWord.code[currentWord.code_length++] = instr;

    } else if (strcmp(token, "RSHIFT") == 0) {
    instr.opcode = OP_RSHIFT;
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
} else if (strcmp(token, "CREATE") == 0) {
    char *next_token = strtok_r(NULL, " \t\n", input_rest);
    if (!next_token) {
        send_to_channel("CREATE requires a name");
        *compile_error = 1;
        return;
    }
    instr.opcode = OP_CREATE;
    instr.operand = currentWord.string_count;
    currentWord.strings[currentWord.string_count++] = strdup(next_token);
    currentWord.code[currentWord.code_length++] = instr;
    return;
} else if (strcmp(token, "ALLOT") == 0) {
    instr.opcode = OP_ALLOT;
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
} else if (strcmp(token, "WORDS") == 0) {
    instr.opcode = OP_WORDS;
    currentWord.code[currentWord.code_length++] = instr;
} else if (strcmp(token, "FORGET") == 0) {
    char *next_token = strtok_r(NULL, " \t\n", input_rest);
    if (!next_token) {
        send_to_channel("FORGET requires a word name");
        *compile_error = 1;
        return;
    }
    int index = findCompiledWordIndex(next_token);
    if (index >= 0) {
        instr.opcode = OP_FORGET;
        instr.operand = index;
        currentWord.code[currentWord.code_length++] = instr;
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "FORGET: Unknown word: %s", next_token);
        send_to_channel(msg);
        *compile_error = 1;
    }
    return;
    } else if (strcmp(token, ".\"") == 0) {
    char *start = *input_rest;
    char *end = strchr(start, '"');
    if (!end) {
        send_to_channel("Missing closing quote for .\"");
        *compile_error = 1;
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
    *input_rest = end + 1; // Avance après le " fermant
    while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++; // Passe les espaces
    return;
        } else if (strcmp(token, "CLOCK") == 0) {
    instr.opcode = OP_CLOCK;
    currentWord.code[currentWord.code_length++] = instr;
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
                char msg[512];
                snprintf(msg, sizeof(msg), "Unknown word: %s", token);
                send_to_channel(msg);
                *compile_error = 1;
            }
            mpz_clear(test_num);
        }
    }
}
void interpret(char *input, Stack *stack) {
    error_flag = 0;
    int compile_error = 0;
    char *saveptr;
    char *token = strtok_r(input, " \t\n", &saveptr);
    while (token && !error_flag) {
        // Gestion des commentaires entre parenthèses "( )"
        if (strcmp(token, "(") == 0) {
            char *end = strstr(saveptr, ")");
            if (!end) {
                send_to_channel("Missing closing parenthesis for comment");
                return;
            }
            saveptr = end + 1;
            token = strtok_r(NULL, " \t\n", &saveptr);
            continue;
        }

        CompiledWord temp = {.code_length = 0, .string_count = 0};
        mpz_t big_value;
        mpz_init(big_value);

        if (compiling) {
            if (strcmp(token, ";") == 0) {
                if (currentWord.code_length >= WORD_CODE_SIZE - 1) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Definition failed for %s: code length exceeds %d", currentWord.name, WORD_CODE_SIZE);
                    send_to_channel(msg);
                    compile_error = 1;
                } else {
                    Instruction end = {OP_END, 0};
                    currentWord.code[currentWord.code_length++] = end;
                }

                if (compile_error) {
                    int index = findCompiledWordIndex(currentWord.name);
                    if (index >= 0) {
                        Instruction forget_instr = {OP_FORGET, index};
                        CompiledWord temp_forget = {.code_length = 1, .string_count = 0};
                        temp_forget.code[0] = forget_instr;
                        executeCompiledWord(&temp_forget, stack, -1);
                        char msg[512];
                        snprintf(msg, sizeof(msg), "Definition aborted due to error, %s forgotten", currentWord.name);
                        send_to_channel(msg);
                    } else {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "Definition failed for %s: compilation error", currentWord.name);
                        send_to_channel(msg);
                    }
                } else {
                    addCompiledWord(currentWord.name, currentWord.code, currentWord.code_length, 
                                    currentWord.strings, currentWord.string_count);
                }

                free(currentWord.name);
                for (int i = 0; i < currentWord.string_count; i++) {
                    if (currentWord.strings[i]) free(currentWord.strings[i]);
                }
                compiling = 0;
                current_word_index = -1;
                compile_error = 0;
            } else {
                compileToken(token, &saveptr, &compile_error);
                if (compile_error) {
                    send_to_channel("Compilation aborted due to error");
                    free(currentWord.name);
                    for (int i = 0; i < currentWord.string_count; i++) {
                        if (currentWord.strings[i]) free(currentWord.strings[i]);
                    }
                    compiling = 0;
                    current_word_index = -1;
                    compile_error = 0;
                    break; // Sort immédiatement après une erreur
                }
            }
        } else {
            if (token[0] == '"') { // Début d’une chaîne
                char *start = token + 1; // Passe le premier "
                char *end = strchr(saveptr, '"'); // Cherche le " fermant
                if (!end) {
                    send_to_channel("Missing closing quote");
                    mpz_clear(big_value);
                    return;
                }
                long int len = end - saveptr; // Longueur jusqu’au " fermant
                char *str = malloc(len + 1);
                strncpy(str, saveptr, len);
                str[len] = '\0';
                temp.code_length = 1;
                temp.code[0].opcode = OP_QUOTE;
                temp.code[0].operand = temp.string_count;
                temp.strings[temp.string_count++] = str;
                executeCompiledWord(&temp, stack, -1);
                saveptr = end + 1; // Avance après le " fermant
            } else if (mpz_set_str(big_value, token, 10) == 0) {
                push(stack, big_value);
            } else if (strcmp(token, ":") == 0) {
                token = strtok_r(NULL, " \t\n", &saveptr);
                if (token) {
                    compiling = 1;
                    currentWord.name = strdup(token);
                    currentWord.code_length = 0;
                    currentWord.string_count = 0;
                    current_word_index = findCompiledWordIndex(currentWord.name);
                    if (current_word_index < 0) current_word_index = dict_count;
                } else {
                    send_to_channel("Colon requires a word name");
                }
            } else if (strcmp(token, "LOAD") == 0) {
                char *start = saveptr;
                while (*start && (*start == ' ' || *start == '\t')) start++;
                if (*start != '"') {
                    send_to_channel("LOAD expects a quoted filename");
                    mpz_clear(big_value);
                    return;
                }
                start++;
                char *end = strchr(start, '"');
                if (!end) {
                    send_to_channel("Missing closing quote for LOAD");
                    mpz_clear(big_value);
                    return;
                }
                long int len = end - start;
                char filename[MAX_STRING_SIZE];
                strncpy(filename, start, len);
                filename[len] = '\0';
                FILE *file = fopen(filename, "r");
                if (!file) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Cannot open file: %s", filename);
                    send_to_channel(msg);
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
                    send_to_channel("Missing closing quote for .\"");
                    mpz_clear(big_value);
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
                executeCompiledWord(&temp, stack, -1);
                free(str);
                saveptr = end + 1;
            } else if (strcmp(token, "STRING") == 0) {
                char *next_token = strtok_r(NULL, " \t\n", &saveptr);
                if (!next_token) {
                    send_to_channel("STRING requires a name");
                    mpz_clear(big_value);
                    return;
                }
                temp.code_length = 1;
                temp.code[0].opcode = OP_STRING;
                temp.code[0].operand = temp.string_count;
                temp.strings[temp.string_count++] = strdup(next_token);
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "+") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_ADD, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "-") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_SUB, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "*") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_MUL, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "/") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DIV, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "MOD") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_MOD, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "DUP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DUP, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "SWAP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_SWAP, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "OVER") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_OVER, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "ROT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_ROT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "DROP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DROP, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "NIP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_NIP, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "=") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_EQ, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "<") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_LT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, ">") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_GT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "AND") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_AND, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "OR") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_OR, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "NOT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_NOT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "I") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_I, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "J") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_J, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "CR") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_CR, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, ".S") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DOT_S, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, ".") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DOT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "FLUSH") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_FLUSH, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "EXIT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_EXIT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "&") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_BIT_AND, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "|") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_BIT_OR, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "^") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_BIT_XOR, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "~") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_BIT_NOT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "LSHIFT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_LSHIFT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "RSHIFT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_RSHIFT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "WORDS") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_WORDS, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "FORGET") == 0) {
                char *next_token = strtok_r(NULL, " \t\n", &saveptr);
                if (!next_token) {
                    send_to_channel("FORGET requires a word name");
                    mpz_clear(big_value);
                    return;
                }
                int index = findCompiledWordIndex(next_token);
                if (index >= 0) {
                    temp.code_length = 1;
                    temp.code[0] = (Instruction){OP_FORGET, index};
                    executeCompiledWord(&temp, stack, -1);
                } else {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "FORGET: Unknown word: %s", next_token);
                    send_to_channel(msg);
                }
            } else if (strcmp(token, "VARIABLE") == 0) {
                char *next_token = strtok_r(NULL, " \t\n", &saveptr);
                if (!next_token) {
                    send_to_channel("VARIABLE requires a name");
                    mpz_clear(big_value);
                    return;
                }
                temp.code_length = 1;
                temp.code[0].opcode = OP_VARIABLE;
                temp.code[0].operand = temp.string_count;
                temp.strings[temp.string_count++] = strdup(next_token);
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "CREATE") == 0) {
                char *next_token = strtok_r(NULL, " \t\n", &saveptr);
                if (!next_token) {
                    send_to_channel("CREATE requires a name");
                    mpz_clear(big_value);
                    return;
                }
                temp.code_length = 1;
                temp.code[0].opcode = OP_CREATE;
                temp.code[0].operand = temp.string_count;
                temp.strings[temp.string_count++] = strdup(next_token);
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "ALLOT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_ALLOT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "!") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_STORE, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "@") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_FETCH, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "PICK") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_PICK, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "ROLL") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_ROLL, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "+!") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_PLUSSTORE, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "DEPTH") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_DEPTH, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "TOP") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_TOP, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "SEE") == 0) {
                char *next_token = strtok_r(NULL, " \t\n", &saveptr);
                if (!next_token) {
                    send_to_channel("SEE requires a word name");
                    mpz_clear(big_value);
                    return;
                }
                int index = findCompiledWordIndex(next_token);
                if (index >= 0) {
                    mpz_set_si(big_value, index);
                    push(stack, big_value);
                    temp.code_length = 1;
                    temp.code[0] = (Instruction){OP_SEE, 0};
                    executeCompiledWord(&temp, stack, -1);
                } else {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "SEE: Unknown word: %s", next_token);
                    send_to_channel(msg);
                }
            } else if (strcmp(token, "IRC-CONNECT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_IRC_CONNECT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "IRC-SEND") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_IRC_SEND, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "RECURSE") == 0) {
                if (current_word_index >= 0) {
                    temp.code_length = 1;
                    temp.code[0] = (Instruction){OP_RECURSE, 0};
                    executeCompiledWord(&temp, stack, current_word_index);
                } else {
                    send_to_channel("RECURSE used outside a definition");
                }
            } else if (strcmp(token, "EMIT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_EMIT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "SQRT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_SQRT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "S!") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_STORE_STRING, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "PRINT") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_PRINT, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, ">R") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_TO_R, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "R>") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_FROM_R, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "R@") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_R_FETCH, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "CLOCK") == 0) {
                temp.code_length = 1;
                temp.code[0] = (Instruction){OP_CLOCK, 0};
                executeCompiledWord(&temp, stack, -1);
            } else if (strcmp(token, "UNTIL") == 0) {
                send_to_channel("UNTIL can only be used inside a definition");
            } else {
                long int index = findCompiledWordIndex(token);
                if (index >= 0) {
                    temp.code_length = 1;
                    temp.code[0] = (Instruction){OP_CALL, index};
                    executeCompiledWord(&temp, stack, index);
                } else {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Unknown word: %s", token);
                    send_to_channel(msg);
                }
            }
            mpz_clear(big_value);
        }
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    if (compiling && !token) {
        send_to_channel("Error: Incomplete definition detected, resetting compilation");
        free(currentWord.name);
        for (int i = 0; i < currentWord.string_count; i++) {
            if (currentWord.strings[i]) free(currentWord.strings[i]);
        }
        compiling = 0;
        current_word_index = -1;
        compile_error = 0;
    }
}
void irc_connect(Stack *stack) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Socket creation failed\n");
        return;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(6667);
    server.sin_addr.s_addr = inet_addr("94.125.182.252"); 

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Connection to labynet.fr failed\n");
        close(sock);
        return;
    }

    irc_socket = sock;

    char nick_cmd[512], user_cmd[512], join_cmd[512];
    snprintf(nick_cmd, sizeof(nick_cmd), "NICK %s\r\n", BOT_NAME);
    snprintf(user_cmd, sizeof(user_cmd), "USER %s 0 * :Forth IRC Bot\r\n", BOT_NAME);
    snprintf(join_cmd, sizeof(join_cmd), "JOIN %s\r\n", CHANNEL);
    send(irc_socket, nick_cmd, strlen(nick_cmd), 0);
    send(irc_socket, user_cmd, strlen(user_cmd), 0);
    send(irc_socket, join_cmd, strlen(join_cmd), 0);

    mpz_set_si(mpz_pool[0], irc_socket);
    push(stack, mpz_pool[0]);
}

 

int main() {
    // Initialisation des deux piles
        int sock;
    initStack(&main_stack);
    initStack(&return_stack);
    init_mpz_pool();

    // Initialisation de DP (comme dans ton code)
    char dp_cmd[] = "VARIABLE DP DROP";
    interpret(dp_cmd, &main_stack);  // Passe explicitement main_stack
    int dp_idx = findMemoryIndex("DP");
    if (dp_idx >= 0) {
        mpz_set_si(memory[dp_idx].values[0], 0);
    } else {
        printf("DP non trouvé\n");
    }

    printf("Forth-like interpreter with GMP\n");
    char buffer[512];
    while (1) {
        irc_connect(&main_stack);  // Passe main_stack pour stocker le socket
        if (main_stack.top < 0) {
            printf("Failed to connect, retrying in 5 seconds...\n");
            sleep(5);
            continue;
        }
        int sock = mpz_get_si(main_stack.data[main_stack.top]);
        main_stack.top--;  // Dépile le socket

        printf("Connected to labynet.fr\n");

        while (1) {
            int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                printf("Disconnected from labynet.fr, attempting to reconnect in 5 seconds...\n");
                close(sock);
                sleep(5);
                break;
            }
            buffer[bytes] = '\0';

            if (strstr(buffer, "PING ") == buffer) {
                char pong[512];
                snprintf(pong, sizeof(pong), "PONG %s\r\n", buffer + 5);
                send(sock, pong, strlen(pong), 0);
                continue;
            }

            char prefix[512];
            snprintf(prefix, sizeof(prefix), "PRIVMSG %s :%s:", CHANNEL, BOT_NAME);
            char *msg = strstr(buffer, prefix);
            if (msg) {
                char forth_cmd[512];
                snprintf(forth_cmd, sizeof(forth_cmd), "%s", msg + strlen(prefix));
                forth_cmd[strcspn(forth_cmd, "\r\n")] = '\0';
                interpret(forth_cmd, &main_stack);  // Passe main_stack
            }
        }
    }
 
    clearStack(&main_stack);  // Nettoie main_stack
    close(sock);
    clear_mpz_pool();
    return 0;
}
