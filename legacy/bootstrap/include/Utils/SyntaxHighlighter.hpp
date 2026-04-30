#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <regex>

namespace Cryo
{
    // Forward declarations
    enum class TokenType;
    
    /**
     * @brief Represents a syntax token with highlighting information
     */
    struct SyntaxToken 
    {
        size_t start_pos;
        size_t end_pos;
        TokenType type;
        std::string color_code;
        
        SyntaxToken(size_t start, size_t end, TokenType token_type, const std::string& color)
            : start_pos(start), end_pos(end), type(token_type), color_code(color) {}
    };

    /**
     * @brief Token types for syntax highlighting
     */
    enum class TokenType 
    {
        Unknown,
        Keyword,
        Type,
        String,
        Number,
        Comment,
        Operator,
        Identifier,
        Punctuation,
        Literal,
        Function,
        Variable,
        Constant,
        Preprocessor
    };

    /**
     * @brief Language-specific syntax rules
     */
    struct LanguageRules 
    {
        std::vector<std::string> keywords;
        std::vector<std::string> types;
        std::vector<std::string> operators;
        std::vector<std::string> literals;
        std::string string_delimiters;
        std::string comment_single_line;
        std::string comment_multi_line_start;
        std::string comment_multi_line_end;
        std::string number_pattern;
        std::unordered_map<TokenType, std::string> color_scheme;
        
        LanguageRules() = default;
    };

    /**
     * @brief Abstract base class for language-specific syntax highlighters
     */
    class LanguageHighlighter 
    {
    public:
        virtual ~LanguageHighlighter() = default;
        virtual std::vector<SyntaxToken> tokenize(const std::string& code) = 0;
        virtual std::string get_language_name() const = 0;
        
    protected:
        virtual LanguageRules get_language_rules() const = 0;
        std::vector<SyntaxToken> basic_tokenize(const std::string& code, const LanguageRules& rules);
    };

    /**
     * @brief Cryo language syntax highlighter
     */
    class CryoHighlighter : public LanguageHighlighter 
    {
    public:
        CryoHighlighter();
        std::vector<SyntaxToken> tokenize(const std::string& code) override;
        std::string get_language_name() const override { return "cryo"; }
        
    protected:
        LanguageRules get_language_rules() const override;
        
    private:
        LanguageRules _rules;
        void initialize_cryo_rules();
        bool is_keyword(const std::string& word) const;
        bool is_type(const std::string& word) const;
        bool is_operator(const std::string& word) const;
    };

    /**
     * @brief Main syntax highlighter that supports multiple languages
     */
    class SyntaxHighlighter 
    {
    public:
        SyntaxHighlighter();
        ~SyntaxHighlighter() = default;

        // Core highlighting functionality
        std::string highlight_code(const std::string& code, const std::string& language = "cryo") const;
        std::string highlight_line(const std::string& line, const std::string& language = "cryo") const;
        
        // Integration with diagnostic formatting
        std::string apply_syntax_highlighting(const std::string& source_line) const;
        
        // Language management
        void register_language(const std::string& name, std::unique_ptr<LanguageHighlighter> highlighter);
        bool supports_language(const std::string& language) const;
        std::vector<std::string> get_supported_languages() const;
        
        // Configuration
        void set_color_output(bool enabled) { _use_colors = enabled; }
        void set_default_language(const std::string& language) { _default_language = language; }
        bool is_color_output_enabled() const { return _use_colors; }
        
    private:
        std::unordered_map<std::string, std::unique_ptr<LanguageHighlighter>> _highlighters;
        std::string _default_language;
        bool _use_colors;
        
        // Helper methods
        std::string apply_tokens_to_string(const std::string& code, const std::vector<SyntaxToken>& tokens) const;
        std::string colorize_token(const std::string& text, const std::string& color_code) const;
        LanguageHighlighter* get_highlighter(const std::string& language) const;
        
        // Color utilities
        std::string get_reset_code() const;
        bool should_colorize() const;
    };

    // Utility functions for color codes
    namespace ColorCodes 
    {
        constexpr const char* RESET = "\033[0m";
        constexpr const char* BOLD = "\033[1m";
        constexpr const char* DIM = "\033[2m";
        
        // Foreground colors
        constexpr const char* RED = "\033[31m";
        constexpr const char* GREEN = "\033[32m";
        constexpr const char* YELLOW = "\033[33m";
        constexpr const char* BLUE = "\033[34m";
        constexpr const char* MAGENTA = "\033[35m";
        constexpr const char* CYAN = "\033[36m";
        constexpr const char* WHITE = "\033[37m";
        constexpr const char* GREY = "\033[90m";
        
        // Bright colors
        constexpr const char* BRIGHT_RED = "\033[91m";
        constexpr const char* BRIGHT_GREEN = "\033[92m";
        constexpr const char* BRIGHT_YELLOW = "\033[93m";
        constexpr const char* BRIGHT_BLUE = "\033[94m";
        constexpr const char* BRIGHT_MAGENTA = "\033[95m";
        constexpr const char* BRIGHT_CYAN = "\033[96m";
        constexpr const char* BRIGHT_WHITE = "\033[97m";
    }
}