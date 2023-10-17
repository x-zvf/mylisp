#ifndef HG_L_INTERPRETER_H
#define HG_L_INTERPRETER_H

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <stdbool.h>



typedef enum lValueType {
    L_VALUE_ERROR,
    L_VALUE_NUMBER,
    L_VALUE_STRING,
    L_VALUE_CHARACTER,
    L_VALUE_BOOL,
    L_VALUE_NIL,
    L_VALUE_SYMBOL,
    L_VALUE_LIST
} l_value_type_t;

#define L_VALUE_FLAG_NONE 0
#define L_VALUE_FLAG_INTEGER 1
#define L_VALUE_FLAG_REAL 2

typedef struct lVector {
    size_t capacity;
    size_t length;
    size_t element_size;
    void *data;
    void (*destroy)(void *data);
} l_vector_t;

typedef struct lValue {
    l_value_type_t type;
    union {
        double double_value;
        long long long_value;
        size_t string_index;
        size_t symbol_index;
        char character;
        bool boolean;
        l_vector_t list;
    } value;
    char flags;
    
} l_value_t;

//TODO: make this a hash table
typedef struct lTable {
    l_vector_t keys;
    l_vector_t values;
} l_table_t;

typedef struct lRefCounted {
    size_t ref_count;
    void *data;
    void (*destroy)(void *data);
} l_ref_counted_t;

typedef struct lEnvironment {
    l_table_t *symbol_table;
    struct lEnvironment *parent;
} l_environment_t;

typedef struct lInterpreter {
    //l_environment_t *global_environment;
    //l_environment_t *current_environment;
    l_vector_t *string_table; //<RefCounted<char*>>
} l_interpreter_t;

typedef enum lTokenType {
    TOKEN_PLACEHOLDER = 0,
    TOKEN_RPAREN = 1,
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
    size_t line;
    size_t column;
} l_tokenizer_t;



l_interpreter_t* l_interpreter_create();
void l_interpreter_destroy(l_interpreter_t* interpreter);

l_value_t l_interpreter_eval(l_interpreter_t* interpreter, const char* source);
l_value_t l_interpreter_execute(l_interpreter_t *interpreter, l_value_t s_expression);

void l_debug_print_value(l_value_t *value, l_vector_t *string_table);
void l_debug_print_token(l_token_t *token);

l_token_t l_tokenizer_next(l_tokenizer_t *tokenizer);

l_value_t l_parse_expression(l_token_t first, l_tokenizer_t *tokenizer, l_vector_t **string_table);
l_value_t l_parse_list(l_tokenizer_t *tokenizer, l_vector_t **string_table);
l_value_t l_parse_atom(l_token_t *token, l_vector_t **string_table);
l_value_t l_parse_quote(l_tokenizer_t *tokenizer, l_vector_t **string_table);

void l_vector_init(l_vector_t *vector, size_t element_size, size_t initial_capacity, void (*destroy)(void *data));
void l_vector_destroy(l_vector_t *vector);
void l_vector_push(l_vector_t *vector, void *element);
void *l_vector_get(l_vector_t *vector, size_t index);

void l_table_init(l_table_t *table, size_t key_size, size_t value_size, size_t initial_capacity);
void l_table_destroy(l_table_t *table);

size_t l_intern_string(l_vector_t **string_table, const char *string, bool eternal);
const char *l_get_interned_string(l_vector_t *string_table, size_t index);

void l_ref_counted_init(l_ref_counted_t *ref_counted, void *data, void (*destroy)(void *data));
void l_ref_counted_destroy(l_ref_counted_t *ref_counted);

void l_environment_init(l_environment_t *environment, l_environment_t *parent);
void l_environment_destroy(l_environment_t *environment);

void l_value_destroy(l_value_t *value);
#define L_INTERPRETER_IMPLEMENTATION 1






#ifdef L_INTERPRETER_IMPLEMENTATION 

void l_vector_init(l_vector_t *vector, size_t element_size, size_t initial_capacity, void (*destroy)(void *data)) {
    vector->capacity = initial_capacity;
    vector->length = 0;
    vector->element_size = element_size;
    vector->destroy = destroy;
    if(initial_capacity > 0) {
        vector->data = malloc(element_size * initial_capacity);
        if(vector->data == NULL) {
            free(vector->data);
            vector->capacity = 0;
        }
    } else {
        vector->data = NULL;
    }
}

void l_vector_destroy(l_vector_t *vector) {
    if(vector->destroy != NULL) {
        for(size_t i = 0; i < vector->length; i++) {
            vector->destroy((char *)vector->data + i * vector->element_size);
        }
    }
    free(vector->data);
    vector->data = NULL;
    vector->capacity = 0;
    vector->length = 0;
}

void l_vector_push(l_vector_t *vector, void *element) {
    if(vector->length == vector->capacity) {
        vector->capacity *= 2;
        vector->data = realloc(vector->data, vector->capacity * vector->element_size);
    }
    memcpy((char *)vector->data + vector->length * vector->element_size, element, vector->element_size);
    vector->length++;
}

void *l_vector_get(l_vector_t *vector, size_t index) {
    if(index >= vector->length) {
        return NULL;
    }
    return (char *)vector->data + index * vector->element_size;
}


static bool isSymbol(char c) {
    return isalpha(c) || c == '_' || c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == '<' || c == '>' || c == '?' || c == '!';
}

l_token_t l_tokenizer_next(l_tokenizer_t *tokenizer) {

#define RETURN_ERROR_TOKEN(fmt, ...)                                            \
    do{                                                                         \
        char *message;                                                          \
        asprintf(&message,"[%s:%d]: Tokenizing error at [%zu:%zu]: "#fmt,       \
                __FILE__,__LINE__, tokenizer->line,                             \
                tokenizer->column,  __VA_ARGS__);                               \
        l_token_t err = {.type = TOKEN_ERROR, .value.error_message = message};  \
        return err;                                                             \
    } while(0)

#define NEXT_CHAR(i) tokenizer->data[tokenizer->offset + i]
#define ADVANCE(i)\
    do {                                                                        \
        if(tokenizer->offset + i >= tokenizer->data_length) {                   \
            RETURN_ERROR_TOKEN(                                                 \
                    "Unexpected end of file trying to advance %d characters",   \
                    i);                                                         \
        }                                                                       \
        tokenizer->offset += i;                                                 \
    }while(0)
#define HAS_CHARS(n) (tokenizer->offset + n < tokenizer->data_length)

#define IS_TOKEN_SEPARATOR(c) \
    (c == ' ' || c == '\n' || c == '\t' || c == '\0' || c == '(' || c == ')' || c == '\'')


#define REQUIRE_CHARS(n, MSG)                                                   \
    if (tokenizer->offset + n >= tokenizer->data_length) {                      \
        RETURN_ERROR_TOKEN("required %d chars, only have %zu: "#MSG,            \
                n, tokenizer->data_length - tokenizer->offset);                 \
    }

    if(NEXT_CHAR(0) == ';' && NEXT_CHAR(1) == ';') {
        ADVANCE(2);
        while(NEXT_CHAR(0) != '\n' && NEXT_CHAR(0) != '\0') {
            ADVANCE(1);
        }
        if(NEXT_CHAR(0) == '\n') {
            ADVANCE(1);
        }
    }
    while(isspace(NEXT_CHAR(0)) && HAS_CHARS(1)) {
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
            REQUIRE_CHARS(1, "reading string");
            ADVANCE(1);
            if(NEXT_CHAR(0) == '\\') {
                REQUIRE_CHARS(1, "reading string escape sequence");
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
        free(string);
        if(errno == ERANGE) {
            RETURN_ERROR_TOKEN("Integer literal out of range%s", "");
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
            RETURN_ERROR_TOKEN("Unexpected character in octal integer%s", "");
        }
        int length = tokenizer->offset - start;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        long long value = strtoll(string, NULL, 8);
        free(string);
        if(errno == ERANGE) {
            RETURN_ERROR_TOKEN("Octal integer literal out of range%s","");
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
            RETURN_ERROR_TOKEN("Unexpected character in binary integer%s","");
        }
        int length = tokenizer->offset - start;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        long long value = strtoll(string, NULL, 2);
        free(string);
        if(errno == ERANGE) {
            RETURN_ERROR_TOKEN("Binary integer literal out of range%s","");
        }
        return (l_token_t) {.type = TOKEN_INTEGER, .value.number = value};
    }
    if((NEXT_CHAR(0) >= '0' && NEXT_CHAR(0) <= '9')
        || (HAS_CHARS(2) && NEXT_CHAR(1) >= '0' && NEXT_CHAR(1) <= '9'
             && (NEXT_CHAR(0) == '-' || NEXT_CHAR(0) == '+' || NEXT_CHAR(0) == '.'))){
        int start = tokenizer->offset;
        if(NEXT_CHAR(0) == '-' || NEXT_CHAR(0) == '+') {
            ADVANCE(1);
        }
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
            free(string);
            if(errno == ERANGE) {
                RETURN_ERROR_TOKEN("Real literal out of range%s","");
            }
            return (l_token_t) {.type = TOKEN_REAL, .value.real = value};
        }
        if(!IS_TOKEN_SEPARATOR(NEXT_CHAR(0))) {
            RETURN_ERROR_TOKEN("Unexpected character in integer: %c",NEXT_CHAR(0));
        }
        int length = tokenizer->offset - start;
        char *string = (char*)malloc(length + 1);
        memcpy(string, tokenizer->data + start, length);
        string[length] = '\0';
        long long value = strtoll(string, NULL, 10);
        free(string);
        if(errno == ERANGE) {
            RETURN_ERROR_TOKEN("Integer literal out of range%s","");
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
    if(!HAS_CHARS(1)) {
        return (l_token_t) {.type = TOKEN_EOF};
    }
    return (l_token_t) {.type = TOKEN_ERROR, .value.error_message = "Unexpected character"};
}

l_value_t l_interpreter_eval(l_interpreter_t *interpreter, const char *source) { 
    (void) interpreter;
    l_tokenizer_t tokenizer;
    tokenizer.data = source;
    tokenizer.offset = 0;
    tokenizer.data_length = strlen(source);

    l_token_t first = l_tokenizer_next(&tokenizer);
    l_value_t result;
    while(first.type != TOKEN_EOF) {
        result = l_parse_expression(first, &tokenizer, &interpreter->string_table);
        l_debug_print_value(&result, interpreter->string_table);

        if(result.type == L_VALUE_ERROR) {
            break;
        }

        if(result.type == L_VALUE_LIST) {
            result = l_interpreter_execute(interpreter, result);
        }

        printf("\n");
        l_value_destroy(&result);
        first = l_tokenizer_next(&tokenizer);
    }

    return result;
}

l_value_t l_interpreter_execute(l_interpreter_t *interpreter, l_value_t s_expression) {
    (void) interpreter;
    (void) s_expression;

    return s_expression;
}

l_value_t l_parse_expression(l_token_t first, l_tokenizer_t *tokenizer, l_vector_t **string_table) {
    //l_token_t token = l_tokenizer_next(tokenizer);
    const char *err_where = NULL;
    const char *err_why = NULL;
    switch(first.type) {
        case TOKEN_LPAREN:
            return l_parse_list(tokenizer, string_table);
        case TOKEN_QUOTE:
            return l_parse_quote(tokenizer, string_table);
        case TOKEN_REAL:
        case TOKEN_INTEGER:
        case TOKEN_STRING:
        case TOKEN_BOOLEAN:
        case TOKEN_CHARACTER:
        case TOKEN_SYMBOL:
            return l_parse_atom(&first, string_table);
        case TOKEN_ERROR:
            err_where = "tokenization";
            err_why = first.value.error_message;
            break;
        case TOKEN_EOF:
            err_where = "parsing";
            err_why = "unexpected end of file";
            break;
        case TOKEN_RPAREN:
            err_where = "parsing";
            err_why = "unexpected ')'";
            break;
        default:
            err_where = "parsing";
            err_why = "unhandled token type";
            break;
    }
    char *error_message;
    asprintf(&error_message, "Error while parsing token type %d: where: %s why: %s", first.type, err_where, err_why);
    size_t interned_index = l_intern_string(string_table, error_message, false);
    return (l_value_t) {.type = L_VALUE_ERROR, .value.string_index = interned_index};
}

l_interpreter_t* l_interpreter_create() {
    l_interpreter_t* interpreter = (l_interpreter_t*)malloc(sizeof(l_interpreter_t));
    interpreter->string_table = (l_vector_t*)malloc(sizeof(l_vector_t));
    l_vector_init(interpreter->string_table, sizeof(l_ref_counted_t), 16, (void (*)(void *))l_ref_counted_destroy);
    return interpreter;
}

void l_interpreter_destroy(l_interpreter_t* interpreter) {
    l_vector_destroy(interpreter->string_table);
    free(interpreter->string_table);
    free(interpreter);
}

void l_value_destroy(l_value_t *value) {
    switch(value->type) {
        case L_VALUE_NIL:
        case L_VALUE_BOOL:
        case L_VALUE_CHARACTER:
        case L_VALUE_NUMBER:
            break;
        case L_VALUE_LIST:
            l_vector_destroy(&value->value.list);
            break;
        case L_VALUE_STRING:
            {
                //TODO: dec refcount
            }
            break;
        case L_VALUE_ERROR:
            break;
        case L_VALUE_SYMBOL:
            break;
    }
}

l_value_t l_parse_list(l_tokenizer_t *tokenizer, l_vector_t **string_table) {
    l_value_t value = {.type = L_VALUE_LIST };
    l_vector_init(&value.value.list, sizeof(l_value_t), 4, (void (*)(void *))l_value_destroy);

    l_token_t token = l_tokenizer_next(tokenizer);

    while(token.type != TOKEN_RPAREN) {
        l_value_t expression = l_parse_expression(token, tokenizer, string_table);
        if(expression.type == L_VALUE_ERROR) {
            l_vector_destroy(&value.value.list);
            return expression;
        }
        l_vector_push(&value.value.list, &expression);
        token = l_tokenizer_next(tokenizer);
    }
    return value;
}

l_value_t l_parse_quote(l_tokenizer_t *tokenizer, l_vector_t **string_table) {
    l_value_t value = {.type = L_VALUE_LIST };
    l_vector_init(&value.value.list, sizeof(l_value_t), 2, (void (*)(void *))l_value_destroy);

    l_value_t quote = {.type = L_VALUE_SYMBOL, .value.symbol_index = l_intern_string(string_table, "quote", true)};
    l_vector_push(&value.value.list, &quote);

    l_token_t token = l_tokenizer_next(tokenizer);
    l_value_t expression = l_parse_expression(token, tokenizer, string_table);
    if(expression.type == L_VALUE_ERROR) {
        return expression;
    }
    l_vector_push(&value.value.list, &expression);
    return value;
}

l_value_t l_parse_atom(l_token_t *token, l_vector_t **string_table) {
    switch(token->type) {
        case TOKEN_REAL: {
            l_value_t value = {.type = L_VALUE_NUMBER, .value.double_value = token->value.real, .flags = L_VALUE_FLAG_REAL};
            return value;
        }
        case TOKEN_INTEGER: {
            l_value_t value = {.type = L_VALUE_NUMBER, .value.long_value = token->value.number, .flags = L_VALUE_FLAG_INTEGER};
            return value;
        }
        case TOKEN_STRING: {
            l_value_t value = {.type = L_VALUE_STRING, .value.string_index = l_intern_string(string_table, token->value.string, false)};
            return value;
        }
        case TOKEN_BOOLEAN: {
            l_value_t value = {.type = L_VALUE_BOOL, .value.boolean = token->value.boolean};
            return value;
        }
        case TOKEN_CHARACTER: {
            l_value_t value = {.type = L_VALUE_CHARACTER, .value.character = token->value.character};
            return value;
        }
        case TOKEN_SYMBOL: {
            l_value_t value = {.type = L_VALUE_SYMBOL, .value.symbol_index = l_intern_string(string_table, token->value.symbol, false)};
            return value;
        }
        default: {
            char *error_message;
            asprintf(&error_message, "Error: Cannot convert token to value, type=%d\n", token->type);
            l_value_t value = {.type = L_VALUE_ERROR, .value.string_index = l_intern_string(string_table, error_message, false)};
            return value;
        }

    }
}

size_t l_intern_string(l_vector_t **string_table, const char *string, bool eternal) {
    for(size_t i = 0; i < (*string_table)->length; i++) {
        l_ref_counted_t *ref_counted = (l_ref_counted_t *)l_vector_get(*string_table, i);
        if(strcmp((char *)ref_counted->data, string) == 0) {
            ref_counted->ref_count++;
            return i;
        }
    }
    l_ref_counted_t ref_counted;
    l_ref_counted_init(&ref_counted, (void*)string, eternal ? NULL : (void (*)(void *))free);

    l_vector_push(*string_table, &ref_counted);
    return (*string_table)->length - 1;

}
const char *l_get_interned_string(l_vector_t *string_table, size_t index) {
    l_ref_counted_t *ref_counted = (l_ref_counted_t *)l_vector_get(string_table, index);
    return (const char *)ref_counted->data;
}

void l_ref_counted_init(l_ref_counted_t *ref_counted, void *data, void (*destroy)(void *data)) {
    ref_counted->ref_count = 1;
    ref_counted->data = data;
    ref_counted->destroy = destroy;

}
void l_ref_counted_destroy(l_ref_counted_t *ref_counted) {
    if(ref_counted->destroy != NULL) {
        ref_counted->destroy(ref_counted->data);
    }
    ref_counted->data = NULL;
    ref_counted->destroy = NULL;
    ref_counted->ref_count = 0;
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
        case TOKEN_PLACEHOLDER: {
            printf("PLACEHOLDER: This must not happen!\n");
        } break;
    }
    printf("\n");


}
void l_debug_print_value(l_value_t *value, l_vector_t *string_table) {
    switch(value->type) {
        case L_VALUE_ERROR: {
            printf("ERROR: %s", l_get_interned_string(string_table, value->value.string_index));
        } break;
        case L_VALUE_NUMBER: {
            if(value->flags & L_VALUE_FLAG_INTEGER) {
                printf("%lld", value->value.long_value);
            } else {
                printf("%f", value->value.double_value);
            }
        } break;
        case L_VALUE_STRING: {
            printf("\"%s\"", l_get_interned_string(string_table, value->value.string_index));
        } break;
        case L_VALUE_CHARACTER: {
            printf("\\%c", value->value.character);
        } break;
        case L_VALUE_BOOL: {
            printf("%s", value->value.boolean ? "true" : "false");
        } break;
        case L_VALUE_NIL: {
            printf("nil");
        } break;
        case L_VALUE_SYMBOL: {
            printf("%s", l_get_interned_string(string_table, value->value.symbol_index));
        } break;
        case L_VALUE_LIST: {
            printf("(");
            for(size_t i = 0; i < value->value.list.length; i++) {
                l_value_t *element = (l_value_t *)l_vector_get(&value->value.list, i);
                l_debug_print_value(element, string_table);
                if(i < value->value.list.length - 1) {
                    printf(" ");
                }
            }
            printf(")");
        } break;
    }
}


#endif // L_INTERPRETER_IMPLEMENTATION
#endif // HG_L_INTERPRETER_H
