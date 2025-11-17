#include "Compiler/CompilerInstance.hpp"
#include "wasm/WASMRuntimeAdapter.hpp"
#include <emscripten.h>
#include <string>
#include <memory>

/**
 * CryoLang WASM API
 * 
 * This file provides the high-level API that JavaScript can use
 * to interact with the CryoLang compiler in WebAssembly.
 */

namespace {
    // Global compiler instance for WASM
    std::unique_ptr<Cryo::CompilerInstance> g_compiler = nullptr;
    std::string g_last_error;
    std::string g_last_ast;
}

extern "C" {

    //===================================================================
    // Compiler Control Functions
    //===================================================================

    EMSCRIPTEN_KEEPALIVE
    int cryo_compile_source(const char* source_code)
    {
        if (!source_code) {
            g_last_error = "Source code is null";
            return 0;
        }

        try {
            if (!g_compiler) {
                g_compiler = std::make_unique<Cryo::CompilerInstance>();
            }

            // Create a temporary file name for WASM compilation
            std::string temp_filename = "wasm_source.cryo";
            
            // Compile the source code
            bool success = g_compiler->compile_from_string(source_code, temp_filename);
            
            if (!success) {
                g_last_error = "Compilation failed";
                return 0;
            }

            return 1; // Success
        }
        catch (const std::exception& e) {
            g_last_error = std::string("Exception during compilation: ") + e.what();
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE
    const char* cryo_get_last_error()
    {
        return g_last_error.c_str();
    }

    EMSCRIPTEN_KEEPALIVE
    int cryo_compile_to_wasm(const char* source_code, const char* output_path)
    {
        if (!source_code || !output_path) {
            g_last_error = "Invalid parameters";
            return 0;
        }

        try {
            if (!g_compiler) {
                g_compiler = std::make_unique<Cryo::CompilerInstance>();
            }

            // Set WASM target
            g_compiler->set_target_triple("wasm32-unknown-emscripten");
            
            // Compile to WASM
            std::string temp_filename = "wasm_source.cryo";
            bool success = g_compiler->compile_to_file(source_code, temp_filename, output_path);
            
            if (!success) {
                g_last_error = "WASM compilation failed";
                return 0;
            }

            return 1; // Success
        }
        catch (const std::exception& e) {
            g_last_error = std::string("Exception during WASM compilation: ") + e.what();
            return 0;
        }
    }

    //===================================================================
    // AST and Analysis Functions
    //===================================================================

    EMSCRIPTEN_KEEPALIVE
    const char* cryo_get_ast(const char* source_code)
    {
        if (!source_code) {
            g_last_error = "Source code is null";
            return nullptr;
        }

        try {
            if (!g_compiler) {
                g_compiler = std::make_unique<Cryo::CompilerInstance>();
            }

            // Parse and get AST
            std::string ast_dump = g_compiler->get_ast_dump(source_code, "wasm_source.cryo");
            g_last_ast = ast_dump;
            
            return g_last_ast.c_str();
        }
        catch (const std::exception& e) {
            g_last_error = std::string("Exception during AST generation: ") + e.what();
            return nullptr;
        }
    }

    EMSCRIPTEN_KEEPALIVE
    int cryo_validate_syntax(const char* source_code)
    {
        if (!source_code) {
            g_last_error = "Source code is null";
            return 0;
        }

        try {
            if (!g_compiler) {
                g_compiler = std::make_unique<Cryo::CompilerInstance>();
            }

            // Validate syntax only (no code generation)
            bool valid = g_compiler->validate_syntax(source_code, "wasm_source.cryo");
            
            if (!valid) {
                g_last_error = "Syntax validation failed";
                return 0;
            }

            return 1; // Valid syntax
        }
        catch (const std::exception& e) {
            g_last_error = std::string("Exception during syntax validation: ") + e.what();
            return 0;
        }
    }

    //===================================================================
    // Diagnostic Functions
    //===================================================================

    EMSCRIPTEN_KEEPALIVE
    int cryo_get_error_count()
    {
        if (!g_compiler) {
            return 0;
        }

        try {
            return g_compiler->get_diagnostic_count();
        }
        catch (...) {
            return -1; // Error getting error count
        }
    }

    EMSCRIPTEN_KEEPALIVE
    const char* cryo_get_error_message(int index)
    {
        if (!g_compiler) {
            return nullptr;
        }

        try {
            std::string error_msg = g_compiler->get_diagnostic_message(index);
            // Store in a static buffer (not thread-safe, but fine for WASM single-thread)
            static std::string error_buffer;
            error_buffer = error_msg;
            return error_buffer.c_str();
        }
        catch (...) {
            return nullptr;
        }
    }

    //===================================================================
    // Configuration Functions
    //===================================================================

    EMSCRIPTEN_KEEPALIVE
    void cryo_set_optimization_level(int level)
    {
        if (g_compiler) {
            g_compiler->set_optimization_level(level);
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void cryo_enable_debug_info(int enable)
    {
        if (g_compiler) {
            g_compiler->enable_debug_info(enable != 0);
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void cryo_set_target_triple(const char* triple)
    {
        if (g_compiler && triple) {
            g_compiler->set_target_triple(triple);
        }
    }

    //===================================================================
    // Cleanup Functions
    //===================================================================

    EMSCRIPTEN_KEEPALIVE
    void cryo_cleanup()
    {
        g_compiler.reset();
        g_last_error.clear();
        g_last_ast.clear();
    }

    //===================================================================
    // Version and Info Functions
    //===================================================================

    EMSCRIPTEN_KEEPALIVE
    const char* cryo_get_version()
    {
        return "CryoLang 0.1.0 (WebAssembly)";
    }

    EMSCRIPTEN_KEEPALIVE
    const char* cryo_get_supported_targets()
    {
        return "wasm32-unknown-emscripten,wasm64-unknown-emscripten,native";
    }

    //===================================================================
    // Interactive Features for Playground
    //===================================================================

    EMSCRIPTEN_KEEPALIVE
    int cryo_format_source(const char* source_code, char* output_buffer, int buffer_size)
    {
        if (!source_code || !output_buffer || buffer_size <= 0) {
            return 0;
        }

        try {
            // Simple formatter - in reality, you'd implement a proper formatter
            std::string formatted = source_code; // Placeholder
            
            if (formatted.length() >= static_cast<size_t>(buffer_size)) {
                return 0; // Buffer too small
            }

            strcpy(output_buffer, formatted.c_str());
            return formatted.length();
        }
        catch (...) {
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE
    int cryo_get_completion_suggestions(const char* source_code, int cursor_position, 
                                        char* suggestions_buffer, int buffer_size)
    {
        // Placeholder for autocomplete functionality
        // In a full implementation, this would analyze the AST and provide intelligent suggestions
        if (!source_code || !suggestions_buffer || buffer_size <= 0) {
            return 0;
        }

        const char* basic_suggestions = "function,const,mut,class,struct,enum,if,else,while,for,match,return";
        
        if (strlen(basic_suggestions) >= static_cast<size_t>(buffer_size)) {
            return 0;
        }

        strcpy(suggestions_buffer, basic_suggestions);
        return strlen(basic_suggestions);
    }

} // extern "C"