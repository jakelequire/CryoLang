pub mod config;
pub mod error;
pub mod formatter;
pub mod lexer;
pub mod parser;
pub mod profiler;
pub mod token;

pub use config::{Config, FormatOptions};
pub use error::{FormatError, Result};
pub use formatter::{format_file, format_string};
pub use profiler::{Profiler, ProfilerStats};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_formatting() {
        let input = "function main()->int{return 0;}";
        let expected = "function main() -> int {\n    return 0;\n}";
        
        let options = FormatOptions::default();
        let result = format_string(input, &options).unwrap();
        assert_eq!(result.trim(), expected);
    }

    #[test]
    fn test_variable_declaration_formatting() {
        let input = "const   x:int=42;";
        let expected = "const x: int = 42;";
        
        let options = FormatOptions::default();
        let result = format_string(input, &options).unwrap();
        assert_eq!(result.trim(), expected);
    }

    #[test]
    fn test_struct_formatting() {
        let input = "type struct Point{x:int,y:int}";
        let expected = "type struct Point {\n    x: int,\n    y: int\n}";
        
        let options = FormatOptions::default();
        let result = format_string(input, &options).unwrap();
        assert_eq!(result.trim(), expected);
    }
}