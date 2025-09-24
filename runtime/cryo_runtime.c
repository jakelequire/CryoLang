#include <stdio.h>
#include <stdbool.h>

// CryoLang Runtime Library Implementation
// These functions correspond to the declarations in runtime.cryo

void print_int(int value)
{
    printf("%d\n", value);
}

void print_float(float value)
{
    printf("%f\n", value);
}

void print_bool(bool value)
{
    printf("%s\n", value ? "true" : "false");
}

void print_char(char value)
{
    printf("%c\n", value);
}

void print(const char *str)
{
    printf("%s", str);
}

void println(const char *str)
{
    printf("%s\n", str);
}
