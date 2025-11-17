#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Cryo::WASM
{
    /**
     * @brief WebAssembly Runtime Adapter
     * 
     * This class handles the adaptation of CryoLang's native runtime 
     * to WebAssembly environment. It manages memory allocation through
     * WASM linear memory and provides JavaScript interop capabilities.
     */
    class WASMRuntimeAdapter
    {
    public:
        //===================================================================
        // Construction & Configuration
        //===================================================================

        WASMRuntimeAdapter();
        ~WASMRuntimeAdapter();

        /**
         * @brief Initialize the WASM runtime adapter
         * @param heap_size Initial heap size in bytes
         * @param enable_js_interop Enable JavaScript interoperability
         * @return Success status
         */
        bool initialize(size_t heap_size = 16 * 1024 * 1024, bool enable_js_interop = true);

        /**
         * @brief Shutdown the runtime adapter
         */
        void shutdown();

        //===================================================================
        // Memory Management Interface
        //===================================================================

        /**
         * @brief Allocate memory in WASM linear memory
         * @param size Size in bytes
         * @return Pointer offset in linear memory (0 = failure)
         */
        uint32_t wasm_alloc(size_t size);

        /**
         * @brief Free memory in WASM linear memory
         * @param ptr Pointer offset
         */
        void wasm_free(uint32_t ptr);

        /**
         * @brief Get current heap statistics
         */
        struct HeapStats {
            size_t total_size;
            size_t allocated_bytes;
            size_t free_bytes;
            size_t allocation_count;
        };
        HeapStats get_heap_stats() const;

        //===================================================================
        // JavaScript Interop
        //===================================================================

        /**
         * @brief Register a JavaScript function that can be called from Cryo
         * @param name Function name
         * @param signature Function signature (e.g., "ii" = int(int))
         */
        void register_js_function(const std::string& name, const std::string& signature);

        /**
         * @brief Export a Cryo function to JavaScript
         * @param cryo_name Internal function name
         * @param js_name JavaScript-visible name
         * @param signature Function signature
         */
        void export_function(const std::string& cryo_name, const std::string& js_name, const std::string& signature);

        //===================================================================
        // String Handling
        //===================================================================

        /**
         * @brief Copy string from JavaScript to WASM memory
         * @param js_string_ptr JavaScript string pointer
         * @return WASM memory offset
         */
        uint32_t copy_string_from_js(const char* js_string_ptr);

        /**
         * @brief Copy string from WASM memory to JavaScript
         * @param wasm_offset WASM memory offset
         * @return String length (JavaScript should read this many bytes)
         */
        size_t copy_string_to_js(uint32_t wasm_offset, char* js_buffer, size_t buffer_size);

        //===================================================================
        // Error Handling
        //===================================================================

        bool has_errors() const { return !_errors.empty(); }
        const std::vector<std::string>& get_errors() const { return _errors; }
        void clear_errors() { _errors.clear(); }

    private:
        //===================================================================
        // Internal State
        //===================================================================

        bool _initialized = false;
        bool _js_interop_enabled = false;
        size_t _heap_size = 0;
        uint32_t _heap_start = 0;
        uint32_t _heap_current = 0;

        // Error tracking
        std::vector<std::string> _errors;

        // JavaScript function registry
        std::unordered_map<std::string, std::string> _js_functions;
        std::unordered_map<std::string, std::pair<std::string, std::string>> _exported_functions;

        //===================================================================
        // Internal Methods
        //===================================================================

        void report_error(const std::string& message);
        uint32_t align_pointer(uint32_t ptr, size_t alignment = 8);
    };

    //===================================================================
    // C-style exports for WASM (extern "C")
    //===================================================================

    extern "C" {
        // Memory management exports
        uint32_t cryo_wasm_alloc(size_t size);
        void cryo_wasm_free(uint32_t ptr);
        
        // String handling exports
        uint32_t cryo_wasm_string_from_js(const char* str);
        size_t cryo_wasm_string_to_js(uint32_t offset, char* buffer, size_t buffer_size);
        
        // Runtime control exports
        int cryo_wasm_initialize(size_t heap_size);
        void cryo_wasm_shutdown();
        
        // Statistics exports
        size_t cryo_wasm_get_heap_size();
        size_t cryo_wasm_get_allocated_bytes();
    }

} // namespace Cryo::WASM