#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

#define MAX_TOKEN_LENGTH 4096

void *MALLOC(size_t size) {
    void *ptr = malloc(size);
    if(ptr == NULL) {
        perror("malloc");
        exit(1);
    }
    return ptr;
}

typedef enum TokenType {
    LPAREN,
    RPAREN,
    QUOTE,
    SYMBOL,
    INTEGER,
    REAL,
    STRING,
    BOOLEAN,
    CHARACTER,
    END_OF_FILE
} TokenType;


typedef struct Token {
    TokenType type;
    size_t id;
    union value{
        long number;
        double real;
        bool boolean;
        char character;
        char *string;
        char *symbol;
    } value;
} Token;

typedef struct SourceLocation {
    size_t line;
    size_t column;
    size_t offset;
} SourceLocation;

typedef struct SourceRange {
    SourceLocation start;
    SourceLocation end;
} SourceRange;

typedef struct Vector {
    size_t elementSize;
    size_t length;
    size_t capacity;
    char *data;
} Vector;

Vector *vec_create(size_t elementSize, size_t capacity) {

    Vector *vec = MALLOC(sizeof(Vector));
    vec->elementSize = elementSize;
    vec->length = 0;
    vec->capacity = capacity;
    if(capacity > 0) {
        vec->data = malloc(elementSize * capacity);
        if(vec->data == NULL) {
            perror("malloc");
            free(vec);
            exit(1);
        }
    } else
        vec->data = NULL;
    return vec;
}

Vector *vec_push(Vector *vec, void *element) {
    if(vec->length == vec->capacity) {
        vec->capacity = vec->capacity * 2;
        vec->data = realloc(vec->data, vec->capacity * vec->elementSize);
        if(vec->data == NULL) {
            perror("realloc");
            exit(1);
        }
    }
    memcpy(vec->data + vec->length * vec->elementSize, element, vec->elementSize);
    vec->length++;
    return vec;
}

void vec_free(Vector *vec) {
    free(vec->data);
    free(vec);
}

void *vec_get(Vector *vec, size_t index) {
    if(index >= vec->length) {
        fprintf(stderr, "Error: Index out of bounds\n");
        exit(1);
    }
    return vec->data + index * vec->elementSize;
}

void dump_tokens(Vector *tokenStream, Vector *sourceRanges) {
    printf("Dumping %ld tokens\n", tokenStream->length);
    for(size_t i = 0; i < tokenStream->length; i++) {
        Token *token = vec_get(tokenStream, i);
        printf("%ld: Token %ld: ", i, token->id);
        switch (token->type) {
            case LPAREN:
                printf("LPAREN");
                break;
            case RPAREN:
                printf("RPAREN");
                break;
            case QUOTE:
                printf("QUOTE");
                break;
            case SYMBOL:
                printf("SYMBOL %s", token->value.string);
                break;
            case INTEGER:
                printf("INTEGER %ld", token->value.number);
                break;
            case REAL:
                printf("REAL %f", token->value.real);
                break;
            case STRING:
                printf("STRING %s", token->value.string);
                break;
            case BOOLEAN:
                printf("BOOLEAN %s", token->value.boolean ? "#t" : "#f");
                break;
            case CHARACTER:
                printf("CHARACTER %c", token->value.character);
                break;
            case END_OF_FILE:
                printf("END_OF_FILE");
                break;
        }
        SourceRange *range = vec_get(sourceRanges, i);
        printf(" @ %ld:%ld-%ld:%ld\n", range->start.line, range->start.column, range->end.line, range->end.column);
    }
}

bool isSymbol(char c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c == '+' || c == '-' || c == '*' || c == '/' || 
             c == '=' || c == '<' ||c == '>' || c == '?' || c == '!'));
}
bool tokenize(size_t scriptSize, char *script, Vector **out_tokenStream, Vector **out_sourceRanges) {
    //TODO: Arena allocation

    Vector *tokenStream = vec_create(sizeof(Token), 16);
    Vector *sourceRanges = vec_create(sizeof(SourceRange), 16);

    char buffer[MAX_TOKEN_LENGTH];

    size_t tokenID = 0; //monotonically increasing token ID
    size_t index = 0;

    size_t line = 1;
    size_t column = 1;

    size_t currentTokenLength = 0;

    enum STATE {
        PARSE_START,
        PARSE_SYMBOL,
        PARSE_INTEGER,
        PARSE_HEX_INTEGER,
        PARSE_BINARY_INTEGER,
        PARSE_OCTAL_INTEGER,
        PARSE_REAL,
        PARSE_STRING,
        PARSE_BOOLEAN,
        PARSE_CHARACTER,
    } state = PARSE_START;

#define PUSH_TOKEN(T)                                                           \
    vec_push(tokenStream, &T);                                                  \
    SourceRange range = {                                                       \
        .start = {                                                              \
            .line = line,                                                       \
            .column = column - currentTokenLength,                              \
            .offset = index-currentTokenLength},                                \
        .end = {                                                                \
            .line = line,                                                       \
            .column = column,                                                   \
            .offset = index}};                                                  \
    vec_push(sourceRanges, &range);                                             \
    currentTokenLength = 0;                                                     \
    state = PARSE_START;


#define TOKENIZE_ERROR(...)                                                     \
    do {                                                                        \
        fprintf(stderr, "Error: Tokenizer failed while state=%d @ [%zu:%zu (idx=%zu)]:\n    ",    \
                state, line, column, index);                                    \
        fprintf(stderr, __VA_ARGS__);                                           \
        return false;                                                           \
    } while (0)


#define VERIFY_ERRNO(TYPE)                                                      \
    if(errno == ERANGE) {                                                       \
        fprintf(stderr, "Error: " #TYPE " literal out of range at %ld:%ld\n", line, column); \
        return false;                                                           \
    } else if(errno) {                                                          \
        perror("unknown parsing error");                                        \
        return false;                                                           \
    }


    while(index < scriptSize) {
        if(script[index] == 0) {
            break;
        }
        switch(state) {
            case PARSE_START:
                {
                    if(script[index] == '-' && script[index + 1] == '-') {
                        //skip comments
                        while(script[index] != '\n' && script[index] != 0 && index < scriptSize) {
                            index++;
                        }
                        line++;
                        column = 1;
                    }else if(script[index] == '\n') {
                        index++;
                        line++;
                        column = 1;
                    }else if(script[index] == '(') {
                        Token token = {.type = LPAREN, .id = tokenID++, .value.number = 0};
                        PUSH_TOKEN(token);
                        index++;
                        column++;
                    } else if(script[index] == ')') {
                        Token token = {.type = RPAREN, .id = tokenID++, .value.number = 0};
                        PUSH_TOKEN(token);
                        index++;
                        column++;
                    } else if(script[index] == '"') {
                        state = PARSE_STRING;
                        index++;
                        column++;
                    } else if(script[index] == '#') {
                        state = PARSE_BOOLEAN;
                        index++;
                        column++;
                    } else if(script[index] == '\'') {
                        Token token = {.type = QUOTE, .id = tokenID++, .value.number = 0};
                        PUSH_TOKEN(token);
                        index++;
                        column++;
                    } else if(script[index] == '\\') {
                        state = PARSE_CHARACTER;
                        index++;
                        column++;
                    } else if(isspace(script[index])) {
                        index++;
                        column++;
                    } else if(index + 1 < scriptSize && script[index] == '0' && script[index + 1] == 'x') {
                        index += 2;
                        column += 2;
                        state = PARSE_HEX_INTEGER;
                    } else if(index + 1 < scriptSize && script[index] == '0' && script[index + 1] == 'o') {
                        index += 2;
                        column += 2;
                        state = PARSE_OCTAL_INTEGER;
                    } else if(index + 1 < scriptSize && script[index] == '0' && script[index + 1] == 'b') {
                        index += 2;
                        column += 2;
                        state = PARSE_BINARY_INTEGER;
                    } else if((script[index] >= '0' && script[index] <= '9') || script[index] == '-') {
                        state = PARSE_INTEGER;
                    } else if(isSymbol(script[index])) {
                        state = PARSE_SYMBOL;
                    } else {
                        TOKENIZE_ERROR("Unexpected character '%c'\n", script[index]);
                    }
                }
                break;
            case PARSE_SYMBOL:
                {
                    if(isSymbol(script[index])) {
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                    } else if(isspace(script[index]) || script[index] == ')') {
                        buffer[currentTokenLength] = '\0';
                        //copy string without strdup
                        char *symbol = MALLOC(sizeof(char)*(currentTokenLength + 1));
                        memcpy(symbol, buffer, currentTokenLength + 1);
                        Token token = {.type = SYMBOL, .id = tokenID++, .value.symbol = symbol};
                        PUSH_TOKEN(token);
                    } else {
                        TOKENIZE_ERROR("Unexpected character '%c' while parsing SYMBOL\n", script[index]);
                    }
                }
                break;
            case PARSE_INTEGER:
                {
                    if(script[index] == '-' && currentTokenLength == 0) {
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                    } else if(script[index] >= '0' && script[index] <= '9') {
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                    } else if (script[index] == '.') {
                        state = PARSE_REAL;
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                    } else if(isspace(script[index]) || script[index] == ')') {
                        buffer[currentTokenLength] = '\0';
                        Token token = {.type = INTEGER, .id = tokenID++, .value.number = strtoll(buffer, NULL, 10)};
                        VERIFY_ERRNO(INTEGER);
                        PUSH_TOKEN(token);
                    } else {
                        TOKENIZE_ERROR("Unexpected character '%c' while parsing INTEGER\n", script[index]);
                    }
                }
                break;
            case PARSE_HEX_INTEGER:
                {
                    if((script[index] >= '0' && script[index] <= '9') ||
                            (script[index] >= 'a' && script[index] <= 'f') ||
                            (script[index] >= 'A' && script[index] <= 'F')) {
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                    } else if(isspace(script[index]) || script[index] == ')') {
                        buffer[currentTokenLength] = '\0';
                        Token token = {.type = INTEGER, .id = tokenID++, .value.number = strtoll(buffer, NULL, 16)};
                        VERIFY_ERRNO(INTEGER);
                        PUSH_TOKEN(token);
                    } else {
                        TOKENIZE_ERROR("Unexpected character '%c' while parsing HEX INTEGER\n", script[index]);
                    }
                }
                break;
            case PARSE_BINARY_INTEGER:
                {
                    if(script[index] == '0' || script[index] == '1') {
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                    } else if(isspace(script[index]) || script[index] == ')') {
                        buffer[currentTokenLength] = '\0';
                        Token token = {.type = INTEGER, .id = tokenID++, .value.number = strtoll(buffer, NULL, 2)};
                        VERIFY_ERRNO(INTEGER);
                        PUSH_TOKEN(token);
                    } else {
                        TOKENIZE_ERROR("Unexpected character '%c' while parsing BINARY INTEGER\n", script[index]);
                    }
                }
                break;
            case PARSE_OCTAL_INTEGER:
                {
                    if(script[index] >= '0' && script[index] <= '7') {
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                    } else if(isspace(script[index]) || script[index] == ')') {
                        buffer[currentTokenLength] = '\0';
                        Token token = {.type = INTEGER, .id = tokenID++, .value.number = strtoll(buffer, NULL, 8)};
                        VERIFY_ERRNO(INTEGER);
                        PUSH_TOKEN(token);
                    } else {
                        TOKENIZE_ERROR("Unexpected character '%c' while parsing OCTAL INTEGER\n", script[index]);
                    }
                }
                break;
            case PARSE_REAL:
                {
                    if(script[index] >= '0' && script[index] <= '9') {
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                        continue;
                    } else if(isspace(script[index]) || script[index] == ')') {
                        buffer[currentTokenLength] = '\0';
                        Token token = {.type = REAL, .id = tokenID++, .value.real = strtold(buffer, NULL)};
                        VERIFY_ERRNO(REAL);
                        PUSH_TOKEN(token);
                    }
                }
                break;
            case PARSE_STRING:
                {
                    if(index + 1 < scriptSize && script[index] == '\\') {
                        index++;
                        buffer[currentTokenLength++] = script[index++];
                        column += 2;
                    } else if(script[index] == '"') {
                        buffer[currentTokenLength] = '\0';
                        char *string = MALLOC(sizeof(char)*(currentTokenLength + 1));
                        memcpy(string, buffer, currentTokenLength + 1);
                        Token token = {.type = STRING, .id = tokenID++, .value.string = string};
                        PUSH_TOKEN(token);
                        index++;
                        column++;
                    } else if(script[index] == '\n') {
                        buffer[currentTokenLength++] = script[index++];
                        column = 1;
                        line++;
                    } else {
                        buffer[currentTokenLength++] = script[index++];
                        column++;
                    }
                } break;

            case PARSE_BOOLEAN:
                {
                    if(script[index] == 'f' || script[index] == 't') {
                        bool value = script[index++] == 't';
                        Token token = {.type = BOOLEAN, .id = tokenID++, .value.boolean = value};
                        PUSH_TOKEN(token);
                    } else {
                        TOKENIZE_ERROR("Unexpected character '%c' while parsing BOOLEAN\n", script[index]);
                    }
                } break;
            case PARSE_CHARACTER:
                {
                    char c;
                    if(index + 1 < scriptSize && script[index] == '\\') {
                        char escaped;
                        switch(script[index+1]){
                            case 'n':
                                escaped = '\n';
                                break;
                            case 't':
                                escaped = '\t';
                                break;
                            case 'r':
                                escaped = '\r';
                                break;
                            case '0':
                                escaped = '\0';
                                break;
                            case '\\':
                                escaped = '\\';
                                break;
                            case '"':
                                escaped = '"';
                                break;
                            case '\'':
                                escaped = '\'';
                                break;
                            case '(':
                                escaped = '(';
                                break;
                            case ')':
                                escaped = ')';
                                break;
                            default:
                                TOKENIZE_ERROR("Unexpected escape character '\\%c' at %ld:%ld\n", script[index+1], line, column);
                        }
                        if(index + 2 < scriptSize && (!isspace(script[index + 2]) && script[index+2] != ')')) {
                            TOKENIZE_ERROR("Unexpected character '%c' after escape character '\\%c' at %ld:%ld\n", script[index+2], script[index+1], line, column);
                        }
                        c = escaped;
                        index += 3;
                        column += 3;
                    } else if(index + 1 < scriptSize && !isspace(script[index+1]) && script[index+1] != ')') {
                        TOKENIZE_ERROR("Unexpected character '%c' after CHARACTER at %ld:%ld\n", script[index+1], line, column);
                    } else {
                        c = script[index];
                        index += 2;
                        column += 2;
                    }
                    Token token = {.type = CHARACTER, .id = tokenID++, .value.character = c};
                    PUSH_TOKEN(token);
                }
                break;
            default:
                TOKENIZE_ERROR("Unhandled state %d\n", state);
        }

    }
    if(state != PARSE_START) {
        TOKENIZE_ERROR("Unexpected end of file while parsing.\n");
    }

    *out_tokenStream = tokenStream;
    *out_sourceRanges = sourceRanges;
    return true;
}

int main(int argc, char **argv) {
    FILE *scriptF;
    if (argc == 2 && strcmp(argv[1], "-") != 0) {
        scriptF = fopen(argv[1], "r");
    } else {
        scriptF = stdin;
    }
    if (scriptF == NULL) {
        fprintf(stderr, "Error: Could not open file\n");
        exit(1);
    }

    //read file into memory (mmap)
    fseek(scriptF, 0, SEEK_END);
    size_t scriptSize = ftell(scriptF);
    fseek(scriptF, 0, SEEK_SET);

    char *script = MALLOC(sizeof(char)*(scriptSize + 1));
    fread(script, 1, scriptSize, scriptF);
    script[scriptSize] = '\0';

    Vector *tokenStream;
    Vector *sourceRanges;
    bool success = tokenize(scriptSize, script, &tokenStream, &sourceRanges);
    if(success)
        dump_tokens(tokenStream, sourceRanges);
}
