#include "../include/cryo_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*******************************************************************************
 * String Functions Implementation
 *
 * Provides string manipulation utilities for CryoLang programs including
 * length, substring, searching, case conversion, and splitting/joining.
 ******************************************************************************/

/*==============================================================================
 * STRING PROPERTIES AND MANIPULATION
 *============================================================================*/

cryo_int cryo_string_length(const cryo_string str)
{
    if (str == NULL)
    {
        return 0;
    }
    return (cryo_int)strlen(str);
}

cryo_string cryo_string_to_upper(const cryo_string str)
{
    if (str == NULL)
    {
        return NULL;
    }

    size_t len = strlen(str);
    char *result = malloc(len + 1);
    if (result == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i <= len; i++)
    {
        result[i] = (char)toupper(str[i]);
    }

    return result;
}

cryo_string cryo_string_to_lower(const cryo_string str)
{
    if (str == NULL)
    {
        return NULL;
    }

    size_t len = strlen(str);
    char *result = malloc(len + 1);
    if (result == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i <= len; i++)
    {
        result[i] = (char)tolower(str[i]);
    }

    return result;
}

cryo_string cryo_string_substring(const cryo_string str, cryo_int start, cryo_int length)
{
    if (str == NULL || start < 0 || length < 0)
    {
        return NULL;
    }

    size_t str_len = strlen(str);
    if ((size_t)start >= str_len)
    {
        // Start is beyond string length, return empty string
        char *empty = malloc(1);
        if (empty != NULL)
        {
            empty[0] = '\0';
        }
        return empty;
    }

    // Adjust length if it goes beyond string end
    size_t actual_length = (size_t)length;
    if ((size_t)start + actual_length > str_len)
    {
        actual_length = str_len - (size_t)start;
    }

    char *result = malloc(actual_length + 1);
    if (result == NULL)
    {
        return NULL;
    }

    strncpy(result, str + start, actual_length);
    result[actual_length] = '\0';

    return result;
}

/*==============================================================================
 * STRING SEARCHING AND TESTING
 *============================================================================*/

cryo_int cryo_string_find(const cryo_string str, const cryo_string substr)
{
    if (str == NULL || substr == NULL)
    {
        return -1;
    }

    char *found = strstr(str, substr);
    if (found != NULL)
    {
        return (cryo_int)(found - str);
    }

    return -1; // Not found
}

cryo_bool cryo_string_contains(const cryo_string str, const cryo_string substr)
{
    return cryo_string_find(str, substr) != -1;
}

cryo_bool cryo_string_starts_with(const cryo_string str, const cryo_string prefix)
{
    if (str == NULL || prefix == NULL)
    {
        return false;
    }

    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);

    if (prefix_len > str_len)
    {
        return false;
    }

    return strncmp(str, prefix, prefix_len) == 0;
}

cryo_bool cryo_string_ends_with(const cryo_string str, const cryo_string suffix)
{
    if (str == NULL || suffix == NULL)
    {
        return false;
    }

    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len)
    {
        return false;
    }

    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/*==============================================================================
 * STRING SPLITTING AND JOINING
 *============================================================================*/

cryo_array cryo_string_split(const cryo_string str, const cryo_string delimiter)
{
    cryo_array result = cryo_array_create(sizeof(cryo_string), 8);

    if (str == NULL || delimiter == NULL)
    {
        return result;
    }

    // Make a copy of the string since strtok modifies it
    char *str_copy = malloc(strlen(str) + 1);
    if (str_copy == NULL)
    {
        return result;
    }
    strcpy(str_copy, str);

    char *token = strtok(str_copy, delimiter);
    while (token != NULL)
    {
        // Create a copy of the token
        char *token_copy = malloc(strlen(token) + 1);
        if (token_copy != NULL)
        {
            strcpy(token_copy, token);
            cryo_array_push(&result, &token_copy);
        }
        token = strtok(NULL, delimiter);
    }

    free(str_copy);
    return result;
}

cryo_string cryo_string_join(const cryo_array string_array, const cryo_string delimiter)
{
    if (string_array.data == NULL || delimiter == NULL)
    {
        return NULL;
    }

    if (string_array.length == 0)
    {
        char *empty = malloc(1);
        if (empty != NULL)
        {
            empty[0] = '\0';
        }
        return empty;
    }

    // Calculate total length needed
    size_t total_length = 0;
    size_t delimiter_len = strlen(delimiter);
    cryo_string *strings = (cryo_string *)string_array.data;

    for (size_t i = 0; i < string_array.length; i++)
    {
        if (strings[i] != NULL)
        {
            total_length += strlen(strings[i]);
        }
        if (i < string_array.length - 1)
        {
            total_length += delimiter_len;
        }
    }

    // Allocate result buffer
    char *result = malloc(total_length + 1);
    if (result == NULL)
    {
        return NULL;
    }

    result[0] = '\0'; // Start with empty string

    // Join strings
    for (size_t i = 0; i < string_array.length; i++)
    {
        if (strings[i] != NULL)
        {
            strcat(result, strings[i]);
        }
        if (i < string_array.length - 1)
        {
            strcat(result, delimiter);
        }
    }

    return result;
}