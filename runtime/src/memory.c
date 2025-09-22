#include "../include/cryo_runtime.h"
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * Memory Management Functions Implementation
 *
 * Provides memory management utilities for CryoLang programs including
 * string creation/destruction and future Result<T, E> error handling.
 ******************************************************************************/

/*==============================================================================
 * STRING MEMORY MANAGEMENT
 *============================================================================*/

cryo_string cryo_string_create(const char *str)
{
    if (str == NULL)
    {
        return NULL;
    }

    size_t len = strlen(str);
    char *new_str = malloc(len + 1);
    if (new_str == NULL)
    {
        return NULL;
    }

    strcpy(new_str, str);
    return new_str;
}

void cryo_string_destroy(cryo_string str)
{
    if (str != NULL)
    {
        free(str);
    }
}

cryo_string cryo_string_copy(const cryo_string str)
{
    return cryo_string_create(str);
}

/*==============================================================================
 * ERROR HANDLING (Future Result<T, E> Support)
 *============================================================================*/

cryo_result cryo_try_read_file(const cryo_string filename)
{
    cryo_result result;

    if (filename == NULL)
    {
        result.is_ok = false;
        result.error_message = cryo_string_create("Filename cannot be null");
        return result;
    }

    cryo_string content = cryo_read_file(filename);
    if (content != NULL)
    {
        result.is_ok = true;
        result.ok_value = content;
    }
    else
    {
        result.is_ok = false;
        result.error_message = cryo_string_create("Failed to read file");
    }

    return result;
}