#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

//#define L_INTERPRETER_IMPLEMENTATION
#include "linterpreter.h"


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

    char *script = malloc(sizeof(char)*(scriptSize + 1));
    if (script == NULL) {
        fprintf(stderr, "Error: Could not allocate memory\n");
        exit(1);
    }
    fread(script, 1, scriptSize, scriptF);
    script[scriptSize] = '\0';

   l_interpreter_t *interpreter = l_interpreter_create(); 
   l_value_t result = l_interpreter_eval(interpreter, script);
   l_debug_print_value(&result);
   l_interpreter_destroy(interpreter);
}
