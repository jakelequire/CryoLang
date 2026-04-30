/**
 * CryoLang WebAssembly JavaScript Interop Library
 * 
 * This library provides the JavaScript side of the Cryo-WASM bridge,
 * allowing CryoLang code to call JavaScript functions and vice versa.
 */

var CryoLibrary = {
    // ================================================================
    // Console and Debugging Functions
    // ================================================================
    
    cryo_js_console_log: function(str_ptr) {
        var str = UTF8ToString(str_ptr);
        console.log('[Cryo]: ' + str);
    },

    cryo_js_console_error: function(str_ptr) {
        var str = UTF8ToString(str_ptr);
        console.error('[Cryo Error]: ' + str);
    },

    cryo_js_alert: function(str_ptr) {
        var str = UTF8ToString(str_ptr);
        alert(str);
    },

    // ================================================================
    // Timing Functions
    // ================================================================

    cryo_js_get_timestamp: function() {
        return Date.now();
    },

    cryo_js_get_performance_now: function() {
        return performance.now();
    },

    // ================================================================
    // DOM Manipulation Functions
    // ================================================================

    cryo_js_get_element_by_id: function(id_ptr) {
        var id = UTF8ToString(id_ptr);
        var element = document.getElementById(id);
        return element ? 1 : 0; // Return 1 if found, 0 if not
    },

    cryo_js_set_element_text: function(id_ptr, text_ptr) {
        var id = UTF8ToString(id_ptr);
        var text = UTF8ToString(text_ptr);
        var element = document.getElementById(id);
        if (element) {
            element.textContent = text;
            return 1;
        }
        return 0;
    },

    cryo_js_get_element_text: function(id_ptr, buffer_ptr, buffer_size) {
        var id = UTF8ToString(id_ptr);
        var element = document.getElementById(id);
        if (element) {
            var text = element.textContent || '';
            var bytes = lengthBytesUTF8(text) + 1;
            if (bytes <= buffer_size) {
                stringToUTF8(text, buffer_ptr, buffer_size);
                return bytes - 1; // Return length without null terminator
            }
        }
        return -1; // Error or buffer too small
    },

    // ================================================================
    // HTTP/Fetch Functions
    // ================================================================

    cryo_js_fetch_text: function(url_ptr, callback_ptr) {
        var url = UTF8ToString(url_ptr);
        
        fetch(url)
            .then(response => response.text())
            .then(text => {
                // Allocate WASM memory for the response
                var len = lengthBytesUTF8(text) + 1;
                var response_ptr = _cryo_wasm_alloc(len);
                stringToUTF8(text, response_ptr, len);
                
                // Call the callback with the response pointer
                dynCall_vi(callback_ptr, response_ptr);
            })
            .catch(error => {
                console.error('Fetch error:', error);
                // Call callback with null pointer to indicate error
                dynCall_vi(callback_ptr, 0);
            });
        
        return 1; // Indicates async operation started
    },

    // ================================================================
    // Local Storage Functions
    // ================================================================

    cryo_js_local_storage_set: function(key_ptr, value_ptr) {
        var key = UTF8ToString(key_ptr);
        var value = UTF8ToString(value_ptr);
        try {
            localStorage.setItem(key, value);
            return 1;
        } catch (e) {
            console.error('localStorage.setItem failed:', e);
            return 0;
        }
    },

    cryo_js_local_storage_get: function(key_ptr, buffer_ptr, buffer_size) {
        var key = UTF8ToString(key_ptr);
        try {
            var value = localStorage.getItem(key);
            if (value === null) {
                return -1; // Key not found
            }
            
            var bytes = lengthBytesUTF8(value) + 1;
            if (bytes <= buffer_size) {
                stringToUTF8(value, buffer_ptr, buffer_size);
                return bytes - 1; // Return length without null terminator
            } else {
                return -2; // Buffer too small
            }
        } catch (e) {
            console.error('localStorage.getItem failed:', e);
            return -3; // Error
        }
    },

    cryo_js_local_storage_remove: function(key_ptr) {
        var key = UTF8ToString(key_ptr);
        try {
            localStorage.removeItem(key);
            return 1;
        } catch (e) {
            console.error('localStorage.removeItem failed:', e);
            return 0;
        }
    },

    // ================================================================
    // Canvas Functions (for graphics/games)
    // ================================================================

    cryo_js_get_canvas_context: function(canvas_id_ptr) {
        var canvasId = UTF8ToString(canvas_id_ptr);
        var canvas = document.getElementById(canvasId);
        if (!canvas) return 0;
        
        var ctx = canvas.getContext('2d');
        if (!ctx) return 0;
        
        // Store context in a global array and return index
        if (!Module.canvasContexts) {
            Module.canvasContexts = [];
        }
        
        var index = Module.canvasContexts.length;
        Module.canvasContexts.push(ctx);
        return index + 1; // Return 1-based index (0 means error)
    },

    cryo_js_canvas_fill_rect: function(ctx_index, x, y, width, height) {
        if (!Module.canvasContexts || ctx_index < 1 || ctx_index > Module.canvasContexts.length) {
            return 0;
        }
        
        var ctx = Module.canvasContexts[ctx_index - 1];
        ctx.fillRect(x, y, width, height);
        return 1;
    },

    cryo_js_canvas_set_fill_color: function(ctx_index, color_ptr) {
        if (!Module.canvasContexts || ctx_index < 1 || ctx_index > Module.canvasContexts.length) {
            return 0;
        }
        
        var color = UTF8ToString(color_ptr);
        var ctx = Module.canvasContexts[ctx_index - 1];
        ctx.fillStyle = color;
        return 1;
    },

    // ================================================================
    // Event Handling
    // ================================================================

    cryo_js_add_event_listener: function(element_id_ptr, event_type_ptr, callback_ptr) {
        var elementId = UTF8ToString(element_id_ptr);
        var eventType = UTF8ToString(event_type_ptr);
        var element = document.getElementById(elementId);
        
        if (!element) return 0;
        
        element.addEventListener(eventType, function(event) {
            // Create a simple event object in WASM memory
            var event_data_ptr = _cryo_wasm_alloc(32); // Allocate space for event data
            
            // Store basic event info (simplified)
            HEAP32[event_data_ptr >> 2] = event.clientX || 0;
            HEAP32[(event_data_ptr + 4) >> 2] = event.clientY || 0;
            HEAP32[(event_data_ptr + 8) >> 2] = event.keyCode || 0;
            
            // Call the Cryo callback
            dynCall_vi(callback_ptr, event_data_ptr);
            
            // Free the event data
            _cryo_wasm_free(event_data_ptr);
        });
        
        return 1;
    },

    // ================================================================
    // Math Functions (for high-precision operations)
    // ================================================================

    cryo_js_math_random: function() {
        return Math.random();
    },

    cryo_js_math_sin: function(x) {
        return Math.sin(x);
    },

    cryo_js_math_cos: function(x) {
        return Math.cos(x);
    },

    cryo_js_math_sqrt: function(x) {
        return Math.sqrt(x);
    }
};

// Merge our library with Emscripten's library
mergeInto(LibraryManager.library, CryoLibrary);