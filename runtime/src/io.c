#include "../include/cryo_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * I/O Functions Implementation
 *
 * Provides basic input/output functionality for CryoLang programs including
 * console output, file operations, and input handling.
 ******************************************************************************/

/*==============================================================================
 * CONSOLE OUTPUT FUNCTIONS
 *============================================================================*/

void cryo_print(const cryo_string str)
{
    if (str != NULL)
    {
        printf("%s", str);
        fflush(stdout); // Ensure immediate output
    }
}

void cryo_println(const cryo_string str)
{
    if (str != NULL)
    {
        printf("%s\n", str);
    }
    else
    {
        printf("\n");
    }
    fflush(stdout);
}

void cryo_print_int(cryo_int value)
{
    printf("%d", value);
    fflush(stdout);
}

void cryo_print_float(cryo_float value)
{
    printf("%.6g", value); // Use %g for cleaner float output
    fflush(stdout);
}

void cryo_print_bool(cryo_bool value)
{
    printf("%s", value ? "true" : "false");
    fflush(stdout);
}

void cryo_print_char(cryo_char value)
{
    printf("%c", value);
    fflush(stdout);
}

/*==============================================================================
 * FILE OPERATIONS
 *============================================================================*/

cryo_string cryo_read_file(const cryo_string filename)
{
    if (filename == NULL)
    {
        return NULL;
    }

    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        return NULL; // File couldn't be opened
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size < 0)
    {
        fclose(file);
        return NULL;
    }

    // Allocate buffer (+1 for null terminator)
    char *content = malloc((size_t)file_size + 1);
    if (content == NULL)
    {
        fclose(file);
        return NULL; // Memory allocation failed
    }

    // Read file content
    size_t bytes_read = fread(content, 1, (size_t)file_size, file);
    content[bytes_read] = '\0'; // Null terminate

    fclose(file);
    return content;
}

cryo_bool cryo_write_file(const cryo_string filename, const cryo_string content)
{
    if (filename == NULL || content == NULL)
    {
        return false;
    }

    FILE *file = fopen(filename, "w");
    if (file == NULL)
    {
        return false; // File couldn't be opened
    }

    size_t content_length = strlen(content);
    size_t bytes_written = fwrite(content, 1, content_length, file);

    fclose(file);
    return bytes_written == content_length;
}

cryo_bool cryo_file_exists(const cryo_string filename)
{
    if (filename == NULL)
    {
        return false;
    }

    FILE *file = fopen(filename, "r");
    if (file != NULL)
    {
        fclose(file);
        return true;
    }
    return false;
}

/*==============================================================================
 * INPUT FUNCTIONS (Future Implementation)
 *============================================================================*/

cryo_string cryo_read_line(void)
{
    static char buffer[4096]; // Static buffer for simplicity

    if (fgets(buffer, sizeof(buffer), stdin) != NULL)
    {
        // Remove trailing newline if present
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
        {
            buffer[len - 1] = '\0';
        }

        // Return a copy of the buffer
        char *result = malloc(strlen(buffer) + 1);
        if (result != NULL)
        {
            strcpy(result, buffer);
        }
        return result;
    }

    return NULL; // EOF or error
}