#include "Compiler/CHeaderProcessor.hpp"
#include "Types/TypeArena.hpp"
#include "Utils/Logger.hpp"

#include <cstdio>
#include <filesystem>
#include <array>
#include <sstream>

namespace Cryo
{
    // ========================================================================
    // Public API
    // ========================================================================

    std::vector<std::unique_ptr<FunctionDeclarationNode>> CHeaderProcessor::process_header(
        const std::string &include_path,
        const std::string &source_dir,
        TypeArena &arena)
    {
        // 1. Resolve include path
        std::filesystem::path resolved;
        if (std::filesystem::path(include_path).is_absolute())
        {
            resolved = include_path;
        }
        else
        {
            resolved = std::filesystem::path(source_dir) / include_path;
        }

        resolved = std::filesystem::canonical(resolved);
        std::string resolved_str = resolved.string();

        LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Resolved include path: {}", resolved_str);

        if (!std::filesystem::exists(resolved))
        {
            LOG_WARN(LogComponent::GENERAL, "CHeaderProcessor: Header file not found: {}", resolved_str);
            return {};
        }

        // 2. Run preprocessor
        std::string preprocessed = run_preprocessor(resolved_str);
        if (preprocessed.empty())
        {
            LOG_WARN(LogComponent::GENERAL, "CHeaderProcessor: Preprocessor returned empty output for: {}", resolved_str);
            return {};
        }

        LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Preprocessed output ({} bytes)", preprocessed.size());

        // 3. Parse preprocessed C
        CParser parser;
        auto c_decls = parser.parse(preprocessed);

        // Capture type information for resolving typedefs and enum types
        _typedefs = parser.typedefs();
        _enum_names = parser.enum_names();
        _struct_names = parser.struct_names();

        LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: CParser found {} function declarations, {} typedefs, {} enum names, {} struct names",
                  c_decls.size(), _typedefs.size(), _enum_names.size(), _struct_names.size());

        // 4. Create AST nodes
        std::vector<std::unique_ptr<FunctionDeclarationNode>> result;
        for (const auto &decl : c_decls)
        {
            auto node = create_function_node(decl, arena);
            if (node)
            {
                LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Created function node: {}", decl.name);
                result.push_back(std::move(node));
            }
        }

        return result;
    }

    std::string CHeaderProcessor::find_matching_c_file(const std::string &header_path)
    {
        std::filesystem::path hpath(header_path);
        std::filesystem::path c_path = hpath.parent_path() / (hpath.stem().string() + ".c");

        if (std::filesystem::exists(c_path))
        {
            return c_path.generic_string();
        }
        return "";
    }

    // ========================================================================
    // Preprocessor
    // ========================================================================

    std::string CHeaderProcessor::run_preprocessor(const std::string &resolved_path)
    {
        // Build command: clang -E -P <path>
        // -E: preprocess only
        // -P: omit #line directives

        // Use generic_string() to normalize backslashes to forward slashes,
        // avoiding shell escape-sequence corruption on Windows.
        std::string normalized = std::filesystem::path(resolved_path).generic_string();

#if defined(_WIN32) || defined(_WIN64)
        std::string cmd = "C:/msys64/mingw64/bin/clang -E -P \"" + normalized + "\" 2>NUL";
#else
        std::string cmd = "clang-20 -E -P \"" + normalized + "\" 2>/dev/null";
#endif

        LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Running preprocessor: {}", cmd);

        // Use popen to capture output
        std::array<char, 4096> buffer;
        std::string output;

        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe)
        {
            LOG_WARN(LogComponent::GENERAL, "CHeaderProcessor: Failed to run clang-20 preprocessor");
            return "";
        }

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (status != 0)
        {
            LOG_WARN(LogComponent::GENERAL, "CHeaderProcessor: clang-20 preprocessor exited with status {}", status);
            // Still return output — partial preprocessing may be useful
        }

        return output;
    }

    // ========================================================================
    // C-to-Cryo Type Mapping
    // ========================================================================

    std::string CHeaderProcessor::resolve_typedef(const std::string &type_name) const
    {
        std::string resolved = type_name;
        int depth = 0;
        while (depth < 10) // prevent infinite loops
        {
            auto it = _typedefs.find(resolved);
            if (it == _typedefs.end())
                break;
            resolved = it->second;
            depth++;
        }
        return resolved;
    }

    TypeRef CHeaderProcessor::map_c_type(const std::string &c_type, TypeArena &arena)
    {
        // Normalize: strip leading/trailing whitespace
        std::string type = c_type;
        while (!type.empty() && type.front() == ' ')
            type.erase(type.begin());
        while (!type.empty() && type.back() == ' ')
            type.pop_back();

        // Resolve typedefs first (e.g., "FooRef" -> "struct Foo *")
        std::string resolved = resolve_typedef(type);
        if (resolved != type)
        {
            LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Resolved typedef '{}' -> '{}'", type, resolved);
            return map_c_type(resolved, arena);
        }

        // Check if this is a known enum type name → map to i32
        if (_enum_names.count(type))
        {
            LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Mapped enum type '{}' to i32", type);
            return arena.get_i32();
        }

        // Check for pointer types (ends with *)
        if (!type.empty() && type.back() == '*')
        {
            // Strip the trailing * and any whitespace
            std::string pointee = type.substr(0, type.size() - 1);
            while (!pointee.empty() && pointee.back() == ' ')
                pointee.pop_back();

            // Strip const from pointee for ABI purposes
            // "const char" -> "char"
            if (pointee.substr(0, 6) == "const ")
                pointee = pointee.substr(6);

            TypeRef pointee_type = map_c_type(pointee, arena);
            return arena.get_pointer_to(pointee_type);
        }

        // Strip qualifiers for matching
        std::string stripped = type;
        // Remove 'const ', 'volatile ', 'static ', 'extern ', 'inline '
        auto strip_prefix = [&](const std::string &prefix)
        {
            while (stripped.find(prefix) == 0)
                stripped = stripped.substr(prefix.size());
        };
        strip_prefix("const ");
        strip_prefix("volatile ");
        strip_prefix("static ");
        strip_prefix("extern ");
        strip_prefix("inline ");
        strip_prefix("signed ");

        // Map base types
        if (stripped == "void")
            return arena.get_void();
        if (stripped == "int")
            return arena.get_i32();
        if (stripped == "unsigned int" || stripped == "unsigned")
            return arena.get_u32();
        if (stripped == "char")
            return arena.get_i8();
        if (stripped == "unsigned char")
            return arena.get_u8();
        if (stripped == "short" || stripped == "short int")
            return arena.get_i16();
        if (stripped == "unsigned short" || stripped == "unsigned short int")
            return arena.get_u16();
        if (stripped == "long" || stripped == "long int" || stripped == "long long" || stripped == "long long int")
            return arena.get_i64();
        if (stripped == "unsigned long" || stripped == "unsigned long int" || stripped == "unsigned long long" || stripped == "unsigned long long int")
            return arena.get_u64();
        if (stripped == "float")
            return arena.get_f32();
        if (stripped == "double" || stripped == "long double")
            return arena.get_f64();
        if (stripped == "_Bool")
            return arena.get_bool();
        if (stripped == "size_t")
            return arena.get_u64();

        // Fixed-width integer types
        if (stripped == "int8_t")
            return arena.get_i8();
        if (stripped == "int16_t")
            return arena.get_i16();
        if (stripped == "int32_t")
            return arena.get_i32();
        if (stripped == "int64_t")
            return arena.get_i64();
        if (stripped == "uint8_t")
            return arena.get_u8();
        if (stripped == "uint16_t")
            return arena.get_u16();
        if (stripped == "uint32_t")
            return arena.get_u32();
        if (stripped == "uint64_t")
            return arena.get_u64();

        // Check if this is an enum type by prefix (e.g., "enum Foo")
        if (stripped.substr(0, 5) == "enum ")
        {
            LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Mapped enum type '{}' to i32", c_type);
            return arena.get_i32();
        }

        // Check if this is a known struct name (without "struct " prefix)
        if (_struct_names.count(stripped))
        {
            LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Mapped struct type '{}' to opaque void*", c_type);
            return arena.get_pointer_to(arena.get_void());
        }

        // Unknown types — treat as opaque pointer (void *)
        LOG_DEBUG(LogComponent::GENERAL, "CHeaderProcessor: Unknown C type '{}', mapping to void*", c_type);
        return arena.get_pointer_to(arena.get_void());
    }

    // ========================================================================
    // AST Node Creation
    // ========================================================================

    std::unique_ptr<FunctionDeclarationNode> CHeaderProcessor::create_function_node(
        const CFunctionDecl &decl,
        TypeArena &arena)
    {
        SourceLocation loc; // Default source location for generated nodes

        // Map return type
        TypeRef return_type = map_c_type(decl.return_type, arena);

        // Create function declaration (using the TypeRef constructor)
        auto func_node = std::make_unique<FunctionDeclarationNode>(
            loc,
            decl.name,
            return_type,
            false // is_public
        );
        func_node->set_variadic(decl.is_variadic);

        // Add parameters
        for (size_t i = 0; i < decl.params.size(); i++)
        {
            const auto &param = decl.params[i];
            TypeRef param_type = map_c_type(param.c_type, arena);

            std::string param_name = param.name;
            if (param_name.empty())
            {
                param_name = "arg" + std::to_string(i);
            }

            auto param_node = std::make_unique<VariableDeclarationNode>(
                loc,
                param_name,
                param_type,
                nullptr, // no initializer
                false,   // not mutable
                false    // not global
            );

            func_node->add_parameter(std::move(param_node));
        }

        return func_node;
    }

} // namespace Cryo
