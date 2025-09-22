#ifndef CRYO_RUNTIME_H
#define CRYO_RUNTIME_H

/*******************************************************************************
 * CryoLang Runtime Library
 *
 * This header defines the external C interface for CryoLang's standard library.
 * All functions are exported with C linkage for easy integration with the
 * compiler's LLVM backend (when implemented).
 *
 * Organization:
 * - I/O functions (print, file operations)
 * - String manipulation (length, substring, etc.)
 * - Math functions (trigonometry, rounding, etc.)
 * - Collection utilities (array operations)
 * - System functions (time, random, etc.)
 * - Type conversion utilities
 ******************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>  // size_t
#include <stdbool.h> // bool
#include <stdint.h>  // int32_t, etc.

    /*==============================================================================
     * TYPE DEFINITIONS
     *============================================================================*/

    // CryoLang primitive types mapped to C types
    typedef int32_t cryo_int;
    typedef float cryo_float;
    typedef bool cryo_bool;
    typedef char cryo_char;
    typedef char *cryo_string;

    // Array type for dynamic arrays
    typedef struct
    {
        void *data;
        size_t length;
        size_t capacity;
        size_t element_size;
    } cryo_array;

    /*==============================================================================
     * I/O FUNCTIONS
     *============================================================================*/

    // Basic output functions
    void cryo_print(const cryo_string str);
    void cryo_println(const cryo_string str);
    void cryo_print_int(cryo_int value);
    void cryo_print_float(cryo_float value);
    void cryo_print_bool(cryo_bool value);
    void cryo_print_char(cryo_char value);

    // File operations
    cryo_string cryo_read_file(const cryo_string filename);
    cryo_bool cryo_write_file(const cryo_string filename, const cryo_string content);
    cryo_bool cryo_file_exists(const cryo_string filename);

    // Input functions (for future implementation)
    cryo_string cryo_read_line(void);

    /*==============================================================================
     * STRING FUNCTIONS
     *============================================================================*/

    // String properties and manipulation
    cryo_int cryo_string_length(const cryo_string str);
    cryo_string cryo_string_to_upper(const cryo_string str);
    cryo_string cryo_string_to_lower(const cryo_string str);
    cryo_string cryo_string_substring(const cryo_string str, cryo_int start, cryo_int length);

    // String searching and testing
    cryo_int cryo_string_find(const cryo_string str, const cryo_string substr);
    cryo_bool cryo_string_contains(const cryo_string str, const cryo_string substr);
    cryo_bool cryo_string_starts_with(const cryo_string str, const cryo_string prefix);
    cryo_bool cryo_string_ends_with(const cryo_string str, const cryo_string suffix);

    // String splitting and joining
    cryo_array cryo_string_split(const cryo_string str, const cryo_string delimiter);
    cryo_string cryo_string_join(const cryo_array string_array, const cryo_string delimiter);

    /*==============================================================================
     * MATH FUNCTIONS
     *============================================================================*/

    // Basic math operations
    cryo_float cryo_abs(cryo_float x);
    cryo_float cryo_pow(cryo_float base, cryo_float exponent);
    cryo_float cryo_sqrt(cryo_float x);

    // Trigonometric functions
    cryo_float cryo_sin(cryo_float x);
    cryo_float cryo_cos(cryo_float x);
    cryo_float cryo_tan(cryo_float x);

    // Logarithmic functions
    cryo_float cryo_log(cryo_float x);   // Natural logarithm
    cryo_float cryo_log10(cryo_float x); // Base-10 logarithm

    // Rounding functions
    cryo_float cryo_round(cryo_float x);
    cryo_int cryo_floor(cryo_float x);
    cryo_int cryo_ceil(cryo_float x);

    // Min/max functions
    cryo_int cryo_min_int(cryo_int a, cryo_int b);
    cryo_int cryo_max_int(cryo_int a, cryo_int b);
    cryo_float cryo_min_float(cryo_float a, cryo_float b);
    cryo_float cryo_max_float(cryo_float a, cryo_float b);

    /*==============================================================================
     * ARRAY/COLLECTION FUNCTIONS
     *============================================================================*/

    // Array utilities
    cryo_int cryo_array_length(const cryo_array *arr);
    void cryo_array_push(cryo_array *arr, const void *element);
    void *cryo_array_pop(cryo_array *arr);
    void *cryo_array_get(const cryo_array *arr, cryo_int index);
    void cryo_array_set(cryo_array *arr, cryo_int index, const void *element);

    // Array operations for specific types
    cryo_int cryo_array_sum_int(const cryo_array *arr);
    cryo_int cryo_array_max_int(const cryo_array *arr);
    cryo_int cryo_array_min_int(const cryo_array *arr);

    // Array creation and destruction
    cryo_array cryo_array_create(size_t element_size, size_t initial_capacity);
    void cryo_array_destroy(cryo_array *arr);

    /*==============================================================================
     * TYPE CONVERSION FUNCTIONS
     *============================================================================*/

    // To string conversions
    cryo_string cryo_int_to_string(cryo_int value);
    cryo_string cryo_float_to_string(cryo_float value);
    cryo_string cryo_bool_to_string(cryo_bool value);
    cryo_string cryo_char_to_string(cryo_char value);

    // From string conversions
    cryo_int cryo_string_to_int(const cryo_string str);
    cryo_float cryo_string_to_float(const cryo_string str);
    cryo_bool cryo_string_to_bool(const cryo_string str);

    // Type casting
    cryo_float cryo_int_to_float(cryo_int value);
    cryo_int cryo_float_to_int(cryo_float value);

    /*==============================================================================
     * SYSTEM FUNCTIONS
     *============================================================================*/

    // Time functions
    cryo_int cryo_current_time_millis(void);
    cryo_string cryo_current_date(void);
    cryo_string cryo_current_time(void);
    void cryo_sleep_millis(cryo_int milliseconds);

    // Random functions
    cryo_int cryo_random_int(cryo_int min, cryo_int max);
    cryo_float cryo_random_float(cryo_float min, cryo_float max);
    cryo_bool cryo_random_bool(void);
    void cryo_set_random_seed(cryo_int seed);

    /*==============================================================================
     * MEMORY MANAGEMENT
     *============================================================================*/

    // String memory management
    cryo_string cryo_string_create(const char *str);
    void cryo_string_destroy(cryo_string str);
    cryo_string cryo_string_copy(const cryo_string str);

    /*==============================================================================
     * ERROR HANDLING (Future)
     *============================================================================*/

    // Error result type (for when Result<T, E> is implemented)
    typedef struct
    {
        bool is_ok;
        union
        {
            void *ok_value;
            cryo_string error_message;
        };
    } cryo_result;

    // File operations returning results
    cryo_result cryo_try_read_file(const cryo_string filename);

#ifdef __cplusplus
}
#endif

#endif // CRYO_RUNTIME_H