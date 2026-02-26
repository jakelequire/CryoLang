#include "./example.h"

extern int printf(const char* format, ...);

void greet(const char* name) {
    printf("Hello, %s!\n", name);
}

void foobar() {
    printf("foobar called!\n");
}

void foo() {
    printf("foo called!\n");
}