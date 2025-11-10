#include "test_utils.hpp"
#include "Parser/Parser.hpp"
#include "AST/ASTNode.hpp"

namespace CryoTest {

class ParserTest : public ParserTestFixture {
};

// ============================================================================
// Basic Statement Parsing Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseVariableDeclaration) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const x: int = 42;";
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    CRYO_EXPECT_EQ(ast->statements().size(), 1);
    
    auto stmt = ast->statements()[0].get();
    CRYO_EXPECT_EQ(stmt->kind(), Cryo::NodeKind::VariableDeclaration);
    
    auto var_decl = dynamic_cast<Cryo::VariableDeclarationNode*>(stmt);
    CRYO_ASSERT_TRUE(var_decl != nullptr);
    CRYO_EXPECT_EQ(var_decl->name(), "x");
    CRYO_EXPECT_EQ(var_decl->type_annotation(), "int");
    CRYO_EXPECT_TRUE(var_decl->initializer() != nullptr);
}

CRYO_TEST(ParserTest, ParseMutableVariableDeclaration) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "mut y: float = 3.14;";
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto stmt = ast->statements()[0].get();
    auto var_decl = dynamic_cast<Cryo::VariableDeclarationNode*>(stmt);
    CRYO_ASSERT_TRUE(var_decl != nullptr);
    CRYO_EXPECT_EQ(var_decl->name(), "y");
    CRYO_EXPECT_EQ(var_decl->type_annotation(), "float");
    CRYO_EXPECT_TRUE(var_decl->is_mutable());
}

CRYO_TEST(ParserTest, ParseVariableWithoutInitializer) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "mut z: string;";
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto stmt = ast->statements()[0].get();
    auto var_decl = dynamic_cast<Cryo::VariableDeclarationNode*>(stmt);
    CRYO_ASSERT_TRUE(var_decl != nullptr);
    CRYO_EXPECT_EQ(var_decl->name(), "z");
    CRYO_EXPECT_EQ(var_decl->type_annotation(), "string");
    CRYO_EXPECT_TRUE(var_decl->initializer() == nullptr);
}

// ============================================================================
// Function Declaration Parsing Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseSimpleFunctionDeclaration) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function add(x: int, y: int) -> int {
            return x + y;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    CRYO_EXPECT_EQ(ast->statements().size(), 1);
    
    auto stmt = ast->statements()[0].get();
    CRYO_EXPECT_EQ(stmt->kind(), Cryo::NodeKind::FunctionDeclaration);
    
    auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(stmt);
    CRYO_ASSERT_TRUE(func_decl != nullptr);
    CRYO_EXPECT_EQ(func_decl->name(), "add");
    CRYO_EXPECT_EQ(func_decl->parameters().size(), 2);
    CRYO_EXPECT_EQ(func_decl->return_type()->name(), "int");
    CRYO_EXPECT_TRUE(func_decl->body() != nullptr);
    
    // Check parameters
    CRYO_EXPECT_EQ(func_decl->parameters()[0]->name(), "x");
    CRYO_EXPECT_EQ(func_decl->parameters()[0]->type_annotation(), "int");
    CRYO_EXPECT_EQ(func_decl->parameters()[1]->name(), "y");
    CRYO_EXPECT_EQ(func_decl->parameters()[1]->type_annotation(), "int");
}

CRYO_TEST(ParserTest, ParseFunctionWithNoParameters) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            return 0;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(func_decl != nullptr);
    CRYO_EXPECT_EQ(func_decl->name(), "main");
    CRYO_EXPECT_EQ(func_decl->parameters().size(), 0);
}

CRYO_TEST(ParserTest, ParseFunctionWithVoidReturn) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function print_hello() -> void {
            return;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(func_decl != nullptr);
    CRYO_EXPECT_EQ(func_decl->name(), "print_hello");
    CRYO_EXPECT_EQ(func_decl->return_type()->name(), "void");
}

// ============================================================================
// Expression Parsing Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseBinaryExpression) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const result: int = x + y * 2;";
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto var_decl = dynamic_cast<Cryo::VariableDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(var_decl != nullptr);
    CRYO_ASSERT_TRUE(var_decl->initializer() != nullptr);
    
    auto binary_expr = dynamic_cast<Cryo::BinaryExpressionNode*>(var_decl->initializer());
    CRYO_ASSERT_TRUE(binary_expr != nullptr);
    CRYO_EXPECT_EQ(binary_expr->operator_token().kind(), Cryo::TokenKind::TK_PLUS);
}

CRYO_TEST(ParserTest, ParseUnaryExpression) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const negative: int = -x;";
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto var_decl = dynamic_cast<Cryo::VariableDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(var_decl != nullptr);
    CRYO_ASSERT_TRUE(var_decl->initializer() != nullptr);
    
    auto unary_expr = dynamic_cast<Cryo::UnaryExpressionNode*>(var_decl->initializer());
    CRYO_ASSERT_TRUE(unary_expr != nullptr);
    CRYO_EXPECT_EQ(unary_expr->operator_token().kind(), Cryo::TokenKind::TK_MINUS);
}

CRYO_TEST(ParserTest, ParseTernaryExpression) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const result: int = x > 0 ? 1 : 0;";
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto var_decl = dynamic_cast<Cryo::VariableDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(var_decl != nullptr);
    CRYO_ASSERT_TRUE(var_decl->initializer() != nullptr);
    
    auto ternary_expr = dynamic_cast<Cryo::TernaryExpressionNode*>(var_decl->initializer());
    CRYO_ASSERT_TRUE(ternary_expr != nullptr);
}

CRYO_TEST(ParserTest, ParseFunctionCall) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const result: int = add(5, 10);";
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto var_decl = dynamic_cast<Cryo::VariableDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(var_decl != nullptr);
    CRYO_ASSERT_TRUE(var_decl->initializer() != nullptr);
    
    auto call_expr = dynamic_cast<Cryo::CallExpressionNode*>(var_decl->initializer());
    CRYO_ASSERT_TRUE(call_expr != nullptr);
    CRYO_EXPECT_EQ(call_expr->arguments().size(), 2);
}

// ============================================================================
// Control Flow Parsing Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseIfStatement) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test() -> void {
            if (x > 0) {
                return;
            }
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(func_decl != nullptr);
    
    auto block = func_decl->body();
    CRYO_ASSERT_TRUE(block != nullptr);
    CRYO_EXPECT_EQ(block->statements().size(), 1);
    
    auto if_stmt = dynamic_cast<Cryo::IfStatementNode*>(block->statements()[0].get());
    CRYO_ASSERT_TRUE(if_stmt != nullptr);
    CRYO_EXPECT_TRUE(if_stmt->condition() != nullptr);
    CRYO_EXPECT_TRUE(if_stmt->then_branch() != nullptr);
    CRYO_EXPECT_TRUE(if_stmt->else_branch() == nullptr);
}

CRYO_TEST(ParserTest, ParseIfElseStatement) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test() -> void {
            if (x > 0) {
                return;
            } else {
                return;
            }
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(ast->statements()[0].get());
    auto if_stmt = dynamic_cast<Cryo::IfStatementNode*>(
        func_decl->body()->statements()[0].get());
    
    CRYO_ASSERT_TRUE(if_stmt != nullptr);
    CRYO_EXPECT_TRUE(if_stmt->condition() != nullptr);
    CRYO_EXPECT_TRUE(if_stmt->then_branch() != nullptr);
    CRYO_EXPECT_TRUE(if_stmt->else_branch() != nullptr);
}

CRYO_TEST(ParserTest, ParseWhileLoop) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test() -> void {
            while (x < 10) {
                x = x + 1;
            }
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(ast->statements()[0].get());
    auto while_stmt = dynamic_cast<Cryo::WhileStatementNode*>(
        func_decl->body()->statements()[0].get());
    
    CRYO_ASSERT_TRUE(while_stmt != nullptr);
    CRYO_EXPECT_TRUE(while_stmt->condition() != nullptr);
    CRYO_EXPECT_TRUE(while_stmt->body() != nullptr);
}

CRYO_TEST(ParserTest, ParseForLoop) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test() -> void {
            for (mut i: int = 0; i < 10; i++) {
                return;
            }
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(ast->statements()[0].get());
    auto for_stmt = dynamic_cast<Cryo::ForStatementNode*>(
        func_decl->body()->statements()[0].get());
    
    CRYO_ASSERT_TRUE(for_stmt != nullptr);
    CRYO_EXPECT_TRUE(for_stmt->initializer() != nullptr);
    CRYO_EXPECT_TRUE(for_stmt->condition() != nullptr);
    CRYO_EXPECT_TRUE(for_stmt->increment() != nullptr);
    CRYO_EXPECT_TRUE(for_stmt->body() != nullptr);
}

// ============================================================================
// Struct Declaration Parsing Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseStructDeclaration) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    CRYO_EXPECT_EQ(ast->statements().size(), 1);
    
    auto stmt = ast->statements()[0].get();
    CRYO_EXPECT_EQ(stmt->kind(), Cryo::NodeKind::StructDeclaration);
    
    auto struct_decl = dynamic_cast<Cryo::StructDeclarationNode*>(stmt);
    CRYO_ASSERT_TRUE(struct_decl != nullptr);
    CRYO_EXPECT_EQ(struct_decl->name(), "Point");
    CRYO_EXPECT_EQ(struct_decl->fields().size(), 2);
    
    // Check fields
    CRYO_EXPECT_EQ(struct_decl->fields()[0]->name(), "x");
    CRYO_EXPECT_EQ(struct_decl->fields()[0]->type_annotation(), "int");
    CRYO_EXPECT_EQ(struct_decl->fields()[1]->name(), "y");
    CRYO_EXPECT_EQ(struct_decl->fields()[1]->type_annotation(), "int");
}

CRYO_TEST(ParserTest, ParseStructWithMethods) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
            
            Point(x: int, y: int);
            distance() -> float;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto struct_decl = dynamic_cast<Cryo::StructDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(struct_decl != nullptr);
    CRYO_EXPECT_EQ(struct_decl->name(), "Point");
    CRYO_EXPECT_EQ(struct_decl->fields().size(), 2);
    CRYO_EXPECT_EQ(struct_decl->methods().size(), 2);
}

// ============================================================================
// Class Declaration Parsing Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseClassDeclaration) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        type class Circle {
        public:
            radius: float;
            
        private:
            area_cached: float;
            
            Circle(r: float);
            get_area() -> float;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    CRYO_EXPECT_EQ(ast->statements().size(), 1);
    
    auto class_decl = dynamic_cast<Cryo::ClassDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(class_decl != nullptr);
    CRYO_EXPECT_EQ(class_decl->name(), "Circle");
}

// ============================================================================
// Enum Declaration Parsing Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseSimpleEnum) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        enum Color {
            RED,
            GREEN,
            BLUE
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto enum_decl = dynamic_cast<Cryo::EnumDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(enum_decl != nullptr);
    CRYO_EXPECT_EQ(enum_decl->name(), "Color");
    CRYO_EXPECT_EQ(enum_decl->variants().size(), 3);
}

CRYO_TEST(ParserTest, ParseComplexEnum) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        enum Shape {
            Circle(float),
            Rectangle(float, float),
            Point
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    auto enum_decl = dynamic_cast<Cryo::EnumDeclarationNode*>(ast->statements()[0].get());
    CRYO_ASSERT_TRUE(enum_decl != nullptr);
    CRYO_EXPECT_EQ(enum_decl->name(), "Shape");
    CRYO_EXPECT_EQ(enum_decl->variants().size(), 3);
}

// ============================================================================
// Namespace and Import Parsing Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseNamespaceDeclaration) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        namespace MyModule;
        
        function test() -> void {
            return;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    CRYO_EXPECT_EQ(parser->current_namespace(), "MyModule");
}

CRYO_TEST(ParserTest, ParseImportDeclaration) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        import IO from <io/stdio>;
        import <core/types>;
        
        function test() -> void {
            return;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    
    // First two statements should be imports
    auto import1 = dynamic_cast<Cryo::ImportDeclarationNode*>(ast->statements()[0].get());
    auto import2 = dynamic_cast<Cryo::ImportDeclarationNode*>(ast->statements()[1].get());
    
    CRYO_ASSERT_TRUE(import1 != nullptr);
    CRYO_ASSERT_TRUE(import2 != nullptr);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

CRYO_TEST(ParserTest, HandleMissingSemicolon) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const x: int = 42"; // Missing semicolon
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_EXPECT_TRUE(parser->has_errors());
}

CRYO_TEST(ParserTest, HandleMissingClosingBrace) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test() -> void {
            return;
        // Missing closing brace
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_EXPECT_TRUE(parser->has_errors());
}

CRYO_TEST(ParserTest, HandleInvalidExpression) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const x: int = + + +;"; // Invalid expression
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_EXPECT_TRUE(parser->has_errors());
}

// ============================================================================
// Complex Program Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseCompleteProgram) {
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        namespace TestModule;
        
        import IO from <io/stdio>;
        
        type struct Point {
            x: int;
            y: int;
        }
        
        function distance(p1: Point, p2: Point) -> float {
            const dx: int = p1.x - p2.x;
            const dy: int = p1.y - p2.y;
            return sqrt(dx * dx + dy * dy);
        }
        
        function main() -> int {
            const p1: Point = Point({x: 0, y: 0});
            const p2: Point = Point({x: 3, y: 4});
            const d: float = distance(p1, p2);
            
            if (d > 0.0) {
                IO::println("Distance calculated successfully");
            }
            
            return 0;
        }
    )";
    
    auto parser = create_parser(source);
    auto ast = parser->parse_program();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    CRYO_EXPECT_EQ(parser->current_namespace(), "TestModule");
    EXPECT_GT(ast->statements().size(), 3); // Import, struct, functions
}

// ============================================================================
// Performance Tests
// ============================================================================

CRYO_TEST(ParserTest, ParseLargeProgram) {
    ParserTestHelper helper;
    helper.setup();

    // Generate a large program for performance testing
    std::string large_source = "namespace TestModule;\n";
    
    for (int i = 0; i < 100; ++i) {
        large_source += "function func" + std::to_string(i) + 
                       "(x: int, y: int) -> int {\n";
        large_source += "    const result: int = x + y + " + std::to_string(i) + ";\n";
        large_source += "    return result;\n";
        large_source += "}\n\n";
    }
    
    auto parser = create_parser(large_source);
    
    PerformanceTimer timer;
    timer.start();
    
    auto ast = parser->parse_program();
    
    double elapsed = timer.elapsed_ms();
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    CRYO_EXPECT_FALSE(parser->has_errors());
    CRYO_EXPECT_EQ(ast->statements().size(), 100); // 100 functions
    EXPECT_LT(elapsed, 5000.0) << "Parsing should complete within 5 seconds";
    
    std::cout << "Parsed large program (" << ast->statements().size() 
              << " statements) in " << elapsed << "ms\n";
}

} // namespace CryoTest