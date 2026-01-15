#pragma once
/******************************************************************************
 * @file Types2.hpp
 * @brief Main include file for Cryo's new type system
 *
 * This header provides a convenient single include for the entire new type
 * system. Include this file to get access to all type-related functionality.
 *
 * Usage:
 *   #include "Types2/Types2.hpp"
 *
 *   Cryo::TypeArena arena;
 *   Cryo::TypeRef int_type = arena.get_i32();
 *   Cryo::TypeRef ptr_type = arena.get_pointer_to(int_type);
 ******************************************************************************/

// Core type identity
#include "Types2/TypeID.hpp"
#include "Types2/TypeKind.hpp"
#include "Types2/Type.hpp"

// Type arena (ownership and factory)
#include "Types2/TypeArena.hpp"

// Concrete type classes
#include "Types2/PrimitiveTypes.hpp"
#include "Types2/CompoundTypes.hpp"
#include "Types2/UserDefinedTypes.hpp"
#include "Types2/GenericTypes.hpp"
#include "Types2/ErrorType.hpp"

// Resolution infrastructure (Phase 2)
#include "Types2/ModuleTypeRegistry.hpp"
#include "Types2/GenericRegistry.hpp"
#include "Types2/TypeResolver.hpp"
