/*******************************************************************************
 * CryoLang Runtime Library - Main Implementation File
 *
 * This file includes all runtime modules to create a single compilation unit
 * for easier linking with the CryoLang compiler.
 *
 * Individual modules:
 * - io.c: I/O functions (print, file operations)
 * - string.c: String manipulation functions
 * - math.c: Mathematical functions
 * - collections.c: Array and collection utilities
 * - system.c: System functions (time, random, type conversion)
 * - memory.c: Memory management utilities
 ******************************************************************************/

#include "include/cryo_runtime.h"

// Include all module implementations
#include "src/io.c"
#include "src/string.c"
#include "src/math.c"
#include "src/collections.c"
#include "src/system.c"
#include "src/memory.c"