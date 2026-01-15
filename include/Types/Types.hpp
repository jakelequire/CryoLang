#pragma once
/******************************************************************************
 * @file Types.hpp
 * @brief Main include file for Cryo's new type system
 *
 * This header provides a convenient single include for the entire new type
 * system. Include this file to get access to all type-related functionality.
 *
 * Usage:
 *   #include "Types/Types.hpp"
 *
 *   Cryo::TypeArena arena;
 *   Cryo::TypeRef int_type = arena.get_i32();
 *   Cryo::TypeRef ptr_type = arena.get_pointer_to(int_type);
 ******************************************************************************/

// Core type identity
#include "Types/TypeID.hpp"
#include "Types/TypeKind.hpp"
#include "Types/Type.hpp"

// Type arena (ownership and factory)
#include "Types/TypeArena.hpp"

// Concrete type classes
#include "Types/PrimitiveTypes.hpp"
#include "Types/CompoundTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/ErrorType.hpp"

// Resolution infrastructure (Phase 2)
#include "Types/ModuleTypeRegistry.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/TypeResolver.hpp"

// Codegen bridge (Phase 3)
#include "Types/TypeMapper.hpp"

// Type checking (Phase 4)
#include "Types/TypeChecker.hpp"

// Symbol table (Phase 5)
#include "Types/SymbolTable.hpp"

// Monomorphization (Phase 6)
#include "Types/Monomorphizer.hpp"
