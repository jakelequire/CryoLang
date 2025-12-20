use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

use crate::error::{FormatError, Result};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    pub indent: IndentConfig,
    pub spacing: SpacingConfig,
    pub format: FormatConfig,
    pub comment: CommentConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IndentConfig {
    pub use_tabs: bool,
    pub tab_width: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SpacingConfig {
    /// Add space around binary operators
    pub binary_operators: bool,
    /// Add space after commas
    pub after_comma: bool,
    /// Add space after colons in type annotations
    pub after_colon: bool,
    /// Add space around assignment operators
    pub assignment_operators: bool,
    /// Add space inside parentheses
    pub inside_parentheses: bool,
    /// Add space inside brackets
    pub inside_brackets: bool,
    /// Add space inside braces
    pub inside_braces: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FormatConfig {
    /// Maximum line width before wrapping
    pub max_width: usize,
    /// Force single line for short function signatures
    pub short_function_single_line: bool,
    /// Force single line for short struct/enum definitions
    pub short_struct_single_line: bool,
    /// Align consecutive declarations
    pub align_consecutive_declarations: bool,
    /// Sort imports alphabetically
    pub sort_imports: bool,
    /// Insert final newline
    pub insert_final_newline: bool,
    /// Trim trailing whitespace
    pub trim_trailing_whitespace: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CommentConfig {
    /// Format documentation comments
    pub format_doc_comments: bool,
    /// Wrap comments at max width
    pub wrap_comments: bool,
    /// Normalize comment spacing
    pub normalize_comment_spacing: bool,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            indent: IndentConfig {
                use_tabs: false,
                tab_width: 4,
            },
            spacing: SpacingConfig {
                binary_operators: true,
                after_comma: true,
                after_colon: true,
                assignment_operators: true,
                inside_parentheses: false,
                inside_brackets: false,
                inside_braces: true,
            },
            format: FormatConfig {
                max_width: 100,
                short_function_single_line: true,
                short_struct_single_line: true,
                align_consecutive_declarations: false,
                sort_imports: true,
                insert_final_newline: true,
                trim_trailing_whitespace: true,
            },
            comment: CommentConfig {
                format_doc_comments: true,
                wrap_comments: true,
                normalize_comment_spacing: true,
            },
        }
    }
}

impl Config {
    pub fn from_file<P: AsRef<Path>>(path: P) -> Result<Self> {
        let content = fs::read_to_string(&path)
            .map_err(|e| FormatError::Config(format!("Failed to read config file: {}", e)))?;
        
        let config: Config = toml::from_str(&content)
            .map_err(|e| FormatError::Config(format!("Invalid config format: {}", e)))?;
        
        Ok(config)
    }

    pub fn find_and_load() -> Result<Self> {
        // Look for configuration files in order of preference
        let config_files = [
            "cryofmt.toml",
            ".cryofmt.toml",
            "cryolang.toml",
            ".cryolang.toml",
        ];

        for config_file in &config_files {
            if Path::new(config_file).exists() {
                return Self::from_file(config_file);
            }
        }

        // Look in parent directories
        let mut current_dir = std::env::current_dir()
            .map_err(|e| FormatError::Config(format!("Failed to get current directory: {}", e)))?;

        loop {
            for config_file in &config_files {
                let config_path = current_dir.join(config_file);
                if config_path.exists() {
                    return Self::from_file(config_path);
                }
            }

            if !current_dir.pop() {
                break;
            }
        }

        // Return default config if no file found
        Ok(Self::default())
    }

    pub fn save_to_file<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        let content = toml::to_string_pretty(self)
            .map_err(|e| FormatError::Config(format!("Failed to serialize config: {}", e)))?;
        
        fs::write(path, content)
            .map_err(|e| FormatError::Config(format!("Failed to write config file: {}", e)))?;
        
        Ok(())
    }
}

#[derive(Debug, Clone)]
pub struct FormatOptions {
    pub indent: IndentConfig,
    pub spacing: SpacingConfig,
    pub format: FormatConfig,
    pub comment: CommentConfig,
}

impl FormatOptions {
    pub fn from_config(config: &Config) -> Self {
        FormatOptions {
            indent: config.indent.clone(),
            spacing: config.spacing.clone(),
            format: config.format.clone(),
            comment: config.comment.clone(),
        }
    }

    pub fn get_indent_string(&self) -> String {
        if self.indent.use_tabs {
            "\t".to_string()
        } else {
            " ".repeat(self.indent.tab_width)
        }
    }
}

impl Default for FormatOptions {
    fn default() -> Self {
        Self::from_config(&Config::default())
    }
}