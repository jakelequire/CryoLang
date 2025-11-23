#include "test_utils.hpp"
#include "include/test_helpers.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "Utils/ASTNodeSRMExtensions.hpp"
#include "AST/Type.hpp"
#include "AST/SymbolTable.hpp"
#include <memory>
#include <vector>

using namespace CryoTest;

/**
 * @file symbol_resolution_manager_tests.cpp
 * @brief Comprehensive test suite for the Symbol Resolution Manager (SRM) system
 * 
 * This test suite covers all aspects of the SRM system including:
 * - Core identifier classes (QualifiedIdentifier, FunctionIdentifier, TypeIdentifier)
 * - SymbolResolutionContext functionality
 * - SymbolResolutionManager resolution strategies
 * - AST node integration
 * - SymbolTable integration
 * - Performance and edge cases
 */

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

/**
 * @brief Test helper for SRM-specific testing
 */
class SRMTestHelper : public CompilerTestHelper {
private:
    std::unique_ptr<Cryo::TypeContext> type_context_;
    std::unique_ptr<Cryo::SymbolTable> symbol_table_;
    std::unique_ptr<Cryo::SRM::SymbolResolutionContext> srm_context_;
    std::unique_ptr<Cryo::SRM::SymbolResolutionManager> srm_manager_;
    
public:
    void setup() override {
        CompilerTestHelper::setup();
        
        // Create type context
        type_context_ = std::make_unique<Cryo::TypeContext>();
        
        // Create symbol table with type context
        symbol_table_ = std::make_unique<Cryo::SymbolTable>(nullptr, type_context_.get());
        
        // Create SRM context
        srm_context_ = std::make_unique<Cryo::SRM::SymbolResolutionContext>(
            symbol_table_.get(), type_context_.get());
        
        // Create SRM manager
        srm_manager_ = std::make_unique<Cryo::SRM::SymbolResolutionManager>(srm_context_.get());
    }
    
    Cryo::TypeContext* get_type_context() { return type_context_.get(); }
    Cryo::SymbolTable* get_symbol_table() { return symbol_table_.get(); }
    Cryo::SRM::SymbolResolutionContext* get_srm_context() { return srm_context_.get(); }
    Cryo::SRM::SymbolResolutionManager* get_srm_manager() { return srm_manager_.get(); }
    
    // Helper to create test types
    Cryo::Type* create_int_type() {
        return type_context_->create_primitive_type(Cryo::PrimitiveType::Integer);
    }
    
    Cryo::Type* create_string_type() {
        return type_context_->create_primitive_type(Cryo::PrimitiveType::String);
    }
    
    Cryo::Type* create_void_type() {
        return type_context_->create_primitive_type(Cryo::PrimitiveType::Void);
    }
    
    // Helper to populate symbol table with test symbols
    void add_test_symbols() {
        auto int_type = create_int_type();
        auto string_type = create_string_type();
        auto void_type = create_void_type();
        
        // Add some basic symbols
        symbol_table_->declare_symbol("test_var", Cryo::SymbolKind::Variable, 
                                    Cryo::SourceLocation{}, int_type);
        symbol_table_->declare_symbol("test_func", Cryo::SymbolKind::Function, 
                                    Cryo::SourceLocation{}, void_type);
        symbol_table_->declare_symbol("TestStruct", Cryo::SymbolKind::Type, 
                                    Cryo::SourceLocation{}, nullptr);
    }
};

// ============================================================================
// Core Identifier Tests
// ============================================================================

CRYO_TEST_DESC(SRM_Core, QualifiedIdentifier_Creation, 
              "Tests creation and basic functionality of QualifiedIdentifier") {
    SRMTestHelper helper;
    helper.setup();
    
    // Test simple identifier creation
    auto simple_id = Cryo::SRM::QualifiedIdentifier::create_simple(
        "test_symbol", Cryo::SymbolKind::Variable);
    
    CRYO_ASSERT_NOT_NULL(simple_id.get());
    CRYO_EXPECT_EQ(simple_id->get_symbol_name(), "test_symbol");
    CRYO_EXPECT_EQ(simple_id->to_string(), "test_symbol");
    CRYO_EXPECT_FALSE(simple_id->is_namespaced());
    
    // Test qualified identifier creation
    std::vector<std::string> namespaces = {"std", "collections"};
    auto qualified_id = Cryo::SRM::QualifiedIdentifier::create_qualified(
        namespaces, "Vector", Cryo::SymbolKind::Type);
    
    CRYO_ASSERT_NOT_NULL(qualified_id.get());
    CRYO_EXPECT_EQ(qualified_id->get_symbol_name(), "Vector");
    CRYO_EXPECT_EQ(qualified_id->to_string(), "std::collections::Vector");
    CRYO_EXPECT_TRUE(qualified_id->is_namespaced());
    CRYO_EXPECT_EQ(qualified_id->get_namespace_path(), "std::collections");
}

CRYO_TEST_DESC(SRM_Core, QualifiedIdentifier_Equality, 
              "Tests equality and hashing for QualifiedIdentifier") {
    SRMTestHelper helper;
    helper.setup();
    
    // Create identical identifiers
    auto id1 = Cryo::SRM::QualifiedIdentifier::create_simple(
        "test", Cryo::SymbolKind::Function);
    auto id2 = Cryo::SRM::QualifiedIdentifier::create_simple(
        "test", Cryo::SymbolKind::Function);
    auto id3 = Cryo::SRM::QualifiedIdentifier::create_simple(
        "test", Cryo::SymbolKind::Variable); // Different kind
    
    // Test equality
    CRYO_EXPECT_TRUE(id1->equals(*id2));
    CRYO_EXPECT_FALSE(id1->equals(*id3));
    
    // Test hash consistency
    CRYO_EXPECT_EQ(id1->hash(), id2->hash());
    CRYO_EXPECT_NE(id1->hash(), id3->hash());
}

CRYO_TEST_DESC(SRM_Core, FunctionIdentifier_Creation, 
              "Tests creation and functionality of FunctionIdentifier") {
    SRMTestHelper helper;
    helper.setup();
    
    auto int_type = helper.create_int_type();
    auto string_type = helper.create_string_type();
    
    std::vector<Cryo::Type*> params = {int_type, string_type};
    
    // Test function identifier creation
    auto func_id = std::make_unique<Cryo::SRM::FunctionIdentifier>(
        "test_func", params, helper.create_void_type());
    
    CRYO_ASSERT_NOT_NULL(func_id.get());
    CRYO_EXPECT_EQ(func_id->get_symbol_name(), "test_func");
    CRYO_EXPECT_EQ(func_id->get_parameter_types().size(), 2);
    CRYO_EXPECT_EQ(func_id->get_parameter_types()[0], int_type);
    CRYO_EXPECT_EQ(func_id->get_parameter_types()[1], string_type);
    CRYO_EXPECT_FALSE(func_id->is_constructor());
    CRYO_EXPECT_FALSE(func_id->is_static_method());
}

CRYO_TEST_DESC(SRM_Core, FunctionIdentifier_Signatures, 
              "Tests function signature generation and LLVM naming") {
    SRMTestHelper helper;
    helper.setup();
    
    auto int_type = helper.create_int_type();
    std::vector<Cryo::Type*> params = {int_type};
    
    auto func_id = std::make_unique<Cryo::SRM::FunctionIdentifier>(
        std::vector<std::string>{"math"}, "add", params, int_type);
    
    // Test signature generation
    std::string signature = func_id->to_signature_string();
    CRYO_EXPECT_TRUE(signature.find("(") != std::string::npos);
    CRYO_EXPECT_TRUE(signature.find(")") != std::string::npos);
    
    // Test LLVM name generation
    std::string llvm_name = func_id->to_llvm_name();
    CRYO_EXPECT_FALSE(llvm_name.empty());
    CRYO_EXPECT_TRUE(llvm_name.find("::") == std::string::npos); // Should be escaped
    
    // Test overload key generation
    std::string overload_key = func_id->to_overload_key();
    CRYO_EXPECT_FALSE(overload_key.empty());
    CRYO_EXPECT_TRUE(overload_key.find("math::add") != std::string::npos);
}

CRYO_TEST_DESC(SRM_Core, FunctionIdentifier_Constructors, 
              "Tests constructor-specific functionality") {
    SRMTestHelper helper;
    helper.setup();
    
    auto int_type = helper.create_int_type();
    std::vector<Cryo::Type*> params = {int_type};
    
    // Create constructor identifier
    auto constructor_id = Cryo::SRM::FunctionIdentifier::create_constructor(
        std::vector<std::string>{"geometry"}, "Point", params);
    
    CRYO_ASSERT_NOT_NULL(constructor_id.get());
    CRYO_EXPECT_TRUE(constructor_id->is_constructor());
    CRYO_EXPECT_EQ(constructor_id->get_symbol_name(), "Point");
    
    // Test constructor naming
    std::string constructor_name = constructor_id->to_constructor_name();
    CRYO_EXPECT_TRUE(constructor_name.find("geometry::Point::Point") != std::string::npos);
}

CRYO_TEST_DESC(SRM_Core, TypeIdentifier_Creation, 
              "Tests creation and functionality of TypeIdentifier") {
    SRMTestHelper helper;
    helper.setup();
    
    // Test simple type identifier
    auto type_id = Cryo::SRM::TypeIdentifier::create_simple_type(
        "TestStruct", Cryo::TypeKind::Struct);
    
    CRYO_ASSERT_NOT_NULL(type_id.get());
    CRYO_EXPECT_EQ(type_id->get_symbol_name(), "TestStruct");
    CRYO_EXPECT_EQ(type_id->get_type_kind(), Cryo::TypeKind::Struct);
    CRYO_EXPECT_TRUE(type_id->is_struct());
    CRYO_EXPECT_FALSE(type_id->is_class());
    CRYO_EXPECT_FALSE(type_id->has_template_parameters());
}

CRYO_TEST_DESC(SRM_Core, TypeIdentifier_Templates, 
              "Tests template type identifier functionality") {
    SRMTestHelper helper;
    helper.setup();
    
    auto int_type = helper.create_int_type();
    auto string_type = helper.create_string_type();
    std::vector<Cryo::Type*> template_params = {int_type, string_type};
    
    // Test template type identifier
    auto template_id = Cryo::SRM::TypeIdentifier::create_template_type(
        std::vector<std::string>{"std"}, "Map", Cryo::TypeKind::Class, template_params);
    
    CRYO_ASSERT_NOT_NULL(template_id.get());
    CRYO_EXPECT_TRUE(template_id->has_template_parameters());
    CRYO_EXPECT_TRUE(template_id->is_template_specialization());
    CRYO_EXPECT_EQ(template_id->get_template_parameters().size(), 2);
    
    // Test template name generation
    std::string canonical_name = template_id->to_canonical_name();
    CRYO_EXPECT_TRUE(canonical_name.find("std::Map") != std::string::npos);
    CRYO_EXPECT_TRUE(canonical_name.find("<") != std::string::npos);
    CRYO_EXPECT_TRUE(canonical_name.find(">") != std::string::npos);
}

// ============================================================================
// SymbolResolutionContext Tests
// ============================================================================

CRYO_TEST_DESC(SRM_Context, NamespaceManagement, 
              "Tests namespace push/pop and path generation") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Test initial state
    CRYO_EXPECT_TRUE(context->get_namespace_stack().empty());
    CRYO_EXPECT_EQ(context->get_current_namespace_path(), "");
    
    // Test namespace push
    context->push_namespace("std");
    CRYO_EXPECT_EQ(context->get_namespace_stack().size(), 1);
    CRYO_EXPECT_EQ(context->get_current_namespace_path(), "std");
    CRYO_EXPECT_TRUE(context->is_in_namespace("std"));
    
    // Test nested namespace
    context->push_namespace("collections");
    CRYO_EXPECT_EQ(context->get_namespace_stack().size(), 2);
    CRYO_EXPECT_EQ(context->get_current_namespace_path(), "std::collections");
    
    // Test namespace pop
    context->pop_namespace();
    CRYO_EXPECT_EQ(context->get_namespace_stack().size(), 1);
    CRYO_EXPECT_EQ(context->get_current_namespace_path(), "std");
    
    context->pop_namespace();
    CRYO_EXPECT_TRUE(context->get_namespace_stack().empty());
}

CRYO_TEST_DESC(SRM_Context, NamespaceScope_RAII, 
              "Tests RAII namespace scope management") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Test RAII scope
    {
        Cryo::SRM::NamespaceScope scope(context, "test_namespace");
        CRYO_EXPECT_EQ(context->get_current_namespace_path(), "test_namespace");
    }
    
    // Should be automatically popped
    CRYO_EXPECT_TRUE(context->get_namespace_stack().empty());
}

CRYO_TEST_DESC(SRM_Context, AliasManagement, 
              "Tests namespace alias registration and resolution") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Test alias registration
    context->register_namespace_alias("std", "standard_library");
    CRYO_EXPECT_TRUE(context->has_namespace_alias("std"));
    
    // Test alias resolution
    std::string resolved = context->resolve_namespace_alias("std");
    CRYO_EXPECT_EQ(resolved, "standard_library");
    
    // Test non-existent alias
    std::string unresolved = context->resolve_namespace_alias("nonexistent");
    CRYO_EXPECT_EQ(unresolved, "nonexistent");
}

CRYO_TEST_DESC(SRM_Context, ImportManagement, 
              "Tests imported namespace management") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Test import addition
    context->add_imported_namespace("std::io");
    CRYO_EXPECT_TRUE(context->is_namespace_imported("std::io"));
    
    // Test import list
    auto imported = context->get_imported_namespaces();
    CRYO_EXPECT_TRUE(imported.find("std::io") != imported.end());
    
    // Test import removal
    context->remove_imported_namespace("std::io");
    CRYO_EXPECT_FALSE(context->is_namespace_imported("std::io"));
}

CRYO_TEST_DESC(SRM_Context, IdentifierCreation, 
              "Tests context-based identifier creation helpers") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Set up namespace context
    context->push_namespace("test");
    
    // Test qualified identifier creation
    auto qualified_id = context->create_qualified_identifier("symbol", Cryo::SymbolKind::Variable);
    CRYO_ASSERT_NOT_NULL(qualified_id.get());
    CRYO_EXPECT_EQ(qualified_id->to_string(), "test::symbol");
    
    // Test function identifier creation
    auto int_type = helper.create_int_type();
    std::vector<Cryo::Type*> params = {int_type};
    auto func_id = context->create_function_identifier("func", params);
    CRYO_ASSERT_NOT_NULL(func_id.get());
    CRYO_EXPECT_EQ(func_id->get_symbol_name(), "func");
    
    // Test type identifier creation
    auto type_id = context->create_type_identifier("Type", Cryo::TypeKind::Struct);
    CRYO_ASSERT_NOT_NULL(type_id.get());
    CRYO_EXPECT_EQ(type_id->to_string(), "test::Type");
}

// ============================================================================
// SymbolResolutionManager Tests
// ============================================================================

CRYO_TEST_DESC(SRM_Manager, BasicResolution, 
              "Tests basic symbol resolution functionality") {
    SRMTestHelper helper;
    helper.setup();
    helper.add_test_symbols();
    
    auto manager = helper.get_srm_manager();
    
    // Test simple symbol resolution
    auto simple_id = Cryo::SRM::QualifiedIdentifier::create_simple(
        "test_var", Cryo::SymbolKind::Variable);
    auto result = manager->resolve_symbol(*simple_id);
    
    CRYO_EXPECT_TRUE(result.is_valid());
    CRYO_EXPECT_NOT_NULL(result.symbol);
    CRYO_EXPECT_EQ(result.symbol->name, "test_var");
    CRYO_EXPECT_TRUE(result.is_exact_match);
}

CRYO_TEST_DESC(SRM_Manager, ResolutionStrategies, 
              "Tests different resolution strategies") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    auto manager = helper.get_srm_manager();
    
    // Add a namespaced symbol
    context->push_namespace("test_ns");
    helper.get_symbol_table()->declare_symbol("namespaced_var", Cryo::SymbolKind::Variable, 
                                            Cryo::SourceLocation{}, helper.create_int_type());
    
    // Test namespace-qualified resolution
    auto ns_id = Cryo::SRM::QualifiedIdentifier::create_qualified(
        std::vector<std::string>{"test_ns"}, "namespaced_var", Cryo::SymbolKind::Variable);
    auto result = manager->resolve_symbol(*ns_id);
    
    CRYO_EXPECT_TRUE(result.is_valid());
    CRYO_EXPECT_NOT_NULL(result.symbol);
}

CRYO_TEST_DESC(SRM_Manager, SymbolExistence, 
              "Tests symbol existence checking") {
    SRMTestHelper helper;
    helper.setup();
    helper.add_test_symbols();
    
    auto manager = helper.get_srm_manager();
    
    // Test existing symbol
    auto existing_id = Cryo::SRM::QualifiedIdentifier::create_simple(
        "test_var", Cryo::SymbolKind::Variable);
    CRYO_EXPECT_TRUE(manager->symbol_exists(*existing_id));
    
    // Test non-existing symbol
    auto non_existing_id = Cryo::SRM::QualifiedIdentifier::create_simple(
        "non_existent", Cryo::SymbolKind::Variable);
    CRYO_EXPECT_FALSE(manager->symbol_exists(*non_existing_id));
}

CRYO_TEST_DESC(SRM_Manager, CachePerformance, 
              "Tests symbol resolution caching") {
    SRMTestHelper helper;
    helper.setup();
    helper.add_test_symbols();
    
    auto manager = helper.get_srm_manager();
    auto test_id = Cryo::SRM::QualifiedIdentifier::create_simple(
        "test_var", Cryo::SymbolKind::Variable);
    
    // Clear statistics
    manager->reset_statistics();
    
    // First resolution - should miss cache
    auto result1 = manager->resolve_symbol(*test_id);
    auto stats1 = manager->get_statistics();
    CRYO_EXPECT_EQ(stats1.cache_misses, 1);
    CRYO_EXPECT_EQ(stats1.cache_hits, 0);
    
    // Second resolution - should hit cache
    auto result2 = manager->resolve_symbol(*test_id);
    auto stats2 = manager->get_statistics();
    CRYO_EXPECT_EQ(stats2.cache_hits, 1);
    
    // Results should be identical
    CRYO_EXPECT_EQ(result1.symbol, result2.symbol);
}

// ============================================================================
// AST Node Integration Tests
// ============================================================================

CRYO_TEST_DESC(SRM_AST, FunctionDeclarationIntegration, 
              "Tests SRM integration with FunctionDeclarationNode") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Create a mock function declaration node (simplified for testing)
    auto func_node = std::make_unique<Cryo::FunctionDeclarationNode>(
        Cryo::SourceLocation{}, "test_function", helper.create_void_type());
    
    // Test SRM extension methods
    using FuncExt = Cryo::SRM::SRMNodeExtensions<Cryo::FunctionDeclarationNode>;
    
    std::string canonical_name = FuncExt::get_canonical_name(func_node.get(), context);
    CRYO_EXPECT_FALSE(canonical_name.empty());
    CRYO_EXPECT_TRUE(canonical_name.find("test_function") != std::string::npos);
    
    std::string llvm_name = FuncExt::get_llvm_name(func_node.get(), context);
    CRYO_EXPECT_FALSE(llvm_name.empty());
    CRYO_EXPECT_TRUE(llvm_name.find("::") == std::string::npos); // Should be LLVM-escaped
}

CRYO_TEST_DESC(SRM_AST, TypeDeclarationIntegration, 
              "Tests SRM integration with StructDeclarationNode") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Create a mock struct declaration node
    auto struct_node = std::make_unique<Cryo::StructDeclarationNode>(
        Cryo::SourceLocation{}, "TestStruct");
    
    // Test SRM extension methods
    using StructExt = Cryo::SRM::SRMNodeExtensions<Cryo::StructDeclarationNode>;
    
    std::string canonical_name = StructExt::get_canonical_name(struct_node.get(), context);
    CRYO_EXPECT_EQ(canonical_name, "TestStruct");
    
    std::string llvm_name = StructExt::get_llvm_struct_name(struct_node.get(), context);
    CRYO_EXPECT_FALSE(llvm_name.empty());
    
    // Test constructor name generation
    std::vector<Cryo::Type*> param_types = {helper.create_int_type()};
    std::string constructor_name = StructExt::get_constructor_name(
        struct_node.get(), param_types, context);
    CRYO_EXPECT_TRUE(constructor_name.find("TestStruct::TestStruct") != std::string::npos);
}

// ============================================================================
// SymbolTable Integration Tests
// ============================================================================

CRYO_TEST_DESC(SRM_SymbolTable, SRMIntegration, 
              "Tests SymbolTable SRM integration methods") {
    SRMTestHelper helper;
    helper.setup();
    
    auto symbol_table = helper.get_symbol_table();
    
    // Test SRM initialization
    auto srm_context = symbol_table->get_srm_context();
    CRYO_EXPECT_NOT_NULL(srm_context);
    
    auto srm_manager = symbol_table->get_srm_manager();
    CRYO_EXPECT_NOT_NULL(srm_manager);
    
    // Test SRM configuration
    symbol_table->configure_srm(true, true);
    CRYO_EXPECT_TRUE(srm_context->is_implicit_std_import_enabled());
    CRYO_EXPECT_TRUE(srm_context->is_namespace_fallback_enabled());
}

CRYO_TEST_DESC(SRM_SymbolTable, SRMSymbolDeclaration, 
              "Tests SRM-based symbol declaration") {
    SRMTestHelper helper;
    helper.setup();
    
    auto symbol_table = helper.get_symbol_table();
    
    // Create SRM identifier
    auto identifier = Cryo::SRM::QualifiedIdentifier::create_simple(
        "srm_test_var", Cryo::SymbolKind::Variable);
    
    // Declare symbol using SRM
    bool success = symbol_table->declare_symbol_srm(
        std::move(identifier), Cryo::SymbolKind::Variable, 
        Cryo::SourceLocation{}, helper.create_int_type());
    
    CRYO_EXPECT_TRUE(success);
    
    // Verify symbol can be found through traditional lookup
    auto symbol = symbol_table->lookup_symbol("srm_test_var");
    CRYO_EXPECT_NOT_NULL(symbol);
    CRYO_EXPECT_EQ(symbol->name, "srm_test_var");
}

// ============================================================================
// Utility and Edge Case Tests
// ============================================================================

CRYO_TEST_DESC(SRM_Utils, StringNormalization, 
              "Tests utility string normalization functions") {
    using namespace Cryo::SRM::Utils;
    
    // Test type name normalization
    CRYO_EXPECT_EQ(normalize_type_name("i32"), "int");
    CRYO_EXPECT_EQ(normalize_type_name("i64"), "long");
    CRYO_EXPECT_EQ(normalize_type_name("f32"), "float");
    CRYO_EXPECT_EQ(normalize_type_name("f64"), "double");
    CRYO_EXPECT_EQ(normalize_type_name("bool"), "boolean");
    
    // Test qualified name parsing
    auto parsed = parse_qualified_name("std::collections::Vector");
    CRYO_EXPECT_EQ(parsed.first.size(), 2);
    CRYO_EXPECT_EQ(parsed.first[0], "std");
    CRYO_EXPECT_EQ(parsed.first[1], "collections");
    CRYO_EXPECT_EQ(parsed.second, "Vector");
    
    // Test qualified name building
    std::vector<std::string> namespaces = {"std", "io"};
    std::string qualified = build_qualified_name(namespaces, "println");
    CRYO_EXPECT_EQ(qualified, "std::io::println");
}

CRYO_TEST_DESC(SRM_Utils, IdentifierValidation, 
              "Tests identifier validation functions") {
    using namespace Cryo::SRM::Utils;
    
    // Test valid identifiers
    CRYO_EXPECT_TRUE(is_valid_identifier("valid_name"));
    CRYO_EXPECT_TRUE(is_valid_identifier("_underscore"));
    CRYO_EXPECT_TRUE(is_valid_identifier("name123"));
    
    // Test invalid identifiers
    CRYO_EXPECT_FALSE(is_valid_identifier("123invalid"));
    CRYO_EXPECT_FALSE(is_valid_identifier(""));
    CRYO_EXPECT_FALSE(is_valid_identifier("invalid-name"));
    CRYO_EXPECT_FALSE(is_valid_identifier("invalid.name"));
}

CRYO_TEST_DESC(SRM_Utils, LLVMEscaping, 
              "Tests LLVM name escaping") {
    using namespace Cryo::SRM::Utils;
    
    std::string escaped = escape_for_llvm("std::collections::Vector<int>");
    CRYO_EXPECT_TRUE(escaped.find("::") == std::string::npos);
    CRYO_EXPECT_TRUE(escaped.find("<") == std::string::npos);
    CRYO_EXPECT_TRUE(escaped.find(">") == std::string::npos);
    CRYO_EXPECT_FALSE(escaped.empty());
}

CRYO_TEST_DESC(SRM_Utils, StringSimilarity, 
              "Tests string similarity calculation for suggestions") {
    using namespace Cryo::SRM::Utils;
    
    // Test identical strings
    CRYO_EXPECT_EQ(calculate_string_similarity("test", "test"), 1.0);
    
    // Test completely different strings
    CRYO_EXPECT_EQ(calculate_string_similarity("abc", "xyz"), 0.0);
    
    // Test similar strings
    double similarity = calculate_string_similarity("function", "functoin");
    CRYO_EXPECT_GT(similarity, 0.5);
    CRYO_EXPECT_LT(similarity, 1.0);
}

// ============================================================================
// Error Handling and Edge Cases
// ============================================================================

CRYO_TEST_DESC(SRM_EdgeCases, NullPointerHandling, 
              "Tests proper null pointer handling throughout SRM") {
    SRMTestHelper helper;
    helper.setup();
    
    // Test null context handling in extensions
    using FuncExt = Cryo::SRM::SRMNodeExtensions<Cryo::FunctionDeclarationNode>;
    
    auto func_node = std::make_unique<Cryo::FunctionDeclarationNode>(
        Cryo::SourceLocation{}, "test", helper.create_void_type());
    
    // Should handle null context gracefully
    auto identifier = FuncExt::create_function_identifier(func_node.get(), nullptr);
    CRYO_EXPECT_NULL(identifier.get());
    
    std::string name = FuncExt::get_canonical_name(func_node.get(), nullptr);
    CRYO_EXPECT_EQ(name, "test"); // Should fall back to simple name
}

CRYO_TEST_DESC(SRM_EdgeCases, InvalidIdentifierNames, 
              "Tests handling of invalid identifier names") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Test invalid namespace names should throw
    bool threw_exception = false;
    try {
        context->push_namespace("");
    } catch (const std::invalid_argument&) {
        threw_exception = true;
    }
    CRYO_EXPECT_TRUE(threw_exception);
    
    threw_exception = false;
    try {
        context->push_namespace("123invalid");
    } catch (const std::invalid_argument&) {
        threw_exception = true;
    }
    CRYO_EXPECT_TRUE(threw_exception);
}

CRYO_TEST_DESC(SRM_EdgeCases, EmptyParameterLists, 
              "Tests handling of functions with no parameters") {
    SRMTestHelper helper;
    helper.setup();
    
    // Test function with no parameters
    std::vector<Cryo::Type*> empty_params;
    auto func_id = std::make_unique<Cryo::SRM::FunctionIdentifier>(
        "no_params", empty_params, helper.create_void_type());
    
    CRYO_EXPECT_EQ(func_id->get_parameter_types().size(), 0);
    
    std::string signature = func_id->to_signature_string();
    CRYO_EXPECT_EQ(signature, "()");
    
    std::string overload_key = func_id->to_overload_key();
    CRYO_EXPECT_TRUE(overload_key.find("no_params()") != std::string::npos);
}

CRYO_TEST_DESC(SRM_EdgeCases, DeepNamespaceNesting, 
              "Tests handling of deeply nested namespaces") {
    SRMTestHelper helper;
    helper.setup();
    
    auto context = helper.get_srm_context();
    
    // Create deeply nested namespace
    std::vector<std::string> deep_namespaces = {
        "level1", "level2", "level3", "level4", "level5"
    };
    
    for (const auto& ns : deep_namespaces) {
        context->push_namespace(ns);
    }
    
    std::string path = context->get_current_namespace_path();
    CRYO_EXPECT_EQ(path, "level1::level2::level3::level4::level5");
    
    // Test identifier creation with deep nesting
    auto identifier = context->create_qualified_identifier("deep_symbol", Cryo::SymbolKind::Variable);
    CRYO_EXPECT_EQ(identifier->to_string(), "level1::level2::level3::level4::level5::deep_symbol");
}

// ============================================================================
// Performance Tests
// ============================================================================

CRYO_TEST_DESC(SRM_Performance, ResolutionCacheEfficiency, 
              "Tests symbol resolution cache performance") {
    SRMTestHelper helper;
    helper.setup();
    helper.add_test_symbols();
    
    auto manager = helper.get_srm_manager();
    auto test_id = Cryo::SRM::QualifiedIdentifier::create_simple(
        "test_var", Cryo::SymbolKind::Variable);
    
    manager->reset_statistics();
    
    // Perform multiple resolutions
    const int num_resolutions = 100;
    for (int i = 0; i < num_resolutions; ++i) {
        auto result = manager->resolve_symbol(*test_id);
        CRYO_EXPECT_TRUE(result.is_valid());
    }
    
    auto stats = manager->get_statistics();
    CRYO_EXPECT_EQ(stats.total_resolution_attempts, num_resolutions);
    CRYO_EXPECT_EQ(stats.cache_misses, 1); // Only first should miss
    CRYO_EXPECT_EQ(stats.cache_hits, num_resolutions - 1);
    
    double cache_hit_ratio = stats.cache_hit_ratio;
    CRYO_EXPECT_GT(cache_hit_ratio, 0.95); // Should be very high
}

CRYO_TEST_DESC(SRM_Performance, IdentifierHashConsistency, 
              "Tests hash consistency for identifier caching") {
    SRMTestHelper helper;
    helper.setup();
    
    // Create multiple identical identifiers
    const int num_identifiers = 100;
    std::vector<std::unique_ptr<Cryo::SRM::QualifiedIdentifier>> identifiers;
    
    for (int i = 0; i < num_identifiers; ++i) {
        identifiers.push_back(Cryo::SRM::QualifiedIdentifier::create_simple(
            "test_symbol", Cryo::SymbolKind::Variable));
    }
    
    // All should have the same hash
    size_t first_hash = identifiers[0]->hash();
    for (int i = 1; i < num_identifiers; ++i) {
        CRYO_EXPECT_EQ(identifiers[i]->hash(), first_hash);
    }
}

CRYO_TEST_DESC(SRM_Performance, LargeSymbolTableHandling, 
              "Tests performance with large numbers of symbols") {
    SRMTestHelper helper;
    helper.setup();
    
    auto symbol_table = helper.get_symbol_table();
    auto int_type = helper.create_int_type();
    
    // Add many symbols
    const int num_symbols = 1000;
    for (int i = 0; i < num_symbols; ++i) {
        std::string symbol_name = "test_symbol_" + std::to_string(i);
        symbol_table->declare_symbol(symbol_name, Cryo::SymbolKind::Variable, 
                                   Cryo::SourceLocation{}, int_type);
    }
    
    auto manager = helper.get_srm_manager();
    
    // Test resolution performance
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_symbols; ++i) {
        std::string symbol_name = "test_symbol_" + std::to_string(i);
        auto identifier = Cryo::SRM::QualifiedIdentifier::create_simple(
            symbol_name, Cryo::SymbolKind::Variable);
        auto result = manager->resolve_symbol(*identifier);
        CRYO_EXPECT_TRUE(result.is_valid());
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Resolution should be reasonably fast (adjust threshold as needed)
    CRYO_EXPECT_LT(duration.count(), 1000); // Less than 1 second for 1000 symbols
}