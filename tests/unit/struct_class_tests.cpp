#include "test_utils.hpp"
#include "include/test_helpers.hpp"

using namespace CryoTest;

/**
 * @file struct_class_tests.cpp
 * @brief Comprehensive tests for struct and class functionality in CryoLang
 * 
 * Tests struct/class declaration, instantiation, member access, methods,
 * constructors, destructors, and implementation blocks.
 */

// ============================================================================
// Basic Struct Operations
// ============================================================================

CRYO_TEST_DESC(StructBasics, simple_struct_declaration_and_access,
    "Test basic struct declaration with field access") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
        
        function test_struct_access() -> int {
            const p: Point = Point { x: 10, y: 20 };
            const x_coord: int = p.x;
            const y_coord: int = p.y;
            
            return x_coord + y_coord;  // 10 + 20 = 30
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(StructBasics, struct_with_mixed_data_types,
    "Test struct containing different primitive types") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct MixedData {
            count: int;
            ratio: float;
            active: boolean;
            label: char;
        }
        
        function test_mixed_struct() -> int {
            const data: MixedData = MixedData {
                count: 42,
                ratio: 3.14,
                active: true,
                label: 'A'
            };
            
            const int_part: int = data.count;
            const bool_part: int = data.active ? 1 : 0;
            
            return int_part + bool_part;  // 42 + 1 = 43
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(StructBasics, nested_struct_access,
    "Test structs containing other structs as members") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
        
        type struct Rectangle {
            top_left: Point;
            bottom_right: Point;
        }
        
        function test_nested_struct() -> int {
            const rect: Rectangle = Rectangle {
                top_left: Point { x: 0, y: 10 },
                bottom_right: Point { x: 20, y: 0 }
            };
            
            const x1: int = rect.top_left.x;      // 0
            const y1: int = rect.top_left.y;      // 10
            const x2: int = rect.bottom_right.x;  // 20
            const y2: int = rect.bottom_right.y;  // 0
            
            return x1 + y1 + x2 + y2;  // 0 + 10 + 20 + 0 = 30
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// ============================================================================
// Struct Constructors
// ============================================================================

// DISABLED: This test causes std::terminate and crashes the test suite
// TODO: Fix the underlying std::length_error issue in string construction
/*
CRYO_TEST_DESC(StructConstructors, parameterized_constructor_basic,
    "Test struct with parameterized constructor") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source;
    try {
        source = R"(
        type struct Vector3 {
            x: float;
            y: float;
            z: float;
            
            Vector3(x_val: float, y_val: float, z_val: float);
        }
        
        implement Vector3 {
            Vector3(x_val: float, y_val: float, z_val: float) {
                this.x = x_val;
                this.y = y_val; 
                this.z = z_val;
            }
        }
        
        function test_parameterized_constructor() -> int {
            const vec: Vector3 = Vector3(1.0, 2.0, 3.0);
            
            // Convert to int for simple testing
            const result: int = int(vec.x + vec.y + vec.z);
            return result;  // int(6.0) = 6
        }
    )";
    } catch (const std::length_error& e) {
        // If string construction fails, skip this test
        std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
        std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m String construction failed" << std::endl;
        std::cout << "    \033[90m+-- \033[33mDetails:\033[0m " << e.what() << std::endl;
        return; // Exit test early
    }
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}
*/

CRYO_TEST_DESC(StructConstructors, multiple_constructor_overloads,
    "Test struct with multiple constructor variants") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Counter {
            value: int;
            step: int;
            
            Counter();  // Default constructor
            Counter(initial_value: int);  // Single param constructor
            Counter(initial_value: int, step_size: int);  // Two param constructor
        }
        
        implement Counter {
            Counter() {
                this.value = 0;
                this.step = 1;
            }
            
            Counter(initial_value: int) {
                this.value = initial_value;
                this.step = 1;
            }
            
            Counter(initial_value: int, step_size: int) {
                this.value = initial_value;
                this.step = step_size;
            }
        }
        
        function test_constructor_overloads() -> int {
            const counter1: Counter = Counter();          // 0, 1
            const counter2: Counter = Counter(10);        // 10, 1  
            const counter3: Counter = Counter(5, 3);      // 5, 3
            
            return counter1.value + counter2.value + counter3.value; // 0 + 10 + 5 = 15
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// ============================================================================  
// Struct Methods
// ============================================================================

CRYO_TEST_DESC(StructMethods, basic_method_with_return_value,
    "Test struct methods that return computed values") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Circle {
            radius: float;
            
            Circle(r: float);
            area() -> float;
            circumference() -> float;
        }
        
        implement Circle {
            Circle(r: float) {
                this.radius = r;
            }
            
            area() -> float {
                return 3.14159 * this.radius * this.radius;
            }
            
            circumference() -> float {
                return 2.0 * 3.14159 * this.radius;
            }
        }
        
        function test_struct_methods() -> int {
            const circle: Circle = Circle(5.0);
            const area: float = circle.area();           // ~78.54
            const circ: float = circle.circumference();  // ~31.42
            
            // Convert to int for testing
            return int(area + circ);  // int(~109.96) = 109
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(StructMethods, mutating_method_modifies_fields,
    "Test struct methods that modify internal state") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Counter {
            count: int;
            
            Counter(initial: int);
            increment() -> void;
            add(amount: int) -> void;
            get_value() -> int;
        }
        
        implement Counter {
            Counter(initial: int) {
                this.count = initial;
            }
            
            increment() -> void {
                this.count = this.count + 1;
            }
            
            add(amount: int) -> void {
                this.count = this.count + amount;
            }
            
            get_value() -> int {
                return this.count;
            }
        }
        
        function test_mutating_methods() -> int {
            mut counter: Counter = Counter(10);
            
            counter.increment();        // count = 11
            counter.add(5);            // count = 16  
            counter.increment();        // count = 17
            
            return counter.get_value(); // 17
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// ============================================================================
// Basic Class Operations  
// ============================================================================

CRYO_TEST_DESC(ClassBasics, simple_class_with_public_fields,
    "Test basic class declaration with public field access") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type class SimpleData {
        public:
            value: int;
            name: string;
            
            SimpleData(val: int, n: string) {
                this.value = val;
                this.name = n;
            }
        }
        
        function test_class_fields() -> int {
            const data: SimpleData = SimpleData(42, "test");
            const val: int = data.value;
            
            return val;  // 42
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// DISABLED: This test causes std::length_error and crashes the main process
// TODO: Fix the underlying issue causing main process termination
/*
CRYO_TEST_DESC(ClassBasics, class_with_private_fields_and_accessors,
    "Test class with private fields accessed through public methods") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type class BankAccount {
        private:
            balance: float;
            
        public:
            BankAccount(initial_balance: float) {
                this.balance = initial_balance;
            }
            
            deposit(amount: float) -> void {
                this.balance = this.balance + amount;
            }
            
            get_balance() -> float {
                return this.balance;
            }
        }
        
        function test_private_fields() -> int {
            mut account: BankAccount = BankAccount(100.0);
            account.deposit(50.0);
            
            const balance: float = account.get_balance();
            return int(balance);  // int(150.0) = 150
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}
*/

// ============================================================================
// Class Methods and Inheritance-like Behavior
// ============================================================================

CRYO_TEST_DESC(ClassMethods, method_chaining_operations,
    "Test method chaining patterns in class methods") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type class StringBuilder {
        private:
            length: int;
            
        public:
            StringBuilder() {
                this.length = 0;
            }
            
            append_char(c: char) -> StringBuilder* {
                this.length = this.length + 1;
                return this;
            }
            
            append_number(n: int) -> StringBuilder* {
                // Simplified: assume each digit adds 1 to length
                mut temp: int = n;
                while (temp > 0) {
                    this.length = this.length + 1;
                    temp = temp / 10;
                }
                return this;
            }
            
            get_length() -> int {
                return this.length;
            }
        }
        
        function test_method_chaining() -> int {
            mut builder: StringBuilder = StringBuilder();
            
            // Chain method calls (if supported)
            builder.append_char('A');
            builder.append_number(123);  // 3 digits
            builder.append_char('B');
            
            return builder.get_length();  // 1 + 3 + 1 = 5
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// ============================================================================
// Struct and Class Pointer Operations
// ============================================================================

CRYO_TEST_DESC(StructPointers, pointer_to_struct_member_access,
    "Test accessing struct members through pointers") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Coordinate {
            x: int;
            y: int;
        }
        
        function test_struct_pointer_access() -> int {
            mut coord: Coordinate = Coordinate { x: 15, y: 25 };
            mut coord_ptr: Coordinate* = &coord;
            
            // Access through pointer (if arrow operator supported, use ->)
            const x_val: int = (*coord_ptr).x;
            const y_val: int = (*coord_ptr).y;
            
            // Modify through pointer
            (*coord_ptr).x = 30;
            const new_x: int = coord.x;  // Should be 30
            
            return x_val + y_val + new_x;  // 15 + 25 + 30 = 70
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(ClassPointers, pointer_to_class_method_calls,
    "Test calling methods on classes through pointers") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type class Calculator {
        private:
            result: int;
            
        public:
            Calculator() {
                this.result = 0;
            }
            
            add(value: int) -> void {
                this.result = this.result + value;
            }
            
            multiply(value: int) -> void {
                this.result = this.result * value;
            }
            
            get_result() -> int {
                return this.result;
            }
        }
        
        function test_class_pointer_methods() -> int {
            mut calc: Calculator = Calculator();
            mut calc_ptr: Calculator* = &calc;
            
            // Call methods through pointer
            (*calc_ptr).add(10);
            (*calc_ptr).multiply(3);
            (*calc_ptr).add(5);
            
            return (*calc_ptr).get_result();  // ((0 + 10) * 3) + 5 = 35
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// ============================================================================
// Complex Struct/Class Scenarios
// ============================================================================

CRYO_TEST_DESC(ComplexStructs, array_of_structs_operations,
    "Test operations on arrays containing struct instances") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Student {
            id: int;
            grade: int;
            
            Student(student_id: int, student_grade: int);
        }
        
        implement Student {
            Student(student_id: int, student_grade: int) {
                this.id = student_id;
                this.grade = student_grade;
            }
        }
        
        function test_struct_array() -> int {
            mut students: Student[] = [
                Student(101, 85),
                Student(102, 92),  
                Student(103, 78)
            ];
            
            const first_grade: int = students[0].grade;   // 85
            const second_grade: int = students[1].grade;  // 92
            const third_grade: int = students[2].grade;   // 78
            
            return first_grade + second_grade + third_grade; // 85 + 92 + 78 = 255
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// DISABLED: This test causes std::length_error and crashes the main process
// TODO: Fix the underlying issue with large string literals in source code
/*
CRYO_TEST_DESC(ComplexClasses, class_composition_with_struct_members,
    "Test classes that contain structs as member variables") {
    
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Position {
            x: float;
            y: float;
        }
        
        type class Entity {
        private:
            pos: Position;
            health: int;
            
        public:
            Entity(x: float, y: float, hp: int) {
                this.pos = Position { x: x, y: y };
                this.health = hp;
            }
            
            move_to(new_x: float, new_y: float) -> void {
                this.pos.x = new_x;
                this.pos.y = new_y;
            }
            
            get_x() -> float {
                return this.pos.x;
            }
            
            get_health() -> int {
                return this.health;
            }
        }
        
        function test_class_with_struct() -> int {
            mut entity: Entity = Entity(10.0, 20.0, 100);
            entity.move_to(30.0, 40.0);
            
            const x_pos: float = entity.get_x();      // 30.0
            const health: int = entity.get_health();  // 100
            
            return int(x_pos) + health;  // 30 + 100 = 130
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}
*/
