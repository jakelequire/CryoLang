#include "../include/cryo_runtime.h"
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * Collections Functions Implementation
 *
 * Provides dynamic array and collection utilities for CryoLang programs
 * including array creation, manipulation, and specialized operations.
 ******************************************************************************/

/*==============================================================================
 * ARRAY CREATION AND DESTRUCTION
 *============================================================================*/

cryo_array cryo_array_create(size_t element_size, size_t initial_capacity)
{
    cryo_array arr;
    arr.element_size = element_size;
    arr.length = 0;
    arr.capacity = initial_capacity;

    if (initial_capacity > 0)
    {
        arr.data = malloc(element_size * initial_capacity);
        if (arr.data == NULL)
        {
            arr.capacity = 0;
        }
    }
    else
    {
        arr.data = NULL;
    }

    return arr;
}

void cryo_array_destroy(cryo_array *arr)
{
    if (arr != NULL && arr->data != NULL)
    {
        free(arr->data);
        arr->data = NULL;
        arr->length = 0;
        arr->capacity = 0;
    }
}

/*==============================================================================
 * ARRAY UTILITIES
 *============================================================================*/

cryo_int cryo_array_length(const cryo_array *arr)
{
    if (arr == NULL)
    {
        return 0;
    }
    return (cryo_int)arr->length;
}

static bool array_ensure_capacity(cryo_array *arr, size_t required_capacity)
{
    if (arr->capacity >= required_capacity)
    {
        return true;
    }

    // Double capacity or use required capacity, whichever is larger
    size_t new_capacity = arr->capacity * 2;
    if (new_capacity < required_capacity)
    {
        new_capacity = required_capacity;
    }

    void *new_data = realloc(arr->data, arr->element_size * new_capacity);
    if (new_data == NULL)
    {
        return false; // Allocation failed
    }

    arr->data = new_data;
    arr->capacity = new_capacity;
    return true;
}

void cryo_array_push(cryo_array *arr, const void *element)
{
    if (arr == NULL || element == NULL)
    {
        return;
    }

    if (!array_ensure_capacity(arr, arr->length + 1))
    {
        return; // Failed to expand array
    }

    // Copy element to end of array
    char *dest = (char *)arr->data + (arr->length * arr->element_size);
    memcpy(dest, element, arr->element_size);
    arr->length++;
}

void *cryo_array_pop(cryo_array *arr)
{
    if (arr == NULL || arr->length == 0)
    {
        return NULL;
    }

    arr->length--;
    return (char *)arr->data + (arr->length * arr->element_size);
}

void *cryo_array_get(const cryo_array *arr, cryo_int index)
{
    if (arr == NULL || index < 0 || (size_t)index >= arr->length)
    {
        return NULL;
    }

    return (char *)arr->data + ((size_t)index * arr->element_size);
}

void cryo_array_set(cryo_array *arr, cryo_int index, const void *element)
{
    if (arr == NULL || element == NULL || index < 0 || (size_t)index >= arr->length)
    {
        return;
    }

    char *dest = (char *)arr->data + ((size_t)index * arr->element_size);
    memcpy(dest, element, arr->element_size);
}

/*==============================================================================
 * ARRAY OPERATIONS FOR SPECIFIC TYPES
 *============================================================================*/

cryo_int cryo_array_sum_int(const cryo_array *arr)
{
    if (arr == NULL || arr->element_size != sizeof(cryo_int))
    {
        return 0;
    }

    cryo_int sum = 0;
    cryo_int *data = (cryo_int *)arr->data;

    for (size_t i = 0; i < arr->length; i++)
    {
        sum += data[i];
    }

    return sum;
}

cryo_int cryo_array_max_int(const cryo_array *arr)
{
    if (arr == NULL || arr->length == 0 || arr->element_size != sizeof(cryo_int))
    {
        return 0; // Return 0 for empty or invalid arrays
    }

    cryo_int *data = (cryo_int *)arr->data;
    cryo_int max = data[0];

    for (size_t i = 1; i < arr->length; i++)
    {
        if (data[i] > max)
        {
            max = data[i];
        }
    }

    return max;
}

cryo_int cryo_array_min_int(const cryo_array *arr)
{
    if (arr == NULL || arr->length == 0 || arr->element_size != sizeof(cryo_int))
    {
        return 0; // Return 0 for empty or invalid arrays
    }

    cryo_int *data = (cryo_int *)arr->data;
    cryo_int min = data[0];

    for (size_t i = 1; i < arr->length; i++)
    {
        if (data[i] < min)
        {
            min = data[i];
        }
    }

    return min;
}