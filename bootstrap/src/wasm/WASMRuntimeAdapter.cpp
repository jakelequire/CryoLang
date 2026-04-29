#include "wasm/WASMRuntimeAdapter.hpp"
#include <algorithm>
#include <cstring>
#include <emscripten.h>

namespace Cryo::WASM
{
    // Global instance for C-style exports
    static std::unique_ptr<WASMRuntimeAdapter> g_wasm_adapter = nullptr;

    //===================================================================
    // WASMRuntimeAdapter Implementation
    //===================================================================

    WASMRuntimeAdapter::WASMRuntimeAdapter() = default;

    WASMRuntimeAdapter::~WASMRuntimeAdapter()
    {
        if (_initialized)
        {
            shutdown();
        }
    }

    bool WASMRuntimeAdapter::initialize(size_t heap_size, bool enable_js_interop)
    {
        if (_initialized)
        {
            report_error("WASMRuntimeAdapter already initialized");
            return false;
        }

        _heap_size = heap_size;
        _js_interop_enabled = enable_js_interop;

        // Allocate heap in WASM linear memory
        // Note: In Emscripten, we can use malloc for simplicity
        // In production, you might want to manage linear memory directly
        _heap_start = reinterpret_cast<uint32_t>(malloc(heap_size));
        if (_heap_start == 0)
        {
            report_error("Failed to allocate WASM heap");
            return false;
        }

        _heap_current = _heap_start;
        _initialized = true;

        // Register essential JavaScript functions if interop is enabled
        if (_js_interop_enabled)
        {
            register_js_function("console_log", "vi");     // void console_log(int str_ptr)
            register_js_function("js_alert", "vi");        // void js_alert(int str_ptr)
            register_js_function("get_timestamp", "d");    // double get_timestamp()
        }

        return true;
    }

    void WASMRuntimeAdapter::shutdown()
    {
        if (!_initialized)
            return;

        if (_heap_start != 0)
        {
            free(reinterpret_cast<void*>(_heap_start));
            _heap_start = 0;
        }

        _heap_current = 0;
        _initialized = false;
        _js_functions.clear();
        _exported_functions.clear();
    }

    uint32_t WASMRuntimeAdapter::wasm_alloc(size_t size)
    {
        if (!_initialized)
        {
            report_error("WASMRuntimeAdapter not initialized");
            return 0;
        }

        // Simple bump allocator for now
        // In production, you'd want a more sophisticated allocator
        size_t aligned_size = (size + 7) & ~7; // 8-byte alignment
        
        if (_heap_current + aligned_size > _heap_start + _heap_size)
        {
            report_error("WASM heap exhausted");
            return 0;
        }

        uint32_t result = _heap_current;
        _heap_current += aligned_size;
        return result;
    }

    void WASMRuntimeAdapter::wasm_free(uint32_t ptr)
    {
        // Simple bump allocator doesn't support individual frees
        // In production, implement a proper free-list allocator
        // For now, just validate the pointer is in our heap range
        if (ptr < _heap_start || ptr >= _heap_start + _heap_size)
        {
            report_error("Invalid WASM pointer in free");
        }
    }

    WASMRuntimeAdapter::HeapStats WASMRuntimeAdapter::get_heap_stats() const
    {
        HeapStats stats = {};
        if (_initialized)
        {
            stats.total_size = _heap_size;
            stats.allocated_bytes = _heap_current - _heap_start;
            stats.free_bytes = _heap_size - stats.allocated_bytes;
            stats.allocation_count = 1; // Simplified for bump allocator
        }
        return stats;
    }

    void WASMRuntimeAdapter::register_js_function(const std::string& name, const std::string& signature)
    {
        _js_functions[name] = signature;
    }

    void WASMRuntimeAdapter::export_function(const std::string& cryo_name, const std::string& js_name, const std::string& signature)
    {
        _exported_functions[js_name] = {cryo_name, signature};
    }

    uint32_t WASMRuntimeAdapter::copy_string_from_js(const char* js_string_ptr)
    {
        if (!js_string_ptr)
            return 0;

        size_t len = strlen(js_string_ptr);
        uint32_t wasm_ptr = wasm_alloc(len + 1); // +1 for null terminator
        
        if (wasm_ptr != 0)
        {
            memcpy(reinterpret_cast<void*>(wasm_ptr), js_string_ptr, len + 1);
        }

        return wasm_ptr;
    }

    size_t WASMRuntimeAdapter::copy_string_to_js(uint32_t wasm_offset, char* js_buffer, size_t buffer_size)
    {
        if (wasm_offset == 0 || js_buffer == nullptr || buffer_size == 0)
            return 0;

        const char* wasm_str = reinterpret_cast<const char*>(wasm_offset);
        size_t str_len = strlen(wasm_str);
        size_t copy_len = std::min(str_len, buffer_size - 1);

        memcpy(js_buffer, wasm_str, copy_len);
        js_buffer[copy_len] = '\0';

        return copy_len;
    }

    void WASMRuntimeAdapter::report_error(const std::string& message)
    {
        _errors.push_back(message);
    }

    uint32_t WASMRuntimeAdapter::align_pointer(uint32_t ptr, size_t alignment)
    {
        return (ptr + alignment - 1) & ~(alignment - 1);
    }

    //===================================================================
    // C-style exports for WASM
    //===================================================================

    extern "C" {
        
        uint32_t cryo_wasm_alloc(size_t size)
        {
            if (g_wasm_adapter)
                return g_wasm_adapter->wasm_alloc(size);
            return 0;
        }

        void cryo_wasm_free(uint32_t ptr)
        {
            if (g_wasm_adapter)
                g_wasm_adapter->wasm_free(ptr);
        }

        uint32_t cryo_wasm_string_from_js(const char* str)
        {
            if (g_wasm_adapter)
                return g_wasm_adapter->copy_string_from_js(str);
            return 0;
        }

        size_t cryo_wasm_string_to_js(uint32_t offset, char* buffer, size_t buffer_size)
        {
            if (g_wasm_adapter)
                return g_wasm_adapter->copy_string_to_js(offset, buffer, buffer_size);
            return 0;
        }

        int cryo_wasm_initialize(size_t heap_size)
        {
            if (!g_wasm_adapter)
            {
                g_wasm_adapter = std::make_unique<WASMRuntimeAdapter>();
            }
            return g_wasm_adapter->initialize(heap_size, true) ? 1 : 0;
        }

        void cryo_wasm_shutdown()
        {
            if (g_wasm_adapter)
            {
                g_wasm_adapter->shutdown();
                g_wasm_adapter.reset();
            }
        }

        size_t cryo_wasm_get_heap_size()
        {
            if (g_wasm_adapter)
            {
                auto stats = g_wasm_adapter->get_heap_stats();
                return stats.total_size;
            }
            return 0;
        }

        size_t cryo_wasm_get_allocated_bytes()
        {
            if (g_wasm_adapter)
            {
                auto stats = g_wasm_adapter->get_heap_stats();
                return stats.allocated_bytes;
            }
            return 0;
        }

        // Emscripten-specific exports
        EMSCRIPTEN_KEEPALIVE
        void cryo_main()
        {
            // Entry point that can be called from JavaScript
            // Initialize the WASM runtime and run user code
            if (cryo_wasm_initialize(16 * 1024 * 1024))
            {
                // Call user's main function here
                // This would be generated by the compiler
            }
        }

    } // extern "C"

} // namespace Cryo::WASM