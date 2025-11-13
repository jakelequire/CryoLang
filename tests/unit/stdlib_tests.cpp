#include "test_utils.hpp"
#include "include/test_helpers.hpp"
#include <cstdlib>

using namespace CryoTest;

// ============================================================================
// Standard Library Module Tests
// ============================================================================

/**
 * Test basic struct declaration and method compilation
 * This addresses the SocketAddr compilation issues in stdlib
 */
CRYO_TEST_DESC(StdLib, BasicStructWithMethods, "Tests compilation of structs with methods that have proper control flow") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct TestAddr {
            value: u16;
            
            TestAddr(val: u16) {
                this.value = val;
            }
            
            get_value() -> u16 {
                return this.value;
            }
            
            set_value(new_val: u16) -> void {
                this.value = new_val;
                return;
            }
        }
        
        function test_struct() -> u16 {
            const addr: TestAddr = TestAddr(u16(8080));
            addr.set_value(u16(9090));
            return addr.get_value();
        }
    )";
    
    bool success = helper.compile_source(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test namespaced type resolution
 * This addresses the "Types::SocketAddr" resolution issues
 */
CRYO_TEST_DESC(StdLib, NamespacedTypeResolution, "Tests resolution of types through namespace qualifiers") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        namespace TestTypes;
        
        type struct Address {
            port: u16;
            
            Address(p: u16) {
                this.port = p;
            }
        }
        
        namespace TestNet;
        import TestTypes;
        
        type class Socket {
            addr: TestTypes::Address;
            
            Socket(a: TestTypes::Address) {
                this.addr = a;
            }
            
            get_address() -> TestTypes::Address {
                return this.addr;
            }
        }
    )";
    
    bool success = helper.compile_source(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test enum with explicit values
 * This addresses HTTP status code compilation issues
 */
CRYO_TEST_DESC(StdLib, EnumWithExplicitValues, "Tests enum declarations with explicit numeric values") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        type enum Status {
            OK = 200,
            NOT_FOUND = 404,
            SERVER_ERROR = 500
        }
        
        function get_status_code(s: Status) -> i32 {
            if (s == Status::OK) {
                return 200;
            }
            if (s == Status::NOT_FOUND) {
                return 404;
            }
            return 500;
        }
    )";
    
    bool success = helper.compile_source(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test struct method return type validation
 * This catches the "Function return type does not match" errors
 */
CRYO_TEST_DESC(StdLib, StructMethodReturnTypes, "Tests proper return type handling in struct methods") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Container {
            data: i32;
            
            Container(val: i32) {
                this.data = val;
            }
            
            // Method returning primitive
            get_data() -> i32 {
                return this.data;
            }
            
            // Method returning boolean
            is_positive() -> boolean {
                return this.data > 0;
            }
            
            // Method returning void (proper termination)
            reset() -> void {
                this.data = 0;
                return;
            }
        }
    )";
    
    bool success = helper.compile_source(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test complex constructor calls
 * This addresses the "Incorrect number of arguments passed to called function" errors
 */
CRYO_TEST_DESC(StdLib, ComplexConstructorCalls, "Tests constructor calls with multiple parameters and type resolution") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Point {
            x: i32;
            y: i32;
            
            Point(x_val: i32, y_val: i32) {
                this.x = x_val;
                this.y = y_val;
            }
        }
        
        type class Shape {
            center: Point;
            radius: f32;
            
            Shape(p: Point, r: f32) {
                this.center = p;
                this.radius = r;
            }
            
            get_center() -> Point {
                return this.center;
            }
        }
        
        function test_constructor_chain() -> i32 {
            const pt: Point = Point(10, 20);
            const shape: Shape = Shape(pt, 5.0);
            const center: Point = shape.get_center();
            return center.x;
        }
    )";
    
    bool success = helper.compile_source(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test proper control flow termination
 * This addresses the "Basic Block does not have terminator" errors
 */
CRYO_TEST_DESC(StdLib, ControlFlowTermination, "Tests proper basic block termination in all control paths") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Config {
            enabled: boolean;
            value: i32;
            
            Config(e: boolean, v: i32) {
                this.enabled = e;
                this.value = v;
            }
            
            // Method with conditional return paths
            get_value_if_enabled() -> i32 {
                if (this.enabled) {
                    return this.value;
                } else {
                    return -1;
                }
                // No unreachable code after complete if-else
            }
            
            // Method with void return
            toggle() -> void {
                this.enabled = !this.enabled;
                return; // Explicit return for void
            }
        }
    )";
    
    bool success = helper.compile_source(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test imported module compilation
 * This catches import resolution issues that may affect stdlib modules
 */
CRYO_TEST_DESC(StdLib, ModuleImportCompilation, "Tests compilation of modules that import other modules") {
    CodegenTestHelper helper;
    helper.setup();

    // First create a helper module source
    std::string module_source = R"(
        namespace TestUtils;
        
        type struct Helper {
            id: u32;
            
            Helper(i: u32) {
                this.id = i;
            }
            
            get_id() -> u32 {
                return this.id;
            }
        }
    )";

    // Then test importing and using it
    std::string main_source = R"(
        namespace TestMain;
        import TestUtils;
        
        function use_helper() -> u32 {
            const h: TestUtils::Helper = TestUtils::Helper(u32(42));
            return h.get_id();
        }
    )";
    
    // Test the main source (the helper would need to be compiled separately in a real scenario)
    bool success = helper.compile_source(main_source);
    // This might fail due to import resolution, but we're testing the compilation flow
    CRYO_EXPECT_TRUE(success);
}

/**
 * Test string literal compilation
 * This addresses string handling issues seen in the test suite
 */
CRYO_TEST_DESC(StdLib, StringLiteralHandling, "Tests proper string literal compilation and memory management") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_strings() -> boolean {
            const msg1: string = "Hello";
            const msg2: string = "World";
            const empty: string = "";
            return true;
        }
        
        type struct Message {
            content: string;
            length: u32;
            
            Message(text: string) {
                this.content = text;
                this.length = u32(0); // Placeholder - would need strlen intrinsic
            }
            
            get_content() -> string {
                return this.content;
            }
        }
    )";
    
    bool success = helper.compile_source(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}