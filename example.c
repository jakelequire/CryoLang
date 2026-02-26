#include "./example.h"
#include <stdio.h>


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

void test_c_stdlib() {
    printf("Testing C standard library function: %d\n", 123);
    fread(NULL, 0, 0, NULL); // Just to demonstrate calling a C function.

}