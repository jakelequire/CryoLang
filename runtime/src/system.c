#include "../include/cryo_runtime.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

/*******************************************************************************
 * System Functions Implementation
 *
 * Provides system-level utilities for CryoLang programs including time
 * operations, random number generation, and platform-specific functions.
 ******************************************************************************/

// Global state for random number generation
static bool random_initialized = false;

/*==============================================================================
 * TIME FUNCTIONS
 *============================================================================*/

cryo_int cryo_current_time_millis(void)
{
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // Convert to milliseconds (Windows FILETIME is in 100ns units)
    return (cryo_int)(uli.QuadPart / 10000);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (cryo_int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

cryo_string cryo_current_date(void)
{
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);

    if (local_time == NULL)
    {
        return NULL;
    }

    char *date_str = malloc(11); // YYYY-MM-DD + null terminator
    if (date_str == NULL)
    {
        return NULL;
    }

    strftime(date_str, 11, "%Y-%m-%d", local_time);
    return date_str;
}

cryo_string cryo_current_time(void)
{
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);

    if (local_time == NULL)
    {
        return NULL;
    }

    char *time_str = malloc(9); // HH:MM:SS + null terminator
    if (time_str == NULL)
    {
        return NULL;
    }

    strftime(time_str, 9, "%H:%M:%S", local_time);
    return time_str;
}

void cryo_sleep_millis(cryo_int milliseconds)
{
    if (milliseconds <= 0)
    {
        return;
    }

#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    usleep((useconds_t)(milliseconds * 1000)); // usleep takes microseconds
#endif
}

/*==============================================================================
 * RANDOM FUNCTIONS
 *============================================================================*/

static void ensure_random_initialized(void)
{
    if (!random_initialized)
    {
        srand((unsigned int)time(NULL));
        random_initialized = true;
    }
}

cryo_int cryo_random_int(cryo_int min, cryo_int max)
{
    ensure_random_initialized();

    if (min > max)
    {
        cryo_int temp = min;
        min = max;
        max = temp;
    }

    if (min == max)
    {
        return min;
    }

    // Generate random number in range [min, max]
    cryo_int range = max - min + 1;
    return min + (rand() % range);
}

cryo_float cryo_random_float(cryo_float min, cryo_float max)
{
    ensure_random_initialized();

    if (min > max)
    {
        cryo_float temp = min;
        min = max;
        max = temp;
    }

    if (min == max)
    {
        return min;
    }

    // Generate random float in range [min, max)
    cryo_float random_01 = (cryo_float)rand() / (cryo_float)RAND_MAX;
    return min + random_01 * (max - min);
}

cryo_bool cryo_random_bool(void)
{
    ensure_random_initialized();
    return (rand() % 2) == 1;
}

void cryo_set_random_seed(cryo_int seed)
{
    srand((unsigned int)seed);
    random_initialized = true;
}

/*==============================================================================
 * TYPE CONVERSION FUNCTIONS
 *============================================================================*/

cryo_string cryo_int_to_string(cryo_int value)
{
    // Allocate enough space for the string representation
    // int32_t can be at most 11 characters including sign and null terminator
    char *result = malloc(12);
    if (result == NULL)
    {
        return NULL;
    }

    snprintf(result, 12, "%d", value);
    return result;
}

cryo_string cryo_float_to_string(cryo_float value)
{
    // Allocate space for float representation (should be enough for most cases)
    char *result = malloc(32);
    if (result == NULL)
    {
        return NULL;
    }

    snprintf(result, 32, "%.6g", value); // Use %g for cleaner output
    return result;
}

cryo_string cryo_bool_to_string(cryo_bool value)
{
    const char *str_value = value ? "true" : "false";
    size_t len = strlen(str_value);

    char *result = malloc(len + 1);
    if (result == NULL)
    {
        return NULL;
    }

    strcpy(result, str_value);
    return result;
}

cryo_string cryo_char_to_string(cryo_char value)
{
    char *result = malloc(2); // One character + null terminator
    if (result == NULL)
    {
        return NULL;
    }

    result[0] = value;
    result[1] = '\0';
    return result;
}

cryo_int cryo_string_to_int(const cryo_string str)
{
    if (str == NULL)
    {
        return 0;
    }
    return (cryo_int)strtol(str, NULL, 10);
}

cryo_float cryo_string_to_float(const cryo_string str)
{
    if (str == NULL)
    {
        return 0.0f;
    }
    return strtof(str, NULL);
}

cryo_bool cryo_string_to_bool(const cryo_string str)
{
    if (str == NULL)
    {
        return false;
    }

    // Case-insensitive comparison
    if (strcmp(str, "true") == 0 || strcmp(str, "True") == 0 || strcmp(str, "TRUE") == 0)
    {
        return true;
    }
    if (strcmp(str, "1") == 0)
    {
        return true;
    }

    return false; // Default to false for everything else
}

cryo_float cryo_int_to_float(cryo_int value)
{
    return (cryo_float)value;
}

cryo_int cryo_float_to_int(cryo_float value)
{
    return (cryo_int)value; // Truncation
}