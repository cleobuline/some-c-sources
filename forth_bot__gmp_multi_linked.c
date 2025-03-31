
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include <time.h>
#include <ctype.h>
#include "memory_forth.h"
 

#define STACK_SIZE 1000
#define WORD_CODE_SIZE 512
#define CONTROL_STACK_SIZE 100
#define MAX_STRING_SIZE 256
#define MPZ_POOL_SIZE 3
#define BUFFER_SIZE 2048
 
#define SERVER "46.16.175.175" // default 
#define PORT 6667
#define BOT_NAME "mforth"
#define USER "mforth"
#define CHANNEL "##forth"

// Après les #define et les includes
typedef enum {
    OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_DUP, OP_SWAP, OP_OVER,
    OP_ROT, OP_DROP, OP_EQ, OP_LT, OP_GT, OP_AND, OP_OR, OP_NOT, OP_XOR, OP_I, OP_J,
    OP_DO, OP_LOOP, OP_BRANCH_FALSE, OP_BRANCH,
    OP_UNLOOP, OP_PLUS_LOOP, OP_SQRT, OP_CALL, OP_END, OP_DOT_QUOTE, OP_LITSTRING,
    OP_CR, OP_DOT_S, OP_DOT, OP_CASE, OP_OF, OP_ENDOF, OP_ENDCASE,
    OP_EXIT, OP_BEGIN, OP_WHILE, OP_REPEAT,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT, OP_LSHIFT, OP_RSHIFT,
    OP_WORDS, OP_LOAD, OP_FORGET, OP_VARIABLE, OP_FETCH, OP_STORE,
    OP_PICK, OP_ROLL, OP_PLUSSTORE, OP_DEPTH, OP_TOP, OP_NIP, OP_MOD,
    OP_CREATE, OP_ALLOT, OP_RECURSE, OP_IRC_CONNECT, OP_IRC_SEND, OP_EMIT,
    OP_STRING, OP_QUOTE, OP_PRINT, OP_NUM_TO_BIN, OP_PRIME_TEST, OP_AGAIN,
    OP_TO_R, OP_FROM_R, OP_R_FETCH, OP_UNTIL, OP_CLEAR_STACK, OP_CLOCK, OP_SEE, OP_2DROP
} OpCode;

typedef struct {
    OpCode opcode;
    long int operand;
} Instruction;

// Définir CompiledWord avant DynamicDictionary
typedef struct {
    char *name;
    Instruction code[WORD_CODE_SIZE];
    long int code_length;
    char *strings[WORD_CODE_SIZE];
    long int string_count;
    int immediate;
} CompiledWord;

typedef struct {
    CompiledWord *words;   // Pointeur vers les mots alloués dynamiquement
    long int count;        // Nombre de mots actuels
    long int capacity;     // Capacité actuelle du tableau
} DynamicDictionary;
 
 
// Structure pour une pile Forth
typedef struct {
    mpz_t data[STACK_SIZE];
    long int top;
} Stack;

// Structure pour les structures de contrôle
typedef enum { CT_IF, CT_ELSE, CT_DO, CT_CASE, CT_OF, CT_ENDOF, CT_BEGIN, CT_WHILE, CT_REPEAT } ControlType;

typedef struct {
    ControlType type;
    long int addr;
} ControlEntry;

// Structure pour la mémoire Forth
typedef enum {
    MEMORY_VARIABLE,
    MEMORY_ARRAY,
    MEMORY_STRING
} MemoryType;

typedef struct {
    char *name;
    MemoryType type;
    mpz_t *values;
    char *string;
    long int size;
} Memory;

#define LOOP_STACK_SIZE 16  // Taille max de la pile de boucles, ajustable

typedef struct {
    mpz_t index;    // Index actuel de la boucle
    mpz_t limit;    // Limite de la boucle
} LoopEntry;

// Structure Env pour le multi-utilisateur
typedef struct Env {
    char nick[MAX_STRING_SIZE];
    Stack main_stack;
    Stack return_stack;
    LoopEntry loop_stack[LOOP_STACK_SIZE];
    int loop_stack_top;
    DynamicDictionary dictionary; // Remplace CompiledWord dictionary[DICT_SIZE]
    MemoryList memory_list;
    char output_buffer[BUFFER_SIZE];
    int buffer_pos;
    CompiledWord currentWord;
    int compiling;
    int compile_error;
    long int current_word_index;
    ControlEntry control_stack[CONTROL_STACK_SIZE];
    int control_stack_top;
    char *string_stack[STACK_SIZE];
    int string_stack_top;
    int error_flag;
    char emit_buffer[512];
    int emit_buffer_pos;
    struct Env *next;
} Env;

void executeCompiledWord(CompiledWord *word, Stack *stack, int word_index);
void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word, int word_index);
void interpret(char *input, Stack *stack);
void compileToken(char *token, char **input_rest, Env *env);
void send_to_channel(const char *msg) ;

// Variables globales
Env *head = NULL;
Env *currentenv = NULL;
static int irc_socket = -1;
mpz_t mpz_pool[MPZ_POOL_SIZE];
char * channel ; 

// Fonctions utilitaires
void init_mpz_pool() {
    for (int i = 0; i < MPZ_POOL_SIZE; i++) mpz_init(mpz_pool[i]);
}

void clear_mpz_pool() {
    for (int i = 0; i < MPZ_POOL_SIZE; i++) mpz_clear(mpz_pool[i]);
}
void executeCompiledWord(CompiledWord *word, Stack *stack, int word_index) {
    long int ip = 0;
    while (ip < word->code_length && !currentenv->error_flag) {
        executeInstruction(word->code[ip], stack, &ip, word, word_index);
        ip++;
    }
}
 
void initDynamicDictionary(DynamicDictionary *dict) {
    dict->capacity = 16; // Capacité initiale, ajustable
    dict->count = 0;
    dict->words = (CompiledWord *)malloc(dict->capacity * sizeof(CompiledWord));
    if (!dict->words) {
        send_to_channel("Erreur : Échec de l’allocation du dictionnaire");
        exit(1); // Ou gérer autrement
    }
    for (long int i = 0; i < dict->capacity; i++) {
        dict->words[i].name = NULL;
        dict->words[i].code_length = 0;
        dict->words[i].string_count = 0;
        dict->words[i].immediate = 0;
    }
}

void resizeDynamicDictionary(DynamicDictionary *dict) {
    long int new_capacity = dict->capacity * 2;
    CompiledWord *new_words = (CompiledWord *)realloc(dict->words, new_capacity * sizeof(CompiledWord));
    if (!new_words) {
        send_to_channel("Erreur : Échec du redimensionnement du dictionnaire");
        exit(1); // Ou gérer autrement
    }
    dict->words = new_words;
    for (long int i = dict->count; i < new_capacity; i++) {
        dict->words[i].name = NULL;
        dict->words[i].code_length = 0;
        dict->words[i].string_count = 0;
        dict->words[i].immediate = 0;
    }
    dict->capacity = new_capacity;
}

void addWord(DynamicDictionary *dict, const char *name, OpCode opcode, int immediate) {
    if (dict->count >= dict->capacity) {
        resizeDynamicDictionary(dict);
    }
    CompiledWord *word = &dict->words[dict->count];
    word->name = strdup(name);
    word->code[0].opcode = opcode;
    word->code_length = 1;
    word->string_count = 0;
    word->immediate = immediate;
    dict->count++;
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
            snprintf(response, sizeof(response), "PRIVMSG %s :%s\r\n", channel, chunk);
            if (send(irc_socket, response, strlen(response), 0) < 0) {
                printf("Failed to send to channel: %s\n", chunk);
                break;
            }
            offset += current_chunk_size;
            while (offset < msg_len && msg[offset] == ' ') offset++;
        }
    } else {
        printf("IRC socket not initialized\n");
    }
}
void initEnv(Env *env, const char *nick) {
    strncpy(env->nick, nick, MAX_STRING_SIZE - 1);
    env->nick[MAX_STRING_SIZE - 1] = '\0';

    env->main_stack.top = -1;
    env->return_stack.top = -1;
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_init(env->main_stack.data[i]);
        mpz_init(env->return_stack.data[i]);
    }

    initDynamicDictionary(&env->dictionary);

    memory_init(&env->memory_list);

    // Ajout de la variable USERNAME
    unsigned long username_idx = memory_create(&env->memory_list, "USERNAME", TYPE_STRING);
    if (username_idx != 0) {
        MemoryNode *username_node = memory_get(&env->memory_list, username_idx);
        if (username_node) {
            memory_store(&env->memory_list, username_idx, nick); // Stocker le nick dans USERNAME
            // Ajouter USERNAME au dictionnaire
            if (env->dictionary.count >= env->dictionary.capacity) {
                resizeDynamicDictionary(&env->dictionary);
            }
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name = strdup("USERNAME");
            env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = username_idx;
            env->dictionary.words[dict_idx].code_length = 1;
            env->dictionary.words[dict_idx].string_count = 0;
            env->dictionary.words[dict_idx].immediate = 0;
        }
    }

    env->buffer_pos = 0;
    memset(env->output_buffer, 0, BUFFER_SIZE);
    env->currentWord.name = NULL;
    env->currentWord.code_length = 0;
    env->currentWord.string_count = 0;
    env->compiling = 0;
    env->current_word_index = -1;

    env->control_stack_top = 0;

    env->string_stack_top = -1;
    for (int i = 0; i < STACK_SIZE; i++) env->string_stack[i] = NULL;

    env->error_flag = 0;
    env->emit_buffer[0] = '\0';
    env->emit_buffer_pos = 0;

    env->next = NULL;
}
 
Env *createEnv(const char *nick) {
    Env *new_env = (Env *)malloc(sizeof(Env));
    if (!new_env) {
        send_to_channel("createEnv: Memory allocation failed");
        return NULL;
    }
    initEnv(new_env, nick);
    new_env->next = head;
    head = new_env;
    return new_env;
}

void freeEnv(const char *nick) {
    Env *prev = NULL, *curr = head;
    while (curr && strcmp(curr->nick, nick) != 0) {
        prev = curr;
        curr = curr->next;
    }
    if (!curr) return;

    if (prev) prev->next = curr->next;
    else head = curr->next;

    if (curr == currentenv) currentenv = NULL;

    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(curr->main_stack.data[i]);
        mpz_clear(curr->return_stack.data[i]);
    }

    for (long int i = 0; i < curr->dictionary.count; i++) {
        if (curr->dictionary.words[i].name) free(curr->dictionary.words[i].name);
        for (int j = 0; j < curr->dictionary.words[i].string_count; j++) {
            if (curr->dictionary.words[i].strings[j]) free(curr->dictionary.words[i].strings[j]);
        }
    }
    free(curr->dictionary.words); // Libérer le tableau dynamique

    MemoryNode *node = curr->memory_list.head;
    while (node) {
        MemoryNode *next = node->next;
        memory_free(&curr->memory_list, node->name);
        node = next;
    }

    if (curr->currentWord.name) free(curr->currentWord.name);
    for (int i = 0; i < curr->currentWord.string_count; i++) {
        if (curr->currentWord.strings[i]) free(curr->currentWord.strings[i]);
    }
    for (int i = 0; i <= curr->string_stack_top; i++) {
        if (curr->string_stack[i]) free(curr->string_stack[i]);
    }
    free(curr);
}
Env *findEnv(const char *nick) {
    Env *curr = head;
    while (curr && strcmp(curr->nick, nick) != 0) curr = curr->next;
    if (curr) {
        currentenv = curr;
        return curr;
    }
    return NULL;
}

// Fonctions Forth
void push(Stack *stack, mpz_t value) {
    if (stack->top < STACK_SIZE - 1) {
        mpz_set(stack->data[++stack->top], value);
    } else {
        if (currentenv) currentenv->error_flag = 1;
        send_to_channel("Error: Stack overflow");
    }
}

void pop(Stack *stack, mpz_t result) {
    if (stack->top >= 0) {
        mpz_set(result, stack->data[stack->top--]);
    } else {
        if (currentenv) currentenv->error_flag = 1;
        send_to_channel("Error: Stack underflow");
        mpz_set_ui(result, 0);
    }
}

void push_string(char *str) {
    if (!currentenv) return;
    if (currentenv->string_stack_top < STACK_SIZE - 1) {
        currentenv->string_stack[++currentenv->string_stack_top] = str;
    } else {
        currentenv->error_flag = 1;
        send_to_channel("Error: String stack overflow");
    }
}

char *pop_string() {
    if (!currentenv) return NULL;
    if (currentenv->string_stack_top >= 0) {
        return currentenv->string_stack[currentenv->string_stack_top--];
    } else {
        currentenv->error_flag = 1;
        send_to_channel("Error: String stack underflow");
        return NULL;
    }
}

void set_error(const char *msg) {
    if (!currentenv) return;
    char err_msg[512];
    snprintf(err_msg, sizeof(err_msg), "Error: %s", msg);
    send_to_channel(err_msg);
    currentenv->error_flag = 1;
    // Nettoyer return_stack si utilisé dans une boucle
    while (currentenv->return_stack.top >= 2 && currentenv->return_stack.top % 3 == 0) {
        currentenv->return_stack.top -= 3; // Dépile limit, index, addr
    }
}

int findCompiledWordIndex(char *name) {
    if (!currentenv) return -1;
    for (long int i = 0; i < currentenv->dictionary.count; i++) {
        if (currentenv->dictionary.words[i].name && strcmp(currentenv->dictionary.words[i].name, name) == 0) return i;
    }
    return -1;
}


void print_word_definition_irc(int index, Stack *stack) {
    if (!currentenv || index < 0 || index >= currentenv->dictionary.count) {
        send_to_channel("SEE: Unknown word");
        return;
    }

    CompiledWord *word = &currentenv->dictionary.words[index];
    char def_msg[512] = "";
    snprintf(def_msg, sizeof(def_msg), ": %s ", word->name);

    // Tableau pour suivre les cibles des branchements (IF, OF, etc.)
    long int branch_targets[WORD_CODE_SIZE];
    int branch_depth = 0;
    int has_semicolon = 0;

    for (long int i = 0; i < word->code_length; i++) {
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
            case OP_CALL:
                if (instr.operand < currentenv->dictionary.count && currentenv->dictionary.words[instr.operand].name) {
                    snprintf(instr_str, sizeof(instr_str), "%s ", currentenv->dictionary.words[instr.operand].name);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "(CALL %ld) ", instr.operand);
                }
                break;
            case OP_ADD: snprintf(instr_str, sizeof(instr_str), "+ "); break;
            case OP_SUB: snprintf(instr_str, sizeof(instr_str), "- "); break;
            case OP_MUL: snprintf(instr_str, sizeof(instr_str), "* "); break;
            case OP_DIV: snprintf(instr_str, sizeof(instr_str), "/ "); break;
            case OP_MOD: snprintf(instr_str, sizeof(instr_str), "MOD "); break;
            case OP_DUP: snprintf(instr_str, sizeof(instr_str), "DUP "); break;
            case OP_DROP: snprintf(instr_str, sizeof(instr_str), "DROP "); break;
            case OP_SWAP: snprintf(instr_str, sizeof(instr_str), "SWAP "); break;
            case OP_OVER: snprintf(instr_str, sizeof(instr_str), "OVER "); break;
            case OP_ROT: snprintf(instr_str, sizeof(instr_str), "ROT "); break;
            case OP_TO_R: snprintf(instr_str, sizeof(instr_str), ">R "); break;
            case OP_FROM_R: snprintf(instr_str, sizeof(instr_str), "R> "); break;
            case OP_R_FETCH: snprintf(instr_str, sizeof(instr_str), "R@ "); break;
            case OP_EQ: snprintf(instr_str, sizeof(instr_str), "= "); break;
            case OP_LT: snprintf(instr_str, sizeof(instr_str), "< "); break;
            case OP_GT: snprintf(instr_str, sizeof(instr_str), "> "); break;
            case OP_AND: snprintf(instr_str, sizeof(instr_str), "AND "); break;
            case OP_OR: snprintf(instr_str, sizeof(instr_str), "OR "); break;
            case OP_NOT: snprintf(instr_str, sizeof(instr_str), "NOT "); break;
            case OP_XOR: snprintf(instr_str, sizeof(instr_str), "XOR "); break;
            case OP_BIT_AND: snprintf(instr_str, sizeof(instr_str), "& "); break;
            case OP_BIT_OR: snprintf(instr_str, sizeof(instr_str), "| "); break;
            case OP_BIT_XOR: snprintf(instr_str, sizeof(instr_str), "^ "); break;
            case OP_BIT_NOT: snprintf(instr_str, sizeof(instr_str), "~ "); break;
            case OP_LSHIFT: snprintf(instr_str, sizeof(instr_str), "<< "); break;
            case OP_RSHIFT: snprintf(instr_str, sizeof(instr_str), ">> "); break;
            case OP_CR: snprintf(instr_str, sizeof(instr_str), "CR "); break;
            case OP_EMIT: snprintf(instr_str, sizeof(instr_str), "EMIT "); break;
            case OP_DOT: snprintf(instr_str, sizeof(instr_str), ". "); break;
            case OP_DOT_S: snprintf(instr_str, sizeof(instr_str), ".S "); break;
            case OP_FETCH: snprintf(instr_str, sizeof(instr_str), "@ "); break;
            case OP_STORE: snprintf(instr_str, sizeof(instr_str), "! "); break;
            case OP_PLUSSTORE: snprintf(instr_str, sizeof(instr_str), "+! "); break;
            case OP_I: snprintf(instr_str, sizeof(instr_str), "I "); break;
            case OP_J: snprintf(instr_str, sizeof(instr_str), "J "); break;
            case OP_DO:
                snprintf(instr_str, sizeof(instr_str), "DO ");
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_LOOP:
                snprintf(instr_str, sizeof(instr_str), "LOOP ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_PLUS_LOOP:
                snprintf(instr_str, sizeof(instr_str), "+LOOP ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_UNLOOP: snprintf(instr_str, sizeof(instr_str), "UNLOOP "); break;
            case OP_BRANCH:
                snprintf(instr_str, sizeof(instr_str), "BRANCH(%ld) ", instr.operand);
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_BRANCH_FALSE:
                snprintf(instr_str, sizeof(instr_str), "IF ");
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_CASE:
                snprintf(instr_str, sizeof(instr_str), "CASE ");
                branch_targets[branch_depth++] = i; // Marquer le début du CASE
                break;
            case OP_OF:
                snprintf(instr_str, sizeof(instr_str), "OF ");
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_ENDOF:
                if (branch_depth > 0 && i + 1 == branch_targets[branch_depth - 1]) {
                    snprintf(instr_str, sizeof(instr_str), "ENDOF ");
                    branch_depth--;
                } else {
                    snprintf(instr_str, sizeof(instr_str), "ENDOF(%ld) ", instr.operand);
                }
                break;
            case OP_ENDCASE:
                if (branch_depth > 0 && branch_targets[branch_depth - 1] <= i) {
                    snprintf(instr_str, sizeof(instr_str), "ENDCASE ");
                    branch_depth--;
                } else {
                    snprintf(instr_str, sizeof(instr_str), "ENDCASE ");
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
            case OP_DOT_QUOTE:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), ".\" %s \" ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), ".\"(invalid) ");
                }
                break;
            case OP_LITSTRING:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "\"%s\" ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "(LITSTRING %ld) ", instr.operand);
                }
                break;
            case OP_VARIABLE:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "VARIABLE %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "VARIABLE(invalid) ");
                }
                break;
            case OP_STRING:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "STRING %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "STRING(invalid) ");
                }
                break;
            case OP_QUOTE:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "\" %s \" ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "\"(invalid) ");
                }
                break;
            case OP_PRINT: snprintf(instr_str, sizeof(instr_str), "PRINT "); break;
            case OP_NUM_TO_BIN: snprintf(instr_str, sizeof(instr_str), "NUM-TO-BIN "); break;
            case OP_PRIME_TEST: snprintf(instr_str, sizeof(instr_str), "PRIME? "); break;
            case OP_BEGIN:
                snprintf(instr_str, sizeof(instr_str), "BEGIN ");
                branch_targets[branch_depth++] = i;
                break;
            case OP_WHILE:
                snprintf(instr_str, sizeof(instr_str), "WHILE ");
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_REPEAT:
                snprintf(instr_str, sizeof(instr_str), "REPEAT ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_UNTIL:
                snprintf(instr_str, sizeof(instr_str), "UNTIL ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_AGAIN:
                snprintf(instr_str, sizeof(instr_str), "AGAIN ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_EXIT: snprintf(instr_str, sizeof(instr_str), "EXIT "); break;
            case OP_WORDS: snprintf(instr_str, sizeof(instr_str), "WORDS "); break;
            case OP_FORGET:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "FORGET %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "FORGET(invalid) ");
                }
                break;
            case OP_LOAD:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "LOAD %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "LOAD(invalid) ");
                }
                break;
            case OP_ALLOT: snprintf(instr_str, sizeof(instr_str), "ALLOT "); break;
            case OP_CREATE:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "CREATE %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "CREATE(invalid) ");
                }
                break;
            case OP_RECURSE: snprintf(instr_str, sizeof(instr_str), "RECURSE "); break;
            case OP_IRC_SEND:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "IRC-SEND %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "IRC-SEND(invalid) ");
                }
                break;
            case OP_SQRT: snprintf(instr_str, sizeof(instr_str), "SQRT "); break;
            case OP_PICK: snprintf(instr_str, sizeof(instr_str), "PICK "); break;
            case OP_ROLL: snprintf(instr_str, sizeof(instr_str), "ROLL "); break;
            case OP_DEPTH: snprintf(instr_str, sizeof(instr_str), "DEPTH "); break;
            case OP_TOP: snprintf(instr_str, sizeof(instr_str), "TOP "); break;
            case OP_NIP: snprintf(instr_str, sizeof(instr_str), "NIP "); break;
            case OP_CLEAR_STACK: snprintf(instr_str, sizeof(instr_str), "CLEAR-STACK "); break;
            case OP_CLOCK: snprintf(instr_str, sizeof(instr_str), "CLOCK "); break;
            case OP_SEE:
                snprintf(instr_str, sizeof(instr_str), "SEE ");
                break;
            case OP_2DROP: snprintf(instr_str, sizeof(instr_str), "2DROP "); break;
            default:
                snprintf(instr_str, sizeof(instr_str), "(OP_%d %ld) ", instr.opcode, instr.operand);
                break;
        }

        // Vérifier si la chaîne dépasse la taille du buffer
        if (strlen(def_msg) + strlen(instr_str) >= sizeof(def_msg) - 1) {
            send_to_channel(def_msg); // Envoyer ce qu’on a déjà
            snprintf(def_msg, sizeof(def_msg), "%s ", instr_str); // Réinitialiser avec le nouvel élément
        } else {
            strncat(def_msg, instr_str, sizeof(def_msg) - strlen(def_msg) - 1);
        }
    }

    // Ajouter un point-virgule si nécessaire
    if (!has_semicolon) {
        strncat(def_msg, ";", sizeof(def_msg) - strlen(def_msg) - 1);
    }

    // Envoyer la définition complète
    send_to_channel(def_msg);
}
void initDictionary(Env *env) {
    addWord(&env->dictionary, ".S", OP_DOT_S, 0);
    addWord(&env->dictionary, ".", OP_DOT, 0);
    addWord(&env->dictionary, "+", OP_ADD, 0);
    addWord(&env->dictionary, "-", OP_SUB, 0);
    addWord(&env->dictionary, "*", OP_MUL, 0);
    addWord(&env->dictionary, "/", OP_DIV, 0);
    addWord(&env->dictionary, "MOD", OP_MOD, 0);
    addWord(&env->dictionary, "DUP", OP_DUP, 0);
    addWord(&env->dictionary, "DROP", OP_DROP, 0);
    addWord(&env->dictionary, "SWAP", OP_SWAP, 0);
    addWord(&env->dictionary, "OVER", OP_OVER, 0);
    addWord(&env->dictionary, "ROT", OP_ROT, 0);
    addWord(&env->dictionary, ">R", OP_TO_R, 0);
    addWord(&env->dictionary, "R>", OP_FROM_R, 0);
    addWord(&env->dictionary, "R@", OP_R_FETCH, 0);
    addWord(&env->dictionary, "=", OP_EQ, 0);
    addWord(&env->dictionary, "<", OP_LT, 0);
    addWord(&env->dictionary, ">", OP_GT, 0);
    addWord(&env->dictionary, "AND", OP_AND, 0);
    addWord(&env->dictionary, "OR", OP_OR, 0);
    addWord(&env->dictionary, "NOT", OP_NOT, 0);
    addWord(&env->dictionary, "XOR", OP_XOR, 0);
    addWord(&env->dictionary, "&", OP_BIT_AND, 0);
    addWord(&env->dictionary, "|", OP_BIT_OR, 0);
    addWord(&env->dictionary, "^", OP_BIT_XOR, 0);
    addWord(&env->dictionary, "~", OP_BIT_NOT, 0);
    addWord(&env->dictionary, "<<", OP_LSHIFT, 0);
    addWord(&env->dictionary, ">>", OP_RSHIFT, 0);
    addWord(&env->dictionary, "CR", OP_CR, 0);
    addWord(&env->dictionary, "EMIT", OP_EMIT, 0);
    addWord(&env->dictionary, "VARIABLE", OP_VARIABLE, 0);
    addWord(&env->dictionary, "@", OP_FETCH, 0);
    addWord(&env->dictionary, "!", OP_STORE, 0);
    addWord(&env->dictionary, "+!", OP_PLUSSTORE, 0);
    addWord(&env->dictionary, "DO", OP_DO, 0);
    addWord(&env->dictionary, "LOOP", OP_LOOP, 0);
    addWord(&env->dictionary, "I", OP_I, 0);
    addWord(&env->dictionary, "WORDS", OP_WORDS, 0);
    addWord(&env->dictionary, "LOAD", OP_LOAD, 0);
    addWord(&env->dictionary, "CREATE", OP_CREATE, 0);
    addWord(&env->dictionary, "ALLOT", OP_ALLOT, 0);
    addWord(&env->dictionary, ".\"", OP_DOT_QUOTE, 0);
    addWord(&env->dictionary, "CLOCK", OP_CLOCK, 0);
    addWord(&env->dictionary, "BEGIN", OP_BEGIN, 0);
    addWord(&env->dictionary, "WHILE", OP_WHILE, 0);
    addWord(&env->dictionary, "REPEAT", OP_REPEAT, 0);
    addWord(&env->dictionary, "AGAIN", OP_AGAIN, 0);
    addWord(&env->dictionary, "SQRT", OP_SQRT, 0);
    addWord(&env->dictionary, "UNLOOP", OP_UNLOOP, 0);
    addWord(&env->dictionary, "+LOOP", OP_PLUS_LOOP, 0);
    addWord(&env->dictionary, "PICK", OP_PICK, 0);
    addWord(&env->dictionary, "CLEAR-STACK", OP_CLEAR_STACK, 0);
    addWord(&env->dictionary, "PRINT", OP_PRINT, 0);
    addWord(&env->dictionary, "NUM-TO-BIN", OP_NUM_TO_BIN, 0);
    addWord(&env->dictionary, "PRIME?", OP_PRIME_TEST, 0);
    addWord(&env->dictionary, "FORGET", OP_FORGET, 1); // Immédiat
    addWord(&env->dictionary, "STRING", OP_STRING, 1); // Immédiat
    addWord(&env->dictionary, "\"", OP_QUOTE, 0);
    addWord(&env->dictionary, "2DROP", OP_2DROP, 0);
}
void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word, int word_index) {
    if (!currentenv || currentenv->error_flag) return;
    // printf( "Executing opcode %d at ip=%ld\n", instr.opcode, *ip);
    mpz_t *a = &mpz_pool[0], *b = &mpz_pool[1], *result = &mpz_pool[2];
    char temp_str[512];
unsigned long encoded_idx;
    unsigned long type;
    MemoryNode *node;
    switch (instr.opcode) {
 
case OP_PUSH:
    if (instr.operand < word->string_count && word->strings[instr.operand]) {
        if (mpz_set_str(*result, word->strings[instr.operand], 10) == 0) {
            push(stack, *result);
        } else {
            set_error("OP_PUSH: Invalid string number");
        }
    } else {
        mpz_set_ui(*result, instr.operand);
        push(stack, *result);
    }
    break;
    push(stack, *result);
    break;
        case OP_ADD:
            pop(stack, *b);
            pop(stack, *a);
            mpz_add(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_SUB:
            pop(stack, *b);
            pop(stack, *a);
            mpz_sub(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_MUL:
            pop(stack, *b);
            pop(stack, *a);
            mpz_mul(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_DIV:
            pop(stack, *b);
            pop(stack, *a);
            if (mpz_cmp_ui(*b, 0) == 0) {
                set_error("Division by zero");
                push(stack, *a);
                push(stack, *b);
            } else {
                mpz_div(*result, *a, *b);
                push(stack, *result);
            }
            break;
        case OP_MOD:
            pop(stack, *b);
            pop(stack, *a);
            if (mpz_cmp_ui(*b, 0) == 0) {
                set_error("Modulo by zero");
                push(stack, *a);
                push(stack, *b);
            } else {
                mpz_mod(*result, *a, *b);
                push(stack, *result);
            }
            break;
            
        case OP_DUP:
            if (stack->top >= 0) push(stack, stack->data[stack->top]);
            else set_error("DUP: Stack underflow");
            break;
        case OP_DROP:
            pop(stack, *a);
            break;
        case OP_SWAP:
            if (stack->top >= 1) {
                mpz_set(*a, stack->data[stack->top]);
                mpz_set(stack->data[stack->top], stack->data[stack->top - 1]);
                mpz_set(stack->data[stack->top - 1], *a);
            } else set_error("SWAP: Stack underflow");
            break;
        case OP_OVER:
            if (stack->top >= 1) push(stack, stack->data[stack->top - 1]);
            else set_error("OVER: Stack underflow");
            break;
        case OP_ROT:
            if (stack->top >= 2) {
                mpz_set(*a, stack->data[stack->top]);
                mpz_set(stack->data[stack->top], stack->data[stack->top - 2]);
                mpz_set(stack->data[stack->top - 2], stack->data[stack->top - 1]);
                mpz_set(stack->data[stack->top - 1], *a);
            } else set_error("ROT: Stack underflow");
            break;
        case OP_TO_R:
            pop(stack, *a);
            if (!currentenv->error_flag) {
                if (currentenv->return_stack.top < STACK_SIZE - 1) {
                    mpz_set(currentenv->return_stack.data[++currentenv->return_stack.top], *a);
                } else {
                    set_error(">R: Return stack overflow");
                    push(stack, *a);
                }
            }
            break;
        case OP_FROM_R:
            if (currentenv->return_stack.top >= 0) {
                mpz_set(*a, currentenv->return_stack.data[currentenv->return_stack.top--]);
                push(stack, *a);
            } else set_error("R>: Return stack underflow");
            break;
        case OP_R_FETCH:
            if (currentenv->return_stack.top >= 0) push(stack, currentenv->return_stack.data[currentenv->return_stack.top]);
            else set_error("R@: Return stack underflow");
            break;
        case OP_SEE:
       
    if (currentenv->compiling || word_index >= 0) { // Mode compilé ou dans une définition
        print_word_definition_irc(instr.operand, stack);
    } else { // Mode immédiat
        pop(stack, *a);
        if (!currentenv->error_flag && mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) < currentenv->dictionary.count) {
            print_word_definition_irc(mpz_get_si(*a), stack);
        } else if (!currentenv->error_flag) {
            set_error("SEE: Invalid word index");
        }
    }  
    break;
    case OP_2DROP:
    if (stack->top >= 1) {
        pop(stack, mpz_pool[0]); // Dépile le premier élément
        pop(stack, mpz_pool[0]); // Dépile le second élément
    } else {
        set_error("2DROP: Stack underflow");
    }
    break;
        case OP_EQ:
            pop(stack, *b);
            pop(stack, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) == 0);
            push(stack, *result);
            break;
        case OP_LT:
            pop(stack, *b);
            pop(stack, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) < 0);
            push(stack, *result);
            break;
        case OP_GT:
            pop(stack, *b);
            pop(stack, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) > 0);
            push(stack, *result);
            break;
case OP_AND:
    pop(stack, *b);
    pop(stack, *a);
    mpz_set_si(*result, (mpz_cmp_ui(*a, 0) != 0) && (mpz_cmp_ui(*b, 0) != 0));
    push(stack, *result);
    break;
case OP_OR:
    pop(stack, *b);
    pop(stack, *a);
    mpz_set_si(*result, (mpz_cmp_ui(*a, 0) != 0) || (mpz_cmp_ui(*b, 0) != 0));
    push(stack, *result);
    break;
        case OP_NOT:
            pop(stack, *a);
            mpz_set_si(*result, mpz_cmp_ui(*a, 0) == 0);
            push(stack, *result);
            break;
        case OP_XOR:
        pop(stack, *b);
        pop(stack, *a);
        int a_true = (mpz_cmp_ui(*a, 0) != 0);
        int b_true = (mpz_cmp_ui(*b, 0) != 0);
        mpz_set_si(*result, (a_true != b_true)); // Vrai si exactement une des deux est vraie
        push(stack, *result);
        break;
case OP_CALL:
    if (instr.operand >= 0 && instr.operand < currentenv->dictionary.count) {
        executeCompiledWord(&currentenv->dictionary.words[instr.operand], stack, instr.operand);
    } else {
        set_error("Invalid word index");
    }
    break;
        case OP_BRANCH:
            *ip = instr.operand - 1;
            break;
        case OP_BRANCH_FALSE:
            pop(stack, *a);
            if (mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1;
            break;
        case OP_END:
            *ip = word->code_length;
            break;
        case OP_DOT:
            pop(stack, *a);
            mpz_get_str(temp_str, 10, *a);
            send_to_channel(temp_str);
            break;
 
        case OP_DOT_S:
            if (stack->top >= 0) {
                char stack_str[512] = "<";
                char num_str[128];
                snprintf(num_str, sizeof(num_str), "%ld", stack->top + 1);
                strcat(stack_str, num_str);
                strcat(stack_str, "> ");
                for (int i = 0; i <= stack->top; i++) {
                    mpz_get_str(num_str, 10, stack->data[i]);
                    strcat(stack_str, num_str);
                    if (i < stack->top) strcat(stack_str, " ");
                }
                send_to_channel(stack_str);
            } else send_to_channel("<0>");
            break;
case OP_EMIT:
    if (stack->top >= 0) {
        pop(stack, *a);
        char c = (char)mpz_get_si(*a);
        if (currentenv->buffer_pos < BUFFER_SIZE - 1) {
            currentenv->output_buffer[currentenv->buffer_pos++] = c;
            currentenv->output_buffer[currentenv->buffer_pos] = '\0';  // Terminer la chaîne
        } else {
            set_error("Output buffer overflow");
        }
    } else {
        set_error("EMIT: Stack underflow");
    }
    break;

case OP_CR:
    if (currentenv->buffer_pos < BUFFER_SIZE - 1) {
        currentenv->output_buffer[currentenv->buffer_pos++] = '\n';
        currentenv->output_buffer[currentenv->buffer_pos] = '\0';
    }
    if (currentenv->buffer_pos > 0) {
        send_to_channel(currentenv->output_buffer);  // Envoyer au canal IRC
        currentenv->buffer_pos = 0;  // Réinitialiser le buffer
        memset(currentenv->output_buffer, 0, BUFFER_SIZE);
    }
    break;
case OP_VARIABLE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        if (findCompiledWordIndex(name) >= 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "VARIABLE: '%s' already defined", name);
            set_error(msg);
        } else {
            unsigned long index = memory_create(&currentenv->memory_list, name, TYPE_VAR);
            if (index == 0) {
                set_error("VARIABLE: Memory creation failed");
            } else if (currentenv->dictionary.count < currentenv->dictionary.capacity) {
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
            } else {
                resizeDynamicDictionary(&currentenv->dictionary);
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
            }
        }
    } else {
        set_error("Invalid variable name");
    }
    break;
case OP_STORE:
    if (stack->top < 0) {
        set_error("STORE: Stack underflow for address");
        break;
    }
    char debug_msg[512];
    /*  snprintf(debug_msg, sizeof(debug_msg), "STORE: Starting, stack_top=%d", stack->top);
    send_to_channel(debug_msg);
 */
    pop(stack, *result); // encoded_idx
    encoded_idx = mpz_get_ui(*result);
   /*   snprintf(debug_msg, sizeof(debug_msg), "STORE: Popped encoded_idx=%lu, stack_top=%d", encoded_idx, stack->top);
    send_to_channel(debug_msg);
 */
    node = memory_get(&currentenv->memory_list, encoded_idx);
    if (!node) {
        snprintf(debug_msg, sizeof(debug_msg), "STORE: Invalid memory index for encoded_idx=%lu", encoded_idx);
        set_error(debug_msg);
        push(stack, *result);
        break;
    }
    type = node->type;
     /* snprintf(debug_msg, sizeof(debug_msg), "STORE: Found node %s with type=%lu, stack_top=%d", node->name, type, stack->top);
    send_to_channel(debug_msg);
 */
    if (type == TYPE_VAR) {
        if (stack->top < 0) {
            set_error("STORE: Stack underflow for variable value");
            push(stack, *result);
            break;
        }
        pop(stack, *a);
        memory_store(&currentenv->memory_list, encoded_idx, a);
        /*  snprintf(debug_msg, sizeof(debug_msg), "STORE: Set %s = %s", node->name, mpz_get_str(NULL, 10, *a));
        send_to_channel(debug_msg);
        */
         
    } else if (type == TYPE_ARRAY) {
        if (stack->top < 1) {
            set_error("STORE: Stack underflow for array operation");
            push(stack, *result);
            break;
        }
        pop(stack, *a); // offset
        pop(stack, *b); // valeur
        int offset = mpz_get_si(*a);
         /*  snprintf(debug_msg, sizeof(debug_msg), "STORE: Array operation on %s, offset=%d, value=%s", node->name, offset, mpz_get_str(NULL, 10, *b));
        send_to_channel(debug_msg);
        */
        if (offset >= 0 && offset < node->value.array.size) {
            mpz_set(node->value.array.data[offset], *b);
            /* snprintf(debug_msg, sizeof(debug_msg), "STORE: Set %s[%d] = %s", node->name, offset, mpz_get_str(NULL, 10, *b));
            send_to_channel(debug_msg);
            */ 
        } else {
            snprintf(debug_msg, sizeof(debug_msg), "STORE: Array index %d out of bounds (size=%lu)", offset, node->value.array.size);
            set_error(debug_msg);
            push(stack, *b);
            push(stack, *a);
            push(stack, *result);
        }
    } else if (type == TYPE_STRING) {
        if (stack->top < 0) {
            set_error("STORE: Stack underflow for string value");
            push(stack, *result);
            break;
        }
        pop(stack, *a); // index dans string_stack
        int str_idx = mpz_get_si(*a);
        if (mpz_fits_slong_p(*a) && str_idx >= 0 && str_idx <= currentenv->string_stack_top) {
            char *str = currentenv->string_stack[str_idx];
            if (str) {
                memory_store(&currentenv->memory_list, encoded_idx, str);
                for (int i = str_idx; i < currentenv->string_stack_top; i++) {
                    currentenv->string_stack[i] = currentenv->string_stack[i + 1];
                }
                currentenv->string_stack[currentenv->string_stack_top--] = NULL;
                /* snprintf(debug_msg, sizeof(debug_msg), "STORE: Set %s = %s", node->name, str);
                send_to_channel(debug_msg);
                */ 
            } else {
                set_error("STORE: No string at stack index");
                push(stack, *a);
                push(stack, *result);
            }
        } else {
            set_error("STORE: Invalid string stack index");
            push(stack, *a);
            push(stack, *result);
        }
    } else {
        set_error("STORE: Unknown type");
        push(stack, *result);
    }
    break;
 
case OP_FETCH:
    if (stack->top < 0) {
        set_error("FETCH: Stack underflow for address");
        break;
    }
    pop(stack, *result); // encoded_idx (ex. ZOZO = 268435456)
    encoded_idx = mpz_get_ui(*result);
    // char debug_msg[512];
    /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Popped encoded_idx=%lu", encoded_idx);
    send_to_channel(debug_msg);
*/
    node = memory_get(&currentenv->memory_list, encoded_idx);
    if (!node) {
        set_error("FETCH: Invalid memory index");
        push(stack, *result);
        break;
    }
    type = node->type; // Type réel du nœud
    /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Found %s, type=%lu, stack_top=%d", node->name, type, stack->top);
    send_to_channel(debug_msg);
*/
    if (type == TYPE_VAR) {
        memory_fetch(&currentenv->memory_list, encoded_idx, result);
        /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Got variable %s = %s", node->name, mpz_get_str(NULL, 10, *result));
        send_to_channel(debug_msg);
        */
        push(stack, *result);
    } else if (type == TYPE_ARRAY) {
        if (stack->top < 0) {
            set_error("FETCH: Stack underflow for array offset");
            push(stack, *result);
            break;
        }
        pop(stack, *a); // offset (ex. 5)
        int offset = mpz_get_si(*a);
        /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Array offset=%d", offset);
        send_to_channel(debug_msg);
        */
        if (offset >= 0 && offset < node->value.array.size) {
            mpz_set(*result, node->value.array.data[offset]);
            /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Got %s[%d] = %s", node->name, offset, mpz_get_str(NULL, 10, *result));
            send_to_channel(debug_msg);
            */ 
            push(stack, *result);
        } else {
            snprintf(debug_msg, sizeof(debug_msg), "FETCH: Array index %d out of bounds (size=%lu)", offset, node->value.array.size);
            set_error(debug_msg);
            push(stack, *a);
            push(stack, *result);
        }
    } else if (type == TYPE_STRING) {
        char *str;
        memory_fetch(&currentenv->memory_list, encoded_idx, &str);
        if (str) {
            push_string(strdup(str));
            free(str);
            mpz_set_si(*result, currentenv->string_stack_top);
            push(stack, *result);
        } else {
            push_string(NULL);
            mpz_set_si(*result, currentenv->string_stack_top);
            push(stack, *result);
        }
    } else {
        set_error("FETCH: Unknown type");
        push(stack, *result);
    }
    break;
    
 
 case OP_ALLOT:
            if (stack->top < 0) {
                set_error("ALLOT: Stack underflow");
                break;
            }
            pop(stack, *a); // Taille
            if (stack->top < 0) {
                set_error("ALLOT: Stack underflow for index");
                push(stack, *a);
                break;
            }
            pop(stack, *result); // encoded_idx
            encoded_idx = mpz_get_ui(*result);
            node = memory_get(&currentenv->memory_list, encoded_idx);
            if (!node) {
                set_error("ALLOT: Invalid memory index");
                push(stack, *result);
                push(stack, *a);
                break;
            }
            if (node->type != TYPE_VAR) { // Vérifier node->type, pas memory_get_type
                set_error("ALLOT: Can only allot on a variable");
                push(stack, *result);
                push(stack, *a);
                break;
            }
            int size = mpz_get_si(*a);
            if (size <= 0) {
                set_error("ALLOT: Size must be positive");
                push(stack, *result);
                push(stack, *a);
                break;
            }
            mpz_t *array = (mpz_t *)malloc(size * sizeof(mpz_t));
            if (!array) {
                set_error("ALLOT: Memory allocation failed");
                push(stack, *result);
                push(stack, *a);
                break;
            }
            for (int i = 0; i < size; i++) {
                mpz_init_set_ui(array[i], 0);
            }
            if (node->type == TYPE_VAR) {
                mpz_clear(node->value.number);
            }
            node->type = TYPE_ARRAY;
            node->value.array.data = array;
            node->value.array.size = size;
            // char debug_msg[512];
            // snprintf(debug_msg, sizeof(debug_msg), "ALLOT: Created array %s with size %d at encoded_idx=%lu", node->name, size, encoded_idx);
            // send_to_channel(debug_msg);
            break;
           case OP_IRC_SEND:
            pop(stack, *a);
            if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
                send_to_channel(word->strings[instr.operand]);
            } else set_error("Invalid IRC send string");
            break;
case OP_DO:
    if (stack->top < 1) {
        set_error("DO: Stack underflow");
        break;
    }
    pop(stack, *b);  // Start (index initial)
    pop(stack, *a);  // Limit
    if (!currentenv->error_flag && currentenv->return_stack.top < STACK_SIZE - 3) {
        currentenv->return_stack.top += 3;
        mpz_set(currentenv->return_stack.data[currentenv->return_stack.top - 2], *a);  // limit
        mpz_set(currentenv->return_stack.data[currentenv->return_stack.top - 1], *b);  // index
        mpz_set_si(currentenv->return_stack.data[currentenv->return_stack.top], *ip + 1);  // addr
        // Pas besoin de toucher control_stack ici
    } else if (!currentenv->error_flag) {
        set_error("DO: Return stack overflow");
        push(stack, *a);
        push(stack, *b);
    }
    break;

case OP_LOOP:
    if (currentenv->return_stack.top >= 2) {
        mpz_add_ui(currentenv->return_stack.data[currentenv->return_stack.top - 1],
                   currentenv->return_stack.data[currentenv->return_stack.top - 1], 1);  // Incrémente index
        if (mpz_cmp(currentenv->return_stack.data[currentenv->return_stack.top - 1],
                    currentenv->return_stack.data[currentenv->return_stack.top - 2]) < 0) {
            *ip = mpz_get_si(currentenv->return_stack.data[currentenv->return_stack.top]) - 1;  // Retour à addr
        } else {
            currentenv->return_stack.top -= 3;  // Dépile limit, index, addr
        }
    } else {
        set_error("LOOP without DO");
    }
    break;

case OP_I:
    if (currentenv->return_stack.top >= 1) {
        char debug_msg[512];
        // snprintf(debug_msg, sizeof(debug_msg), "I: Current index=%s", mpz_get_str(NULL, 10, currentenv->return_stack.data[currentenv->return_stack.top - 1]));
        // send_to_channel(debug_msg);
         push(stack, currentenv->return_stack.data[currentenv->return_stack.top - 1]);  // Commenté pour ne pas pousser
    } else {
        set_error("I outside loop");
    }
    break;;

case OP_J:
    if (currentenv->return_stack.top >= 1) {
        if (currentenv->return_stack.top >= 4) {
            // Boucle imbriquée : lit l’indice de la boucle externe
            push(stack, currentenv->return_stack.data[currentenv->return_stack.top - 4]);
        } else {
            // Boucle externe seule : lit l’indice courant
            push(stack, currentenv->return_stack.data[currentenv->return_stack.top - 1]);
        }
    } else {
        set_error("J: No outer loop");
    }
    break;
        case OP_UNLOOP:
    if (currentenv->return_stack.top >= 2) {
        currentenv->return_stack.top -= 3;  // Dépile limit, index, addr
    } else {
        set_error("UNLOOP without DO");
    }
    break;
        case OP_PLUS_LOOP:
    if (currentenv->return_stack.top >= 2) {
        pop(stack, *a);  // Incrément
        if (!currentenv->error_flag) {
            mpz_add(currentenv->return_stack.data[currentenv->return_stack.top - 1], currentenv->return_stack.data[currentenv->return_stack.top - 1], *a);
            if (mpz_cmp(currentenv->return_stack.data[currentenv->return_stack.top - 1], currentenv->return_stack.data[currentenv->return_stack.top - 2]) < 0) {
                *ip = mpz_get_si(currentenv->return_stack.data[currentenv->return_stack.top]) - 1;
            } else {
                currentenv->return_stack.top -= 3;
            }
        }
    } else {
        set_error("+LOOP without DO");
    }
    break;
        case OP_SQRT:
            pop(stack, *a);
            if (mpz_cmp_ui(*a, 0) < 0) {
                set_error("Square root of negative number");
                push(stack, *a);
            } else {
                mpz_sqrt(*result, *a);
                push(stack, *result);
            }
        break ; 
        /* 
case OP_DOT_QUOTE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        send_to_channel(word->strings[instr.operand]);
    } else {
        set_error("Invalid string index for .\"");
    }
    break;
    */ 
    case OP_DOT_QUOTE:
    if (instr.operand < word->string_count && word->strings[instr.operand]) {
        size_t len = strlen(word->strings[instr.operand]);
        if (currentenv->buffer_pos + len < BUFFER_SIZE - 1) {
            strncpy(currentenv->output_buffer + currentenv->buffer_pos, word->strings[instr.operand], len);
            currentenv->buffer_pos += len;
            currentenv->output_buffer[currentenv->buffer_pos] = '\0';
        } else {
            set_error("DOT_QUOTE: Output buffer overflow");
        }
    } else {
        set_error("DOT_QUOTE: Invalid string index");
    }
    break;
case OP_LITSTRING:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        if (currentenv->buffer_pos + strlen(word->strings[instr.operand]) < BUFFER_SIZE) {
            strcpy(currentenv->output_buffer + currentenv->buffer_pos, word->strings[instr.operand]);
            currentenv->buffer_pos += strlen(word->strings[instr.operand]);
            currentenv->output_buffer[currentenv->buffer_pos] = '\0';
        } else {
            set_error("Output buffer overflow in LITSTRING");
        }
    } else {
        set_error("Invalid string in LITSTRING");
    }
    break;
        case OP_CASE:
            if (currentenv->control_stack_top < CONTROL_STACK_SIZE) {
                currentenv->control_stack[currentenv->control_stack_top].type = CT_CASE;
                currentenv->control_stack[currentenv->control_stack_top].addr = *ip;
                currentenv->control_stack_top++;
            } else set_error("Control stack overflow");
            break;
        case OP_OF:
            pop(stack, *a);
            pop(stack, *b);
            if (mpz_cmp(*a, *b) != 0) {
                *ip = instr.operand - 1; // Sauter à ENDOF
                push(stack, *b); // Remettre la valeur de test
            } else {
                push(stack, *b); // Remettre la valeur de test
            }
            break;
        case OP_ENDOF:
            *ip = instr.operand - 1; // Sauter à ENDCASE ou prochain ENDOF
            break;
        case OP_ENDCASE:
            pop(stack, *a); // Dépiler la valeur de test
            if (currentenv->control_stack_top > 0 && currentenv->control_stack[currentenv->control_stack_top - 1].type == CT_CASE) {
                currentenv->control_stack_top--;
            } else set_error("ENDCASE without CASE");
            break;
        case OP_EXIT:
            *ip = word->code_length;
            break;
case OP_BEGIN:
    if (currentenv->control_stack_top < CONTROL_STACK_SIZE) {
        currentenv->control_stack[currentenv->control_stack_top++] = (ControlEntry){CT_BEGIN, *ip};
    } else set_error("Control stack overflow");
    break;
case OP_WHILE:
    pop(stack, *a);
    if (!currentenv->error_flag && mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1; // Sauter si faux
    break;
case OP_REPEAT:
    *ip = instr.operand - 1; // Toujours sauter à BEGIN, pas besoin de vérifier control_stack ici
    break;
        case OP_UNTIL:
            pop(stack, *a);
            if (mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1;
            break;
        case OP_AGAIN:
            *ip = instr.operand - 1;
            break;
        case OP_BIT_AND:
            pop(stack, *b);
            pop(stack, *a);
            mpz_and(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_BIT_OR:
            pop(stack, *b);
            pop(stack, *a);
            mpz_ior(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_BIT_XOR:
            pop(stack, *b);
            pop(stack, *a);
            mpz_xor(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_BIT_NOT:
            pop(stack, *a);
            mpz_com(*result, *a);
            push(stack, *result);
            break;
        case OP_LSHIFT:
            pop(stack, *b);
            pop(stack, *a);
            mpz_mul_2exp(*result, *a, mpz_get_ui(*b));
            push(stack, *result);
            break;
        case OP_RSHIFT:
            pop(stack, *b);
            pop(stack, *a);
            mpz_fdiv_q_2exp(*result, *a, mpz_get_ui(*b));
            push(stack, *result);
            break;
case OP_FORGET:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *word_to_forget = word->strings[instr.operand];
        int forget_idx = findCompiledWordIndex(word_to_forget);
        if (forget_idx >= 0) {
            for (int i = forget_idx; i < currentenv->dictionary.count; i++) {
                CompiledWord *dict_word = &currentenv->dictionary.words[i];
                if (dict_word->code_length == 1 && dict_word->code[0].opcode == OP_PUSH) {
                    unsigned long encoded_idx = dict_word->code[0].operand;
                    unsigned long type = memory_get_type(encoded_idx);
                    if (type == TYPE_VAR || type == TYPE_STRING || type == TYPE_ARRAY) {
                        memory_free(&currentenv->memory_list, dict_word->name);
                    }
                }
                if (dict_word->name) {
                    free(dict_word->name);
                    dict_word->name = NULL;
                }
                for (int j = 0; j < dict_word->string_count; j++) {
                    if (dict_word->strings[j]) {
                        free(dict_word->strings[j]);
                        dict_word->strings[j] = NULL;
                    }
                }
                dict_word->code_length = 0;
                dict_word->string_count = 0;
            }
            long int old_dict_count = currentenv->dictionary.count;
            currentenv->dictionary.count = forget_idx;
            char msg[512];
            snprintf(msg, sizeof(msg), "Forgot everything from '%s' at index %d (dict was %ld, now %ld; mem count now %lu)", 
                     word_to_forget, forget_idx, old_dict_count, currentenv->dictionary.count, currentenv->memory_list.count);
            send_to_channel(msg);
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "FORGET: Unknown word: %s", word_to_forget);
            set_error(msg);
        }
    } else {
        set_error("FORGET: Invalid word name");
    }
    break;
case OP_WORDS:
    if (currentenv->dictionary.count > 0) {
        char words_msg[2048] = "";
        size_t remaining = sizeof(words_msg) - 1;
        for (int i = 0; i < currentenv->dictionary.count && remaining > 1; i++) {
            if (currentenv->dictionary.words[i].name) {
                size_t name_len = strlen(currentenv->dictionary.words[i].name);
                if (name_len + 1 < remaining) {
                    strncat(words_msg, currentenv->dictionary.words[i].name, remaining);
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
    
case OP_LOAD: {
    char *filename = NULL;
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        filename = strdup(word->strings[instr.operand]);
    } else {
        set_error("LOAD: No filename provided");
        break;
    }

    FILE *file = fopen(filename, "r");
    if (file) {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), file)) {
            buffer[strcspn(buffer, "\n")] = '\0';
            interpret(buffer, stack); // Utiliser la pile passée (stack, pas forcément main_stack)
        }
        fclose(file);
    } else {
        snprintf(temp_str, sizeof(temp_str), "LOAD: Cannot open file '%s'", filename);
        set_error(temp_str);
    }
    free(filename);
    break;
}
 
        case OP_PICK:
            pop(stack, *a);
            int n = mpz_get_si(*a);
            if (stack->top >= n) push(stack, stack->data[stack->top - n]);
            else set_error("PICK: Stack underflow");
            break;
        case OP_ROLL:
            pop(stack, *a);
            int zozo = mpz_get_si(*a);
            if (stack->top >= zozo) {
                mpz_t temp[zozo + 1];
                for (int i = 0; i <= zozo; i++) {
                    mpz_init(temp[i]);
                    pop(stack, temp[i]);
                }
                for (int i = zozo - 1; i >= 0; i--) push(stack, temp[i]);
                mpz_clear(temp[zozo]);
                for (int i = 0; i < zozo; i++) mpz_clear(temp[i]);
            } else set_error("ROLL: Stack underflow");
            break;
case OP_PLUSSTORE:
    if (stack->top < 1) {
        set_error("+!: Stack underflow");
        break;
    }
    pop(stack, *a); // encoded_idx (ex. 268435456 pour ZOZO)
    if (stack->top < 0) {
        set_error("+!: Stack underflow for value");
        push(stack, *a);
        break;
    }
    pop(stack, *b); // offset ou valeur (selon type)
    unsigned long encoded_idx = mpz_get_ui(*a);
    // char debug_msg[512];
    /* snprintf(debug_msg, sizeof(debug_msg), "+!: encoded_idx=%lu", encoded_idx);
    send_to_channel(debug_msg);
*/
    MemoryNode *node = memory_get(&currentenv->memory_list, encoded_idx);
    if (!node) {
        snprintf(debug_msg, sizeof(debug_msg), "+!: Invalid memory index %lu", encoded_idx);
        set_error(debug_msg);
        push(stack, *b);
        push(stack, *a);
        break;
    }

    if (node->type == TYPE_VAR) {
        // Cas variable : *b est la valeur à ajouter
        mpz_add(node->value.number, node->value.number, *b);
        /* snprintf(debug_msg, sizeof(debug_msg), "+!: Added %s to %s, now %s", 
                 mpz_get_str(NULL, 10, *b), node->name, mpz_get_str(NULL, 10, node->value.number));
        send_to_channel(debug_msg);
        */ 
    } else if (node->type == TYPE_ARRAY) {
        // Cas tableau : vérifier s'il y a un offset sur la pile
        if (node->value.array.size == 0) {
        set_error("tableau vide");
        push(stack, *a);
        push(stack, *b);
        break;
    }
        if (stack->top < 0) {
            // Pas d'offset : *b est la valeur, ajout à l'index 0
            if (node->value.array.size > 0) {
                mpz_add(node->value.array.data[0], node->value.array.data[0], *b);
                snprintf(debug_msg, sizeof(debug_msg), "+!: Added %s to %s[0], now %s", 
                         mpz_get_str(NULL, 10, *b), node->name, mpz_get_str(NULL, 10, node->value.array.data[0]));
                send_to_channel(debug_msg);
            } else {
                set_error("+!: Array is empty");
                push(stack, *b);
                push(stack, *a);
            }
        } else {
            // Offset présent : dépiler la valeur, *b est l'offset
            pop(stack, *result); // valeur à ajouter
            unsigned long offset = mpz_get_ui(*b); // *b est l'offset
            if (offset < node->value.array.size) {
                mpz_add(node->value.array.data[offset], node->value.array.data[offset], *result);
                /*snprintf(debug_msg, sizeof(debug_msg), "+!: Added %s to %s[%lu], now %s", 
                         mpz_get_str(NULL, 10, *result), node->name, offset, 
                         mpz_get_str(NULL, 10, node->value.array.data[offset]));
                send_to_channel(debug_msg);
                */ 
            } else {
                snprintf(debug_msg, sizeof(debug_msg), "+!: Offset %lu out of bounds (size=%lu)", 
                         offset, node->value.array.size);
                set_error(debug_msg);
                push(stack, *result); // Remettre valeur
                push(stack, *b);      // Remettre offset
                push(stack, *a);      // Remettre encoded_idx
            }
        }
    } else {
        set_error("+!: Not a variable or array");
        push(stack, *b);
        push(stack, *a);
    }
    break;
        case OP_DEPTH:
            mpz_set_si(*result, stack->top + 1);
            push(stack, *result);
            break;
        case OP_TOP:
            if (stack->top >= 0) push(stack, stack->data[stack->top]);
            else set_error("TOP: Stack underflow");
            break;
        case OP_NIP:
            if (stack->top >= 1) {
                pop(stack, *a);
                pop(stack, *b);
                push(stack, *a);
            } else set_error("NIP: Stack underflow");
            break;

case OP_CREATE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        if (findCompiledWordIndex(name) >= 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "CREATE: '%s' already defined", name);
            set_error(msg);
        } else {
            unsigned long index = memory_create(&currentenv->memory_list, name, TYPE_ARRAY);
            if (index == 0) {
                set_error("CREATE: Memory creation failed");
            } else if (currentenv->dictionary.count < currentenv->dictionary.capacity) {
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
                MemoryNode *node = memory_get(&currentenv->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)malloc(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            } else {
                resizeDynamicDictionary(&currentenv->dictionary);
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
                MemoryNode *node = memory_get(&currentenv->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)malloc(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            }
        }
    } else {
        set_error("CREATE: Invalid name");
    }
    break;
case OP_STRING:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        if (findCompiledWordIndex(name) >= 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "STRING: '%s' already defined", name);
            set_error(msg);
        } else {
            unsigned long index = memory_create(&currentenv->memory_list, name, TYPE_STRING);
            if (index == 0) {
                set_error("STRING: Memory creation failed");
            } else if (currentenv->dictionary.count < currentenv->dictionary.capacity) {
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
            } else {
                resizeDynamicDictionary(&currentenv->dictionary);
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
            }
        }
    } else {
        set_error("STRING: Invalid name");
    }
    break;
case OP_QUOTE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *str = word->strings[instr.operand]; // Pas besoin de dupliquer ici, déjà alloué
        push_string(str); // Pousse sur string_stack
        mpz_set_si(*result, currentenv->string_stack_top); // Index sur la pile principale
        push(stack, *result);
    } else {
        set_error("QUOTE: Invalid string index");
    }
    break;
    /* Ancienne version 
case OP_PRINT:
    pop(stack, *a);
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= currentenv->string_stack_top) {
        char *str = currentenv->string_stack[mpz_get_si(*a)];
        if (str) {
            send_to_channel(str);
        } else {
            set_error("PRINT: No string at index");
        }
    } else {
        set_error("PRINT: Invalid string stack index");
    }
    break;
    */ 
    case OP_PRINT:
    pop(stack, *a);
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= currentenv->string_stack_top) {
        char *str = currentenv->string_stack[mpz_get_si(*a)];
        if (str) {
            size_t len = strlen(str);
            if (currentenv->buffer_pos + len < BUFFER_SIZE - 1) {
                strncpy(currentenv->output_buffer + currentenv->buffer_pos, str, len);
                currentenv->buffer_pos += len;
                currentenv->output_buffer[currentenv->buffer_pos] = '\0';
            } else {
                set_error("PRINT: Output buffer overflow");
            }
        } else {
            set_error("PRINT: No string at index");
        }
    } else {
        set_error("PRINT: Invalid string stack index");
    }
    break;
    case OP_NUM_TO_BIN:
        pop(stack, *a);
        char *bin_str = mpz_get_str(NULL, 2, *a); // Base 2 = binaire
        send_to_channel(bin_str);
        free(bin_str); // Libérer la mémoire allouée par mpz_get_str
        break;
	case OP_PRIME_TEST:
        pop(stack, *a);
        int is_prime = mpz_probab_prime_p(*a, 25); // 25 itérations de Miller-Rabin
        mpz_set_si(*result, is_prime != 0); // 1 si premier ou probablement premier, 0 sinon
        push(stack, *result);
        break;
		case OP_CLEAR_STACK:
            stack->top = -1;  // Vide la pile en réinitialisant le sommet
            break;
        case OP_CLOCK:
            mpz_set_si(*result, (long int)time(NULL));
            push(stack, *result);
            break;

        default:
            set_error("Unknown opcode");
            break;
    }
}
void interpret(char *input, Stack *stack) {
    if (!currentenv) return;
    currentenv->error_flag = 0;
    currentenv->compile_error = 0;
    char *saveptr;
    char *token = strtok_r(input, " \t\n", &saveptr);
    while (token && !currentenv->error_flag && !currentenv->compile_error) {
        if (strcmp(token, "(") == 0) {
            char *end = strstr(saveptr, ")");
            if (end) saveptr = end + 1;
            else saveptr = NULL;
            token = strtok_r(NULL, " \t\n", &saveptr);
            continue;
        }
        compileToken(token, &saveptr, currentenv);
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
}
void compileToken(char *token, char **input_rest, Env *env) {
    Instruction instr = {0};
    if (!env || env->compile_error) return;

    // Vérification des mots immédiats (exécutés dans tous les modes)
    int idx = findCompiledWordIndex(token);
    if (idx >= 0 && env->dictionary.words[idx].immediate) {
        if (strcmp(token, "STRING") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("STRING requires a name");
                env->compile_error = 1;
                return;
            }
            if (findCompiledWordIndex(next_token) >= 0) {
                char msg[512];
                snprintf(msg, sizeof(msg), "STRING: '%s' already defined", next_token);
                set_error(msg);
                env->compile_error = 1;
                return;
            }
            // Crée une variable STRING dans MemoryList
            unsigned long index = memory_create(&env->memory_list, next_token, TYPE_STRING);
            if (index == 0) {
                set_error("STRING: Memory creation failed");
                env->compile_error = 1;
                return;
            }
            // Ajoute au dictionnaire dynamique
            if (env->dictionary.count >= env->dictionary.capacity) {
                resizeDynamicDictionary(&env->dictionary);
            }
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name = strdup(next_token);
            env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = index;
            env->dictionary.words[dict_idx].code_length = 1;
            env->dictionary.words[dict_idx].string_count = 0;
            env->dictionary.words[dict_idx].immediate = 0; // Pas immédiat après création
            // Si en mode compilation, ajoute l’instruction
            if (env->compiling) {
                instr.opcode = OP_STRING;
                instr.operand = env->currentWord.string_count;
                env->currentWord.strings[env->currentWord.string_count++] = strdup(next_token);
                env->currentWord.code[env->currentWord.code_length++] = instr;
            }
        } else if (strcmp(token, "FORGET") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("FORGET requires a word name");
                env->compile_error = 1;
                return;
            }
            CompiledWord temp_word = {0};
            temp_word.strings[0] = strdup(next_token);
            temp_word.string_count = 1;
            instr.opcode = OP_FORGET;
            instr.operand = 0; // Index 0 dans temp_word.strings
            executeInstruction(instr, &env->main_stack, NULL, &temp_word, -1);
            free(temp_word.strings[0]);
            return;
        }
        return; // Sortir après exécution immédiate
    }

    // Début d’une définition
if (strcmp(token, ":") == 0) {
    char *next_token = strtok_r(NULL, " \t\n", input_rest);
    if (!next_token) {
        set_error("No name for definition");
        env->compile_error = 1;
        return;
    }
    if (findCompiledWordIndex(next_token) >= 0) {
        set_error("Word already defined");
        env->compile_error = 1;
        return;
    }
    env->compiling = 1;
    env->currentWord.name = strdup(next_token); // Déjà strdup ici
    env->currentWord.code_length = 0;
    env->currentWord.string_count = 0;
    env->control_stack_top = 0;
    env->current_word_index = env->dictionary.count;
    if (env->dictionary.count >= env->dictionary.capacity) {
        resizeDynamicDictionary(&env->dictionary);
    }
    return;
}
    // Fin d’une définition
if (strcmp(token, ";") == 0) {
    if (!env->compiling) {
        set_error("Extra ;");
        env->compile_error = 1;
        return;
    }
    if (env->control_stack_top > 0) {
        set_error("Unmatched control structures");
        env->compile_error = 1;
        env->control_stack_top = 0;
        env->compiling = 0;
        return;
    }
    instr.opcode = OP_END;
    env->currentWord.code[env->currentWord.code_length++] = instr;
    if (env->current_word_index >= 0 && env->current_word_index < env->dictionary.capacity) {
        env->dictionary.words[env->current_word_index] = env->currentWord;
        if (env->current_word_index == env->dictionary.count) {
            env->dictionary.count++;
        }
    } else {
        set_error("Dictionary index out of bounds");
        env->compile_error = 1;
    }
    env->compiling = 0;
    env->compile_error = 0;
    env->control_stack_top = 0;
    // NE PAS libérer env->currentWord.name ici, il appartient au dictionnaire maintenant
    env->currentWord.name = NULL; // Juste réinitialiser pour éviter une double libération
    env->currentWord.code_length = 0;
    env->currentWord.string_count = 0;
    return;
}
    // Récursion dans une définition
    if (strcmp(token, "RECURSE") == 0) {
        if (!env->compiling) {
            set_error("RECURSE outside definition");
            env->compile_error = 1;
            return;
        }
        instr.opcode = OP_CALL;
        instr.operand = env->current_word_index;
        env->currentWord.code[env->currentWord.code_length++] = instr;
        return;
    }

    // Affichage d’une définition avec SEE
    if (strcmp(token, "SEE") == 0) {
        char *next_token = strtok_r(NULL, " \t\n", input_rest);
        if (!next_token) {
            send_to_channel("SEE requires a word name");
            env->compile_error = 1;
            return;
        }
        int index = findCompiledWordIndex(next_token);
        if (index >= 0) {
            print_word_definition_irc(index, &env->main_stack);
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "SEE: Unknown word: %s", next_token);
            send_to_channel(msg);
            env->compile_error = 1;
        }
        return;
    }

    // Mode compilation : construire le mot
    if (env->compiling) {
    // Gestion de CASE
 
        // Instructions Forth de base
        if (strcmp(token, "DUP") == 0) instr.opcode = OP_DUP;
        else if (strcmp(token, "DROP") == 0) instr.opcode = OP_DROP;
        else if (strcmp(token, "SWAP") == 0) instr.opcode = OP_SWAP;
        else if (strcmp(token, "OVER") == 0) instr.opcode = OP_OVER;
        else if (strcmp(token, "ROT") == 0) instr.opcode = OP_ROT;
        else if (strcmp(token, "+") == 0) instr.opcode = OP_ADD;
        else if (strcmp(token, "-") == 0) instr.opcode = OP_SUB;
        else if (strcmp(token, "*") == 0) instr.opcode = OP_MUL;
        else if (strcmp(token, "/") == 0) instr.opcode = OP_DIV;
        else if (strcmp(token, "MOD") == 0) instr.opcode = OP_MOD;
        else if (strcmp(token, "=") == 0) instr.opcode = OP_EQ;
        else if (strcmp(token, "<") == 0) instr.opcode = OP_LT;
        else if (strcmp(token, ">") == 0) instr.opcode = OP_GT;
        else if (strcmp(token, "AND") == 0) instr.opcode = OP_AND;
        else if (strcmp(token, "OR") == 0) instr.opcode = OP_OR;
        else if (strcmp(token, "NOT") == 0) instr.opcode = OP_NOT;
        else if (strcmp(token, "XOR") == 0) instr.opcode = OP_XOR;
        else if (strcmp(token, ".") == 0) instr.opcode = OP_DOT;
        else if (strcmp(token, ".S") == 0) instr.opcode = OP_DOT_S;
        else if (strcmp(token, "CR") == 0) instr.opcode = OP_CR;
        else if (strcmp(token, "EMIT") == 0) instr.opcode = OP_EMIT;
        else if (strcmp(token, "@") == 0) instr.opcode = OP_FETCH;
        else if (strcmp(token, "!") == 0) instr.opcode = OP_STORE;
        else if (strcmp(token, ">R") == 0) instr.opcode = OP_TO_R;
        else if (strcmp(token, "R>") == 0) instr.opcode = OP_FROM_R;
        else if (strcmp(token, "R@") == 0) instr.opcode = OP_R_FETCH;
        else if (strcmp(token, "I") == 0) instr.opcode = OP_I;
        else if (strcmp(token, "J") == 0) instr.opcode = OP_J;
        else if (strcmp(token, "DO") == 0) instr.opcode = OP_DO;
        else if (strcmp(token, "LOOP") == 0) instr.opcode = OP_LOOP;
        else if (strcmp(token, "+LOOP") == 0) instr.opcode = OP_PLUS_LOOP;
        else if (strcmp(token, "PICK") == 0) instr.opcode = OP_PICK;
        else if (strcmp(token, "EXIT") == 0) instr.opcode = OP_EXIT;
        else if (strcmp(token, "CLOCK") == 0) instr.opcode = OP_CLOCK;
        else if (strcmp(token, "CLEAR-STACK") == 0) instr.opcode = OP_CLEAR_STACK;
        else if (strcmp(token, "WORDS") == 0) instr.opcode = OP_WORDS;
        else if (strcmp(token, "NUM-TO-BIN") == 0) instr.opcode = OP_NUM_TO_BIN;
        else if (strcmp(token, "PRIME?") == 0) instr.opcode = OP_PRIME_TEST;
        // Opérateurs bit à bit
        else if (strcmp(token, "&") == 0) instr.opcode = OP_BIT_AND;
        else if (strcmp(token, "|") == 0) instr.opcode = OP_BIT_OR;
        else if (strcmp(token, "^") == 0) instr.opcode = OP_BIT_XOR;
        else if (strcmp(token, "~") == 0) instr.opcode = OP_BIT_NOT;
        else if (strcmp(token, "<<") == 0) instr.opcode = OP_LSHIFT;
        else if (strcmp(token, ">>") == 0) instr.opcode = OP_RSHIFT;
        // Gestion de ."
        else if (strcmp(token, ".\"") == 0) {
            char *start = *input_rest;
            char *end = strchr(start, '"');
            if (!end) {
                set_error(".\" expects a string ending with \"");
                env->compile_error = 1;
                return;
            }
            long int len = end - start;
            char *str = malloc(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            instr.opcode = OP_DOT_QUOTE;
            instr.operand = env->currentWord.string_count;
            env->currentWord.strings[env->currentWord.string_count++] = str;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            *input_rest = end + 1;
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            return;
        }
        // Gestion de VARIABLE
        else if (strcmp(token, "VARIABLE") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("VARIABLE requires a name");
                return;
            }
            if (findCompiledWordIndex(next_token) >= 0) {
                set_error("VARIABLE: Name already defined");
                return;
            }
            unsigned long encoded_index = memory_create(&env->memory_list, next_token, TYPE_VAR);
            if (encoded_index == 0) {
                set_error("VARIABLE: Memory creation failed");
                return;
            }
            if (env->dictionary.count >= env->dictionary.capacity) {
                resizeDynamicDictionary(&env->dictionary);
            }
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name = strdup(next_token);
            env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = encoded_index;
            env->dictionary.words[dict_idx].code_length = 1;
            env->dictionary.words[dict_idx].string_count = 0;
            return;
        }
        // Gestion de "
        else if (strcmp(token, "\"") == 0) {
            char *start = *input_rest;
            char *end = strchr(start, '"');
            if (!end) {
                set_error("Missing closing quote for \"");
                env->compile_error = 1;
                return;
            }
            long int len = end - start;
            char *str = malloc(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            if (env->compiling) {
                instr.opcode = OP_QUOTE;
                instr.operand = env->currentWord.string_count;
                env->currentWord.strings[env->currentWord.string_count++] = str;
                env->currentWord.code[env->currentWord.code_length++] = instr;
            } else {
                push_string(str);
                mpz_set_si(mpz_pool[0], env->string_stack_top);
                push(&env->main_stack, mpz_pool[0]);
            }
            *input_rest = end + 1;
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            token = strtok_r(NULL, " \t\n", input_rest);
            if (token) {
                compileToken(token, input_rest, env); // Sans guillemets !
            }
            return;
        }
        // Structures de contrôle
        else if (strcmp(token, "IF") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error("Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_BRANCH_FALSE;
            instr.operand = 0; // À remplir avec THEN ou ELSE
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_IF, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "ELSE") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_IF) {
                set_error("ELSE without IF");
                env->compile_error = 1;
                return;
            }
            ControlEntry if_entry = env->control_stack[env->control_stack_top - 1];
            env->currentWord.code[if_entry.addr].operand = env->currentWord.code_length + 1;
            instr.opcode = OP_BRANCH;
            instr.operand = 0; // À remplir avec THEN
            env->control_stack[env->control_stack_top - 1].type = CT_ELSE;
            env->control_stack[env->control_stack_top - 1].addr = env->currentWord.code_length;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "THEN") == 0) {
            if (env->control_stack_top <= 0) {
                set_error("THEN without IF or ELSE");
                env->compile_error = 1;
                return;
            }
            ControlEntry entry = env->control_stack[--env->control_stack_top];
            if (entry.type == CT_IF || entry.type == CT_ELSE) {
                env->currentWord.code[entry.addr].operand = env->currentWord.code_length;
            } else {
                set_error("Invalid control structure");
                env->compile_error = 1;
                return;
            }
            return;
        }
        else if (strcmp(token, "BEGIN") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error("Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_BEGIN;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_BEGIN, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "WHILE") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_BEGIN) {
                set_error("WHILE without BEGIN");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_WHILE;
            instr.operand = 0; // À remplir avec REPEAT
            env->control_stack[env->control_stack_top - 1].type = CT_WHILE;
            env->control_stack[env->control_stack_top - 1].addr = env->currentWord.code_length;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "REPEAT") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_WHILE) {
                set_error("REPEAT without WHILE");
                env->compile_error = 1;
                return;
            }
            ControlEntry while_entry = env->control_stack[env->control_stack_top - 1];
            env->currentWord.code[while_entry.addr].operand = env->currentWord.code_length + 1;
            instr.opcode = OP_REPEAT;
            instr.operand = while_entry.addr - 8; // Retour à BEGIN
            env->currentWord.code[env->currentWord.code_length++] = instr;
            env->control_stack_top--;
            return;
        }
        else if (strcmp(token, "UNTIL") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_BEGIN) {
                set_error("UNTIL without BEGIN");
                env->compile_error = 1;
                return;
            }
            ControlEntry begin_entry = env->control_stack[--env->control_stack_top];
            instr.opcode = OP_UNTIL;
            instr.operand = begin_entry.addr;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "CASE") == 0) {
            // char debug_msg[512];
            // snprintf(debug_msg, sizeof(debug_msg), "CASE: control_stack_top=%d", env->control_stack_top);
            // send_to_channel(debug_msg);
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error("Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_CASE;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_CASE, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            // snprintf(debug_msg, sizeof(debug_msg), "After CASE: control_stack_top=%d, type=%d", env->control_stack_top, env->control_stack[env->control_stack_top - 1].type);
            // send_to_channel(debug_msg);
            return;
        }
        // Gestion de OF
else if (strcmp(token, "OF") == 0) {
    // char debug_msg[512];
    // snprintf(debug_msg, sizeof(debug_msg), "OF: control_stack_top=%d, top_type=%d", env->control_stack_top, env->control_stack_top > 0 ? env->control_stack[env->control_stack_top - 1].type : -1);
    // send_to_channel(debug_msg);
    // Vérifier qu’un CT_CASE existe dans la pile
    int case_found = 0;
    for (int i = 0; i < env->control_stack_top; i++) {
        if (env->control_stack[i].type == CT_CASE) {
            case_found = 1;
            break;
        }
    }
    if (!case_found) {
        set_error("OF without CASE");
        env->compile_error = 1;
        return;
    }
    instr.opcode = OP_OF;
    instr.operand = 0; // À remplir avec ENDOF
    env->control_stack[env->control_stack_top++] = (ControlEntry){CT_OF, env->currentWord.code_length};
    env->currentWord.code[env->currentWord.code_length++] = instr;
    return;
}
        // Gestion de ENDOF
        else if (strcmp(token, "ENDOF") == 0) {
            //char debug_msg[512];
            //snprintf(debug_msg, sizeof(debug_msg), "ENDOF: control_stack_top=%d", env->control_stack_top);
            //send_to_channel(debug_msg);
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_OF) {
                set_error("ENDOF without OF");
                env->compile_error = 1;
                return;
            }
            ControlEntry of_entry = env->control_stack[--env->control_stack_top];
            env->currentWord.code[of_entry.addr].operand = env->currentWord.code_length + 1;
            instr.opcode = OP_ENDOF;
            instr.operand = 0; // À remplir avec ENDCASE
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_ENDOF, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        // Gestion de ENDCASE
else if (strcmp(token, "ENDCASE") == 0) {
    //char debug_msg[512];
    //snprintf(debug_msg, sizeof(debug_msg), "ENDCASE: control_stack_top=%d", env->control_stack_top);
    //send_to_channel(debug_msg);
    if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_ENDOF) {
        set_error("ENDCASE without ENDOF or CASE");
        env->compile_error = 1;
        return;
    }
    while (env->control_stack_top > 0 && env->control_stack[env->control_stack_top - 1].type == CT_ENDOF) {
        ControlEntry endof_entry = env->control_stack[--env->control_stack_top];
        env->currentWord.code[endof_entry.addr].operand = env->currentWord.code_length;
    }
    if (env->control_stack_top > 0 && env->control_stack[env->control_stack_top - 1].type == CT_CASE) {
        env->control_stack_top--; // Dépiler CASE
    } else {
        set_error("ENDCASE without matching CASE");
        env->compile_error = 1;
        return;
    }
    instr.opcode = OP_ENDCASE;
    env->currentWord.code[env->currentWord.code_length++] = instr;
    // snprintf(debug_msg, sizeof(debug_msg), "After ENDCASE: control_stack_top=%d", env->control_stack_top);
    // send_to_channel(debug_msg);
    return;
}
        // Cas général : mot existant ou nombre
        else {
            int index = findCompiledWordIndex(token);
            if (index >= 0) {
                instr.opcode = OP_CALL;
                instr.operand = index;
            } else {
                mpz_t test_num;
                mpz_init(test_num);
                if (mpz_set_str(test_num, token, 10) == 0) {
                    instr.opcode = OP_PUSH;
                    instr.operand = env->currentWord.string_count;
                    env->currentWord.strings[env->currentWord.string_count++] = strdup(token);
                } else {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Unknown word in definition: %s", token);
                    send_to_channel(msg);
                    env->compile_error = 1;
                    mpz_clear(test_num);
                    return;
                }
                mpz_clear(test_num);
            }
        }

        // Ajouter l’instruction au mot en cours
        if (env->currentWord.code_length < WORD_CODE_SIZE) {
            env->currentWord.code[env->currentWord.code_length++] = instr;
        } else {
            set_error("Word code size exceeded");
            env->compile_error = 1;
        }
    }

    // Mode interprétation
    else {
        if (strcmp(token, "LOAD") == 0) {
            char *next_token = strtok_r(NULL, "\"", input_rest);
            if (!next_token || *next_token == '\0') {
                set_error("LOAD: No filename provided");
                return;
            }
            while (*next_token == ' ' || *next_token == '\t') next_token++;
            if (*next_token == '\0') {
                set_error("LOAD: No filename provided");
                return;
            }
            char filename[512];
            strncpy(filename, next_token, sizeof(filename) - 1);
            filename[sizeof(filename) - 1] = '\0';
            FILE *file = fopen(filename, "r");
            if (file) {
                char buffer[1024];
                while (fgets(buffer, sizeof(buffer), file)) {
                    buffer[strcspn(buffer, "\n")] = '\0';
                    interpret(buffer, &env->main_stack);
                }
                fclose(file);
            } else {
                char error_msg[1024];
                snprintf(error_msg, sizeof(error_msg), "Error: LOAD: Cannot open file '%s'", filename);
                set_error(error_msg);
            }
            strtok_r(NULL, " \t\n", input_rest);
        }
        else if (strcmp(token, "CREATE") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("CREATE requires a name");
                env->compile_error = 1;
                return;
            }
            if (findCompiledWordIndex(next_token) >= 0) {
                char msg[512];
                snprintf(msg, sizeof(msg), "CREATE: '%s' already defined", next_token);
                set_error(msg);
                env->compile_error = 1;
                return;
            }
            unsigned long index = memory_create(&env->memory_list, next_token, TYPE_VAR);
            if (index == 0) {
                set_error("CREATE: Memory creation failed");
                env->compile_error = 1;
                return;
            }
            if (env->dictionary.count >= env->dictionary.capacity) {
                resizeDynamicDictionary(&env->dictionary);
            }
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name = strdup(next_token);
            env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = index;
            env->dictionary.words[dict_idx].code_length = 1;
            env->dictionary.words[dict_idx].string_count = 0;
            if (env->compiling) {
                instr.opcode = OP_CREATE;
                instr.operand = env->currentWord.string_count;
                env->currentWord.strings[env->currentWord.string_count++] = strdup(next_token);
                env->currentWord.code[env->currentWord.code_length++] = instr;
            }
        }
        else if (strcmp(token, "VARIABLE") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("VARIABLE requires a name");
                return;
            }
            if (findCompiledWordIndex(next_token) >= 0) {
                set_error("VARIABLE: Name already defined");
                return;
            }
            unsigned long encoded_index = memory_create(&env->memory_list, next_token, TYPE_VAR);
            if (encoded_index == 0) {
                set_error("VARIABLE: Memory creation failed");
                return;
            }
            if (env->dictionary.count >= env->dictionary.capacity) {
                resizeDynamicDictionary(&env->dictionary);
            }
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name = strdup(next_token);
            env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = encoded_index;
            env->dictionary.words[dict_idx].code_length = 1;
            env->dictionary.words[dict_idx].string_count = 0;
        }
        else if (strcmp(token, ".\"") == 0) {
            char *next_token = strtok_r(NULL, "\"", input_rest);
            if (!next_token) {
                set_error(".\" expects a string ending with \"");
                return;
            }
            send_to_channel(next_token);
            strtok_r(NULL, " \t\n", input_rest);
        }
        else if (strcmp(token, "\"") == 0) {
            char *start = *input_rest;
            char *end = strchr(start, '"');
            if (!end) {
                set_error("Missing closing quote for \"");
                return;
            }
            long int len = end - start;
            char *str = malloc(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            push_string(str);
            mpz_set_si(mpz_pool[0], env->string_stack_top);
            push(&env->main_stack, mpz_pool[0]);
            *input_rest = end + 1;
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (next_token) {
                compileToken(next_token, input_rest, env);
            }
        }
        else {
            int idx = findCompiledWordIndex(token);
            if (idx >= 0) {
                executeCompiledWord(&env->dictionary.words[idx], &env->main_stack, idx);
            } else {
                mpz_t test_num;
                mpz_init(test_num);
                if (mpz_set_str(test_num, token, 10) == 0) {
                    push(&env->main_stack, test_num);
                } else {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Unknown word: %s", token);
                    send_to_channel(msg);
                }
                mpz_clear(test_num);
            }
        }
    }
}

// Connexion IRC
 
 
 void irc_connect(const char *server_ip, const char *bot_nick, const char *channel) {
    if (irc_socket != -1) {
        close(irc_socket);
        irc_socket = -1;
    }

    irc_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (irc_socket == -1) {
        perror("socket");
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6667);  // Port fixe pour l’instant
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(irc_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(irc_socket);
        irc_socket = -1;
        return;
    }

    char buffer[512];
    snprintf(buffer, sizeof(buffer), "NICK %s\r\n", bot_nick);
    send(irc_socket, buffer, strlen(buffer), 0);

    snprintf(buffer, sizeof(buffer), "USER %s 0 * :%s\r\n", bot_nick, bot_nick);
    send(irc_socket, buffer, strlen(buffer), 0);

    snprintf(buffer, sizeof(buffer), "JOIN %s\r\n", channel);
    send(irc_socket, buffer, strlen(buffer), 0);
}

#include <netdb.h> // Ajouter cet include pour gethostbyname

int main(int argc, char *argv[]) {
    char *server_name = "irc.example.com"; // Nom du serveur par défaut
    char *bot_nick = "forth";
    channel = "#labynet";
    struct hostent *server;

    if (argc != 4) {
        printf("Usage: %s <SERVER_NAME> <NICK> <CHANNEL>\n", argv[0]);
        printf("Using defaults: SERVER=%s, NICK=%s, CHANNEL=%s\n", server_name, bot_nick, channel);
    } else {
        server_name = argv[1];
        bot_nick = argv[2];
        channel = argv[3];
    }

    init_mpz_pool();

    while (1) {
        // Résolution DNS du nom du serveur
        server = gethostbyname(server_name);
        if (server == NULL) {
            printf("Failed to resolve hostname %s, retrying in 5 seconds...\n", server_name);
            sleep(5);
            continue;
        }

        // Utiliser la première adresse IP résolue (assumée IPv4)
        char *server_ip = inet_ntoa(*(struct in_addr *)server->h_addr_list[0]);
        irc_connect(server_ip, bot_nick, channel);
        if (irc_socket == -1) {
            printf("Failed to connect to %s (%s), retrying in 5 seconds...\n", server_name, server_ip);
            sleep(5);
            continue;
        }

        printf("Connected to %s (%s) on channel %s as %s\n", server_name, server_ip, channel, bot_nick);
        char buffer[512];
        while (1) {
            int bytes = recv(irc_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                printf("Disconnected from %s, reconnecting in 5 seconds...\n", server_name);
                close(irc_socket);
                irc_socket = -1;
                sleep(5);
                break;
            }
            buffer[bytes] = '\0';

            if (strstr(buffer, "PING ") == buffer) {
                char pong[512];
                snprintf(pong, sizeof(pong), "PONG %s\r\n", buffer + 5);
                send(irc_socket, pong, strlen(pong), 0);
                continue;
            }

            char *nick_start = strchr(buffer, ':');
            if (!nick_start) continue;
            nick_start++;
            char *nick_end = strchr(nick_start, '!');
            if (!nick_end) continue;
            char nick[MAX_STRING_SIZE];
            strncpy(nick, nick_start, nick_end - nick_start);
            nick[nick_end - nick_start] = '\0';

            char prefix[512];
            snprintf(prefix, sizeof(prefix), "PRIVMSG %s :%s:", channel, bot_nick);
            char *msg = strstr(buffer, prefix);
            if (msg) {
                char forth_cmd[512];
                snprintf(forth_cmd, sizeof(forth_cmd), "%s", msg + strlen(prefix));
                forth_cmd[strcspn(forth_cmd, "\r\n")] = '\0';

                // Trim des espaces
                char *trimmed_cmd = forth_cmd;
                while (isspace((unsigned char)*trimmed_cmd)) trimmed_cmd++;
                size_t len = strlen(trimmed_cmd);
                while (len > 0 && isspace((unsigned char)trimmed_cmd[len - 1])) {
                    trimmed_cmd[--len] = '\0';
                }

                // Vérification de QUIT
                if (strcmp(trimmed_cmd, "QUIT") == 0) {
                    printf("DEBUG: QUIT detected for %s\n", nick);
                    Env *env = findEnv(nick);
                    if (env) {
                        freeEnv(nick);
                        char quit_msg[512];
                        snprintf(quit_msg, sizeof(quit_msg), "Environment for %s has been freed.", nick);
                        send_to_channel(quit_msg);
                    } else {
                        send_to_channel("No environment found for you to quit.");
                    }
                    continue;
                }

                // Gestion normale
                Env *env = findEnv(nick);
                if (!env) {
                    env = createEnv(nick);
                    if (!env) {
                        printf("Failed to create env for %s\n", nick);
                        continue;
                    }
                    currentenv = env;
                    printf("Created env for %s\n", nick);
                    initDictionary(env);
                } else {
                    currentenv = env;
                }

                if (!currentenv) {
                    printf("currentenv is NULL!\n");
                    send_to_channel("Error: Environment not initialized");
                } else {
                    interpret(trimmed_cmd, &env->main_stack);
                }
            }
        }
    }

    while (head) freeEnv(head->nick);
    clear_mpz_pool();
    if (irc_socket != -1) close(irc_socket);
    return 0;
}
