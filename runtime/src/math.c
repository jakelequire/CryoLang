#include "../include/cryo_runtime.h"
#include <math.h>

/*******************************************************************************
 * Math Functions Implementation
 *
 * Provides mathematical functions for CryoLang programs including basic
 * arithmetic, trigonometry, logarithms, and rounding operations.
 ******************************************************************************/

/*==============================================================================
 * BASIC MATH OPERATIONS
 *============================================================================*/

cryo_float cryo_abs(cryo_float x)
{
    return fabsf(x);
}

cryo_float cryo_pow(cryo_float base, cryo_float exponent)
{
    return powf(base, exponent);
}

cryo_float cryo_sqrt(cryo_float x)
{
    return sqrtf(x);
}

/*==============================================================================
 * TRIGONOMETRIC FUNCTIONS
 *============================================================================*/

cryo_float cryo_sin(cryo_float x)
{
    return sinf(x);
}

cryo_float cryo_cos(cryo_float x)
{
    return cosf(x);
}

cryo_float cryo_tan(cryo_float x)
{
    return tanf(x);
}

/*==============================================================================
 * LOGARITHMIC FUNCTIONS
 *============================================================================*/

cryo_float cryo_log(cryo_float x)
{
    return logf(x); // Natural logarithm
}

cryo_float cryo_log10(cryo_float x)
{
    return log10f(x); // Base-10 logarithm
}

/*==============================================================================
 * ROUNDING FUNCTIONS
 *============================================================================*/

cryo_float cryo_round(cryo_float x)
{
    return roundf(x);
}

cryo_int cryo_floor(cryo_float x)
{
    return (cryo_int)floorf(x);
}

cryo_int cryo_ceil(cryo_float x)
{
    return (cryo_int)ceilf(x);
}

/*==============================================================================
 * MIN/MAX FUNCTIONS
 *============================================================================*/

cryo_int cryo_min_int(cryo_int a, cryo_int b)
{
    return (a < b) ? a : b;
}

cryo_int cryo_max_int(cryo_int a, cryo_int b)
{
    return (a > b) ? a : b;
}

cryo_float cryo_min_float(cryo_float a, cryo_float b)
{
    return (a < b) ? a : b;
}

cryo_float cryo_max_float(cryo_float a, cryo_float b)
{
    return (a > b) ? a : b;
}