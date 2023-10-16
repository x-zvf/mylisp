#ifndef HG_L_INTERPRETER_H
#define HG_L_INTERPRETER_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>


typedef struct lInterpreter {
    int foo;
} l_interpreter_t;

typedef enum lValueType {
    L_VALUE_ERROR,
    L_VALUE_NUMBER,
    L_VALUE_STRING,
    L_VALUE_BOOL,
    L_VALUE_NIL,
    L_VALUE_LIST
} l_value_type_t;

typedef struct lValue {
    l_value_type_t type;
    union {
        double doubleValue;
        long long longValue;
        char* string;
        bool boolean;
        struct {
            struct lValue* values;
            int count;
        } list;
    } value;
    char flags;
    
} l_value_t;

typedef enum lTokenType {
    TOKEN_RPAREN,
    TOKEN_LPAREN,
    TOKEN_QUOTE,
    TOKEN_SYMBOL,
    TOKEN_INTEGER,
    TOKEN_REAL,
    TOKEN_STRING,
    TOKEN_BOOLEAN,
    TOKEN_CHARACTER,

    TOKEN_EOF,
    TOKEN_ERROR
} l_token_type_t;

typedef struct lToken {
    l_token_type_t type;
    union value{
        long long number;
        double real;
        bool boolean;
        char character;
        char *string;
        const char *error_message;
        char *symbol;
    } value;
} l_token_t;

typedef struct lTokenizer {
    const char *data;
    size_t offset;
    size_t data_length;
} l_tokenizer_t;

l_interpreter_t* l_interpreter_create();
void l_interpreter_destroy(l_interpreter_t* interpreter);

l_value_t l_interpreter_eval(l_interpreter_t* interpreter, const char* source);

void l_debug_print_value(l_value_t *value);
void l_debug_print_token(l_token_t *token);

l_token_t l_tokenizer_next(l_tokenizer_t *tokenizer);

#define L_INTERPRETER_IMPLEMENTATION 1






#ifdef L_INTERPRETER_IMPLEMENTATION 

static bool isSymbol(char c) {
    return isalpha(c) || c == '_' || c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == '<' || c == '>' || c == '?' || c == '!';
}

l_token_t l_tokenizer_next(l_tokenizer_t *tokenizer) {

#define NEXT_CHAR(i) tokenizer->data[tokenizer->offset + i]
#define ADVANCE(i) tokenizer->offset += i
#define REQUIRE_CHARS(n, MSG)                                                   \
    if (tokenizer->offset + n >= tokenizer->data_length) {                      \
        l_token_t err = {.type = TOKEN_ERROR,                                   \
            .value.error_message = "Unexpected end of file:" #MSG};             \
        return err;                                                             \
    }
#define HAS_CHARS(n) (tokenizer->offset + n <= tokenizer->data_length)

#define IS_TOKEN_SEPARATOR(c) (c == ' ' || c == '\n' || c == '\t' || c == '\0' || c == '(' || c == ')' || c == '\'')

    if(NEXT_CHAR(0) == ';' && NEXT_CHAR(1) == ';') {
        ADVANCE(2);
        while(NEXT_CHAR(0) != '\n' && NEXT_CHAR(0) != '\0') {
            ADVANCE(1);
        }
        if(NEXT_CHAR(0) == '\n') {
            ADVANCE(1);
        }
    }
    while(isspace(NEXT_CHAR(0))) {
        ADVANCE(1);
    }
    if(NEXT_CHAR(0) == '\0')
        return (l_token_t) {.type = TOKEN_EOF};

    if(NEXT_CHAR(0) == '(') {
        ADVANCE(1);
        return (l_token_t) {.type = TOKEN_LPAREN};
    }
    if(NEXT_CHAR(0) == ')') {
        ADVANCE(1);
        return (l_token_t) {.type = TOKEN_RPAREN};
    }
    if(NEXT_CHAR(0) == '\'') {
        ADVANCE(1);
        return (l_token_t) {.type = TOKEN_QUOTE};
    }
    if(NEXT_CHAR(0) == '"') {
        ADVANCE(1);
        int start = tokenizer->offset;
        while(NEXT_CHAR(0) != '"') {
            if(NEXT_CHAR(0) == '\0') {
                l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Unexpected end of file while reading string"};
                return err;
            }
            ADVANCE(1);
            if(NEXT_CHAR(0) == '\\') {
                REQUIRE_CHARS(1, "Unexpected end of file while reading string escape sequence");
                ADVANCE(2);
            }
        }
        ADVANCE(1);
        int length = tokenizer->offset - start - 1;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        return (l_token_t) {.type = TOKEN_STRING, .value.string = string};
    }
    if(NEXT_CHAR(0) == '#') {
        ADVANCE(1);
        REQUIRE_CHARS(1, "Unexpected end of file after #");
        if(NEXT_CHAR(0) == 't') {
            ADVANCE(1);
            return (l_token_t) {.type = TOKEN_BOOLEAN, .value.boolean = true};
        }
        if(NEXT_CHAR(0) == 'f') {
            ADVANCE(1);
            return (l_token_t) {.type = TOKEN_BOOLEAN, .value.boolean = false};
        }
        l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Unexpected character after #"};
        return err;
    }

    if(NEXT_CHAR(0) == '\\') {
        ADVANCE(1);
        REQUIRE_CHARS(1, "Unexpected end of file after \\");
        char c = NEXT_CHAR(0);
        ADVANCE(1);
        return (l_token_t) {.type = TOKEN_CHARACTER, .value.character = c};
    }
    if(HAS_CHARS(2) && NEXT_CHAR(0) == '0' && NEXT_CHAR(1) == 'x') {
        ADVANCE(2);
        int start = tokenizer->offset;
        while(isxdigit(NEXT_CHAR(0))) {
            ADVANCE(1);
        }
        if(!IS_TOKEN_SEPARATOR(NEXT_CHAR(0))) {
            l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Unexpected character in hex integer"};
            return err;
        }
        int length = tokenizer->offset - start;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        long long value = strtoll(string, NULL, 16);
        if(errno == ERANGE) {
            l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Integer literal out of range"};
            return err;
        }
        return (l_token_t) {.type = TOKEN_INTEGER, .value.number = value};
    }
    if(HAS_CHARS(2) && NEXT_CHAR(0) == '0' && NEXT_CHAR(1) == 'o') {
        ADVANCE(2);
        int start = tokenizer->offset;
        while(isdigit(NEXT_CHAR(0)) && NEXT_CHAR(0) < '8') {
            ADVANCE(1);
        }
        if(!IS_TOKEN_SEPARATOR(NEXT_CHAR(0))) {
            l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Unexpected character in octal integer"};
            return err;
        }
        int length = tokenizer->offset - start;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        long long value = strtoll(string, NULL, 8);
        if(errno == ERANGE) {
            l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Integer literal out of range"};
            return err;
        }
        return (l_token_t) {.type = TOKEN_INTEGER, .value.number = value};
    }
    if(HAS_CHARS(2) && NEXT_CHAR(0) == '0' && NEXT_CHAR(1) == 'b') {
        ADVANCE(2);
        int start = tokenizer->offset;
        while(NEXT_CHAR(0) == '0' || NEXT_CHAR(0) == '1') {
            ADVANCE(1);
        }
        if(!IS_TOKEN_SEPARATOR(NEXT_CHAR(0))) {
            l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Unexpected character in binary integer"};
            return err;
        }
        int length = tokenizer->offset - start;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        long long value = strtoll(string, NULL, 2);
        if(errno == ERANGE) {
            l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Integer literal out of range"};
            return err;
        }
        return (l_token_t) {.type = TOKEN_INTEGER, .value.number = value};
    }
    if((NEXT_CHAR(0) >= '0' && NEXT_CHAR(0) <= '9')
        || (HAS_CHARS(2) && NEXT_CHAR(1) >= '0' && NEXT_CHAR(1) <= '9'
             && (NEXT_CHAR(0) == '-' || NEXT_CHAR(0) == '+' || NEXT_CHAR(0) == '.'))){
        int start = tokenizer->offset;
        while(isdigit(NEXT_CHAR(0))) {
            ADVANCE(1);
        }
        if(NEXT_CHAR(0) == '.') {
            ADVANCE(1);
            while(isdigit(NEXT_CHAR(0))) {
                ADVANCE(1);
            }
            if(NEXT_CHAR(0) == 'e' || NEXT_CHAR(0) == 'E') {
                ADVANCE(1);
                if(NEXT_CHAR(0) == '+' || NEXT_CHAR(0) == '-') {
                    ADVANCE(1);
                }
                while(isdigit(NEXT_CHAR(0))) {
                    ADVANCE(1);
                }
            }
            int length = tokenizer->offset - start;
            char *string = (char*)malloc(length + 1);
            memcpy(string, tokenizer->data + start, length);
            string[length] = '\0';
            double value = strtod(string, NULL);
            if(errno == ERANGE) {
                l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Real literal out of range"};
                return err;
            }
            return (l_token_t) {.type = TOKEN_REAL, .value.real = value};
        }
        if(!IS_TOKEN_SEPARATOR(NEXT_CHAR(0))) {
            l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Unexpected character in integer"};
            return err;
        }
        int length = tokenizer->offset - start;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        long long value = strtoll(string, NULL, 10);
        if(errno == ERANGE) {
            l_token_t err = {.type = TOKEN_ERROR, .value.error_message = "Integer literal out of range"};
            return err;
        }
        return (l_token_t) {.type = TOKEN_INTEGER, .value.number = value};
    }
    if(isSymbol(NEXT_CHAR(0))) {
        int start = tokenizer->offset;
        while(isSymbol(NEXT_CHAR(0))) {
            ADVANCE(1);
        }
        int length = tokenizer->offset - start;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        return (l_token_t) {.type = TOKEN_SYMBOL, .value.symbol = string};
    }
    return (l_token_t) {.type = TOKEN_ERROR, .value.error_message = "Unexpected character"};
}

l_value_t l_interpreter_eval(l_interpreter_t *interpreter, const char *source) { 
    (void) interpreter;
    l_tokenizer_t tokenizer;
    tokenizer.data = source;
    tokenizer.offset = 0;
    tokenizer.data_length = strlen(source);

    l_token_t token;
    do {
    token = l_tokenizer_next(&tokenizer);
    l_debug_print_token(&token);
    } while(token.type != TOKEN_EOF && token.type != TOKEN_ERROR);

    l_value_t value = {.type = L_VALUE_ERROR};
    return value;
}

l_interpreter_t* l_interpreter_create() {
    l_interpreter_t* interpreter = (l_interpreter_t*)malloc(sizeof(l_interpreter_t));
    return interpreter;
}
void l_interpreter_destroy(l_interpreter_t* interpreter) {
    free(interpreter);
}

void l_debug_print_token(l_token_t *token) {
    printf("TOKEN: ");
    switch(token->type) {
        case TOKEN_RPAREN: {
            printf(")");
        } break;
        case TOKEN_LPAREN: {
            printf("(");
        } break;
        case TOKEN_QUOTE: {
            printf("'");
        } break;
        case TOKEN_SYMBOL: {
            printf("%s", token->value.symbol);
        } break;
        case TOKEN_INTEGER: {
            printf("%lld", token->value.number);
        } break;
        case TOKEN_REAL: {
            printf("%f", token->value.real);
        } break;
        case TOKEN_STRING: {
            printf("\"%s\"", token->value.string);
        } break;
        case TOKEN_BOOLEAN: {
            printf("%s", token->value.boolean ? "#t" : "#f");
        } break;
        case TOKEN_CHARACTER: {
            printf("#\\%c", token->value.character);
        } break;
        case TOKEN_EOF: {
            printf("EOF");
        } break;
        case TOKEN_ERROR: {
            printf("ERROR: %s", token->value.error_message);
        } break;
    }
    printf("\n");


}
void l_debug_print_value(l_value_t *value) {
    switch(value->type) {
        case L_VALUE_ERROR: {
            printf("ERROR");
        } break;
        case L_VALUE_NUMBER: {
            printf("%f", value->value.doubleValue);
        } break;
        case L_VALUE_STRING: {
            printf("%s", value->value.string);
        } break;
        case L_VALUE_BOOL: {
            printf("%s", value->value.boolean ? "true" : "false");
        } break;
        case L_VALUE_NIL: {
            printf("nil");
        } break;
        case L_VALUE_LIST: {
            printf("(");
            for(int i = 0; i < value->value.list.count; i++) {
                l_debug_print_value(&value->value.list.values[i]);
                if(i != value->value.list.count - 1) {
                    printf(" ");
                }
            }
            printf(")");
        } break;
    }
}


#endif // L_INTERPRETER_IMPLEMENTATION
#endif // HG_L_INTERPRETER_H
