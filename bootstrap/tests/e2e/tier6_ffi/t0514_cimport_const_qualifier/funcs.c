#include "funcs.h"
#include <stdio.h>
#include <string.h>

int string_length(const char *s) { return (int)strlen(s); }
void print_message(const char *msg) { printf("%s\n", msg); }
const char *get_greeting(void) { return "greetings"; }
