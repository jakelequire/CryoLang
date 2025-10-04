## 🔧 **CryoLSP Rebuild Plan - Professional Approach**

### **Phase 1: Root Cause Analysis & Clean Slate** 
The current issue appears to be in the handshake completion. Your server stops after sending the initialize response and never receives the `initialized` notification, causing the communication to hang.

### **Key Problems Identified:**
1. **Blocking I/O**: The message handler is using blocking character-by-character reading which can deadlock
2. **Buffer synchronization**: Windows stdio buffering issues despite your mitigation attempts
3. **Error recovery**: No proper error handling when messages are malformed or incomplete
4. **State management**: Complex state tracking across different modes (stdio vs socket)

---

## 🏗️ **New LSP Architecture Design**

### **Core Principles:**
1. **Separation of Concerns**: Clean separation between transport, protocol, and language services
2. **Testability**: Each component can be unit tested independently  
3. **Robustness**: Graceful error handling and recovery
4. **Debuggability**: Comprehensive logging and state inspection
5. **Incrementality**: Build and test one feature at a time

### **Architecture Overview:**
```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────────┐
│   VS Code       │◄──►│   Transport      │◄──►│   CryoLSP Server    │
│   Extension     │    │   Layer          │    │                     │
└─────────────────┘    │                  │    │  ┌───────────────┐  │
                       │  • JSON-RPC      │    │  │   Protocol    │  │
                       │  • Message       │    │  │   Handler     │  │
                       │    Framing       │    │  └───────────────┘  │
                       │  • I/O Handling  │    │  ┌───────────────┐  │
                       └──────────────────┘    │  │   Language    │  │
                                               │  │   Services    │  │
                                               │  └───────────────┘  │
                                               └─────────────────────┘
```

---

## 📋 **Detailed Implementation Plan**

### **Phase 1: Foundation (Week 1)**

#### **Step 1.1: Create New Project Structure**
```
tools/CryoLSP/
├── src/
│   ├── main.cpp                 # Minimal entry point
│   ├── transport/
│   │   ├── Transport.hpp/.cpp   # Abstract transport interface
│   │   ├── StdioTransport.hpp/.cpp
│   │   └── SocketTransport.hpp/.cpp
│   ├── protocol/
│   │   ├── Message.hpp/.cpp     # LSP message types
│   │   ├── JsonRpc.hpp/.cpp     # JSON-RPC parsing
│   │   └── Protocol.hpp/.cpp    # LSP protocol handler
│   ├── services/
│   │   ├── LanguageServer.hpp/.cpp
│   │   └── DocumentManager.hpp/.cpp
│   └── utils/
│       ├── Logger.hpp/.cpp
│       └── Testing.hpp/.cpp
├── tests/
│   ├── unit/
│   └── integration/
└── CMakeLists.txt               # Modern build system
```

#### **Step 1.2: Implement Robust Transport Layer**
```cpp
// Transport.hpp
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool initialize() = 0;
    virtual std::optional<std::string> read_message() = 0;
    virtual bool write_message(const std::string& message) = 0;
    virtual void shutdown() = 0;
    
    // Event callbacks
    std::function<void(const std::string&)> on_message_received;
    std::function<void(const std::string&)> on_error;
};

// StdioTransport.cpp - NON-BLOCKING IMPLEMENTATION
class StdioTransport : public Transport {
    // Use proper async I/O or select/poll for non-blocking reads
    // Implement proper JSON-RPC message framing
    // Handle partial reads gracefully
};
```

#### **Step 1.3: Create Testable JSON-RPC Parser**
```cpp
// JsonRpc.hpp
struct JsonRpcMessage {
    std::string jsonrpc = "2.0";
    std::optional<std::string> id;
    std::optional<std::string> method;
    std::optional<nlohmann::json> params;
    std::optional<nlohmann::json> result;
    std::optional<nlohmann::json> error;
};

class JsonRpcParser {
public:
    static std::optional<JsonRpcMessage> parse(const std::string& json);
    static std::string serialize(const JsonRpcMessage& message);
    static bool is_request(const JsonRpcMessage& msg);
    static bool is_response(const JsonRpcMessage& msg);
    static bool is_notification(const JsonRpcMessage& msg);
};
```

### **Phase 2: Core Protocol (Week 2)**

#### **Step 2.1: Minimal LSP Server**
```cpp
class LanguageServer {
private:
    std::unique_ptr<Transport> transport_;
    enum class State { Uninitialized, Initializing, Running, ShuttingDown };
    State state_ = State::Uninitialized;
    
public:
    bool start();
    void stop();
    
private:
    void handle_message(const JsonRpcMessage& message);
    void handle_initialize(const JsonRpcMessage& request);
    void handle_initialized(const JsonRpcMessage& notification);
    void handle_shutdown(const JsonRpcMessage& request);
    void handle_exit(const JsonRpcMessage& notification);
};
```

#### **Step 2.2: Implement Initialize Handshake CORRECTLY**
```cpp
void LanguageServer::handle_initialize(const JsonRpcMessage& request) {
    // Parse client capabilities
    auto client_caps = parse_client_capabilities(request.params);
    
    // Build server capabilities
    auto server_caps = build_server_capabilities();
    
    // Send response
    JsonRpcMessage response;
    response.id = request.id;
    response.result = server_caps;
    
    send_message(response);
    
    // IMPORTANT: Don't set state to Running yet!
    // Wait for 'initialized' notification
    state_ = State::Initializing;
}

void LanguageServer::handle_initialized(const JsonRpcMessage& notification) {
    // NOW we're fully initialized
    state_ = State::Running;
    logger_.info("LSP handshake complete - server ready");
}
```

### **Phase 3: Language Services (Week 3)**

#### **Step 3.1: Document Management**
```cpp
class DocumentManager {
public:
    void did_open(const std::string& uri, const std::string& content);
    void did_change(const std::string& uri, const std::vector<TextEdit>& changes);
    void did_close(const std::string& uri);
    
    std::optional<std::string> get_document_content(const std::string& uri);
    
private:
    std::unordered_map<std::string, Document> documents_;
};
```

#### **Step 3.2: Hover Provider Integration**
```cpp
class HoverProvider {
public:
    HoverProvider(CryoCompiler& compiler) : compiler_(compiler) {}
    
    std::optional<Hover> provide_hover(
        const std::string& uri, 
        const Position& position
    );
    
private:
    CryoCompiler& compiler_;
    
    // Use your existing compiler to analyze symbols at position
    SymbolInfo analyze_symbol_at_position(const std::string& content, Position pos);
};
```

### **Phase 4: Testing & Validation (Week 4)**

#### **Step 4.1: Unit Tests**
```cpp
// Test JSON-RPC parsing
TEST(JsonRpcParser, ParseValidRequest) {
    std::string json = R"({"jsonrpc":"2.0","id":1,"method":"test","params":{}})";
    auto msg = JsonRpcParser::parse(json);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->method.value(), "test");
}

// Test LSP handshake
TEST(LanguageServer, InitializeHandshake) {
    MockTransport transport;
    LanguageServer server(std::make_unique<MockTransport>(transport));
    
    // Send initialize request
    // Verify initialize response  
    // Send initialized notification
    // Verify server is in Running state
}
```

#### **Step 4.2: Integration Tests**
Create a test harness that simulates VS Code:
```cpp
class LSPTestClient {
public:
    bool connect_to_server(const std::string& command);
    void send_initialize();
    void send_initialized();
    void open_document(const std::string& uri, const std::string& content);
    HoverResponse request_hover(const std::string& uri, Position pos);
};
```

### **Phase 5: Advanced Features (Week 5+)**

Once the foundation is solid:
- Code completion
- Go to definition  
- Find references
- Diagnostics (error reporting)
- Document symbols
- Workspace symbols

---

## 🛠️ **Key Implementation Guidelines**

### **1. Use Modern C++ Best Practices**
```cpp
// Use smart pointers
std::unique_ptr<Transport> transport_;

// Use RAII for resource management  
class MessageBuffer {
    ~MessageBuffer() { /* cleanup */ }
};

// Use structured bindings and std::optional
if (auto message = parser.parse(json)) {
    handle_message(*message);
}
```

### **2. Comprehensive Logging**
```cpp
class Logger {
public:
    enum Level { Debug, Info, Warn, Error };
    
    template<typename... Args>
    void log(Level level, const std::string& component, 
             const std::string& format, Args&&... args);
    
    void set_file_output(const std::string& filepath);
    void set_console_output(bool enabled);
};

// Usage:
logger_.info("Protocol", "Received initialize request from client");
logger_.debug("Transport", "Message length: {}", message.length());
```

### **3. Error Recovery**
```cpp
class MessageHandler {
    enum class ParseResult { Success, MalformedJson, IncompleteMessage, UnknownMethod };
    
    ParseResult handle_raw_message(const std::string& raw) {
        try {
            auto message = JsonRpcParser::parse(raw);
            if (!message) return ParseResult::MalformedJson;
            
            dispatch_message(*message);
            return ParseResult::Success;
            
        } catch (const std::exception& e) {
            logger_.error("Parser", "Exception: {}", e.what());
            return ParseResult::MalformedJson;
        }
    }
};
```

### **4. Configuration & Debugging**
```cpp
struct LSPConfig {
    bool enable_debug_logging = false;
    std::string log_file_path = "cryo-lsp.log";
    int socket_port = 7777;
    bool use_stdio = true;
    
    static LSPConfig from_args(int argc, char* argv[]);
    static LSPConfig from_file(const std::string& config_file);
};
```

---

## 🎯 **Success Criteria**

Before considering the LSP "done":

1. **✅ Handshake Test**: Initialize → Initialized → Ready state works 100% of the time
2. **✅ Document Lifecycle**: Open/Change/Close documents work reliably  
3. **✅ Hover Functionality**: Hover works for basic types (int, string, etc.)
4. **✅ Error Recovery**: Server doesn't crash on malformed messages
5. **✅ Performance**: Responses within 100ms for simple hover requests
6. **✅ Stress Test**: Can handle 1000+ messages without issues

---