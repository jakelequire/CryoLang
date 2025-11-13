#include "test_utils.hpp"
#include "include/test_helpers.hpp"
#include <filesystem>
#include <fstream>

using namespace CryoTest;

// ============================================================================
// Standard Library Integration Tests
// Tests actual stdlib module compilation to catch real-world issues
// ============================================================================

/**
 * Test that our stdlib-like networking types compile correctly
 * Reproduces the errors found in std::net::Types module
 */
CRYO_TEST_DESC(StdLibIntegration, NetworkingTypesModule, "Tests compilation of networking types similar to stdlib modules") {
    IntegrationTestHelper helper;
    helper.setup();

    // Create a simplified version of the networking types that should work
    std::string source = R"(
        namespace TestNet::Types;
        
        type struct IpAddr {
            a: u8;
            b: u8; 
            c: u8;
            d: u8;
            
            IpAddr(octet_a: u8, octet_b: u8, octet_c: u8, octet_d: u8) {
                this.a = octet_a;
                this.b = octet_b;
                this.c = octet_c;
                this.d = octet_d;
            }
            
            to_u32() -> u32 {
                return (u32(this.a) << 24) | (u32(this.b) << 16) | (u32(this.c) << 8) | u32(this.d);
            }
            
            is_loopback() -> boolean {
                return this.a == u8(127);
            }
        }
        
        type struct SocketAddr {
            ip: IpAddr;
            port: u16;
            
            SocketAddr(address: IpAddr, port_num: u16) {
                this.ip = address;
                this.port = port_num;
            }
            
            get_ip() -> IpAddr {
                return this.ip;
            }
            
            get_port() -> u16 {
                return this.port;
            }
            
            set_port(new_port: u16) -> void {
                this.port = new_port;
                return;
            }
        }
        
        type enum NetError {
            SUCCESS = 0,
            CONNECTION_REFUSED = 1,
            TIMEOUT = 2,
            INVALID_ADDRESS = 3
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    if (!success) {
        std::cout << "Network types module test failed:\n" << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test TCP socket implementation similar to stdlib
 * Reproduces issues in std::net::TCP module 
 */
CRYO_TEST_DESC(StdLibIntegration, TcpSocketModule, "Tests TCP socket class compilation and method resolution") {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        namespace TestNet;
        
        // Inline the types to avoid import issues
        type struct SocketAddr {
            port: u16;
            
            SocketAddr(port_num: u16) {
                this.port = port_num;
            }
            
            get_port() -> u16 {
                return this.port;
            }
        }
        
        type enum NetError {
            SUCCESS = 0,
            FAILED = 1
        }
        
        type class TcpSocket {
        private:
            socket_fd: i32;
            local_addr: SocketAddr;
            peer_addr: SocketAddr;
            is_connected_flag: boolean;
            is_bound_flag: boolean;

        public:
            TcpSocket() {
                this.socket_fd = i32(-1);
                this.is_connected_flag = false;
                this.is_bound_flag = false;
                this.local_addr = SocketAddr(u16(0));
                this.peer_addr = SocketAddr(u16(0));
            }
            
            TcpSocket(fd: i32, local: SocketAddr, peer: SocketAddr) {
                this.socket_fd = fd;
                this.local_addr = local;
                this.peer_addr = peer;
                this.is_connected_flag = true;
                this.is_bound_flag = true;
            }
            
            bind(addr: SocketAddr) -> NetError {
                this.local_addr = addr;
                this.is_bound_flag = true;
                return NetError::SUCCESS;
            }
            
            connect(addr: SocketAddr) -> NetError {
                this.peer_addr = addr;
                this.is_connected_flag = true;
                return NetError::SUCCESS;
            }
            
            close() -> NetError {
                this.socket_fd = i32(-1);
                this.is_connected_flag = false;
                this.is_bound_flag = false;
                return NetError::SUCCESS;
            }
            
            is_connected() -> boolean {
                return this.is_connected_flag;
            }
            
            get_local_addr() -> SocketAddr {
                return this.local_addr;
            }
        }
        
        function test_tcp_usage() -> boolean {
            const socket: TcpSocket = TcpSocket();
            const addr: SocketAddr = SocketAddr(u16(8080));
            const result: NetError = socket.bind(addr);
            return socket.is_connected();
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    if (!success) {
        std::cout << "TCP socket module test failed:\n" << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test HTTP status codes and enums 
 * Reproduces issues in std::net::HTTP module
 */
CRYO_TEST_DESC(StdLibIntegration, HttpStatusModule, "Tests HTTP status code enums and related functionality") {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        namespace TestHttp;
        
        type enum StatusCode {
            OK = 200,
            NOT_FOUND = 404,
            INTERNAL_ERROR = 500
        }
        
        type enum HttpMethod {
            GET = 0,
            POST = 1,
            PUT = 2,
            DELETE = 3
        }
        
        type struct HttpRequest {
            method: HttpMethod;
            status: StatusCode;
            
            HttpRequest(m: HttpMethod) {
                this.method = m;
                this.status = StatusCode::OK;
            }
            
            set_status(code: StatusCode) -> void {
                this.status = code;
                return;
            }
            
            get_status() -> StatusCode {
                return this.status;
            }
            
            is_success() -> boolean {
                return this.status == StatusCode::OK;
            }
        }
        
        function test_http_functionality() -> boolean {
            const req: HttpRequest = HttpRequest(HttpMethod::GET);
            req.set_status(StatusCode::NOT_FOUND);
            const status: StatusCode = req.get_status();
            return !req.is_success();
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    if (!success) {
        std::cout << "HTTP status module test failed:\n" << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test complex constructor patterns that cause LLVM IR issues
 * Addresses "Incorrect number of arguments passed to called function" errors
 */
CRYO_TEST_DESC(StdLibIntegration, ComplexConstructorPatterns, "Tests complex constructor call patterns used in stdlib") {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        namespace TestConstructors;
        
        type struct Config {
            enabled: boolean;
            value: i32;
            
            Config(e: boolean, v: i32) {
                this.enabled = e;
                this.value = v;
            }
        }
        
        type class Manager {
        private:
            config: Config;
            name: string;
            
        public:
            Manager(cfg: Config, n: string) {
                this.config = cfg;
                this.name = n;
            }
            
            // Constructor with inline Config creation
            Manager(enabled: boolean, value: i32, name: string) {
                this.config = Config(enabled, value);
                this.name = name;
            }
            
            get_config() -> Config {
                return this.config;
            }
            
            is_enabled() -> boolean {
                return this.config.enabled;
            }
        }
        
        function test_complex_constructors() -> boolean {
            // Direct config creation
            const cfg1: Config = Config(true, 42);
            const mgr1: Manager = Manager(cfg1, "test1");
            
            // Inline constructor
            const mgr2: Manager = Manager(false, 24, "test2");
            
            return mgr1.is_enabled() && !mgr2.is_enabled();
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    if (!success) {
        std::cout << "Complex constructor patterns test failed:\n" << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test module that reproduces "Basic Block does not have terminator" errors
 * Focuses on control flow in struct methods
 */
CRYO_TEST_DESC(StdLibIntegration, ControlFlowTermination, "Tests proper control flow termination in complex methods") {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        namespace TestControlFlow;
        
        type struct StateMachine {
            state: i32;
            max_state: i32;
            
            StateMachine(initial_state: i32, max: i32) {
                this.state = initial_state;
                this.max_state = max;
            }
            
            // Method with multiple return paths
            advance() -> boolean {
                if (this.state < this.max_state) {
                    this.state = this.state + 1;
                    return true;
                } else {
                    return false;
                }
                // No unreachable code
            }
            
            // Method with while loop and proper termination
            reset_to_zero() -> void {
                while (this.state > 0) {
                    this.state = this.state - 1;
                }
                return; // Explicit return for void methods
            }
            
            // Method with switch-like logic using if-else
            get_state_name() -> string {
                if (this.state == 0) {
                    return "initial";
                } else if (this.state == this.max_state) {
                    return "final";
                } else {
                    return "intermediate";
                }
                // All paths return
            }
        }
        
        function test_state_machine() -> boolean {
            const sm: StateMachine = StateMachine(0, 3);
            const advanced: boolean = sm.advance();
            sm.reset_to_zero();
            const name: string = sm.get_state_name();
            return advanced;
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    if (!success) {
        std::cout << "Control flow termination test failed:\n" << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test namespace resolution like in stdlib modules
 * Addresses "Failed to create function declaration for: Types::SocketAddr" errors
 */
CRYO_TEST_DESC(StdLibIntegration, NamespaceTypeResolution, "Tests namespace type resolution in complex scenarios") {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        namespace Core::Types;
        
        type struct Address {
            value: u32;
            
            Address(val: u32) {
                this.value = val;
            }
        }
        
        namespace Net::Protocol;
        
        // Use fully qualified type name from different namespace
        type class Connection {
        private:
            addr: Core::Types::Address;
            
        public:
            Connection(address: Core::Types::Address) {
                this.addr = address;
            }
            
            get_address() -> Core::Types::Address {
                return this.addr;
            }
            
            connect() -> boolean {
                // Use the address somehow
                const val: u32 = this.addr.value;
                return val > u32(0);
            }
        }
        
        namespace Main;
        
        function test_namespace_resolution() -> boolean {
            const addr: Core::Types::Address = Core::Types::Address(u32(12345));
            const conn: Net::Protocol::Connection = Net::Protocol::Connection(addr);
            return conn.connect();
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    if (!success) {
        std::cout << "Namespace type resolution test failed:\n" << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}