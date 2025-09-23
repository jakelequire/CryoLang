#ifndef CRYO_RUNTIME_H
#define CRYO_RUNTIME_H

#include <stdbool.h>

// CryoLang Runtime Library Header
// Function declarations for the CryoLang runtime library

#ifdef __cplusplus
extern "C"
{
#endif

    // Basic I/O functions
    void print_int(int value);
    void print_float(float value);
    void print_bool(bool value);
    void print_char(char value);
    void print(const char *str);
    void println(const char *str);

#ifdef __cplusplus
}
#endif

#endif // CRYO_RUNTIME_H