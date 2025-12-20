use thiserror::Error;

pub type Result<T> = std::result::Result<T, FormatError>;

#[derive(Error, Debug)]
pub enum FormatError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Parse error at line {line}, column {column}: {message}")]
    Parse {
        line: usize,
        column: usize,
        message: String,
    },

    #[error("Lexer error at line {line}, column {column}: {message}")]
    Lexer {
        line: usize,
        column: usize,
        message: String,
    },

    #[error("Configuration error: {0}")]
    Config(String),

    #[error("Invalid file format: {0}")]
    InvalidFormat(String),

    #[error("Unsupported syntax: {0}")]
    UnsupportedSyntax(String),
}

impl FormatError {
    pub fn parse_error(line: usize, column: usize, message: impl Into<String>) -> Self {
        FormatError::Parse {
            line,
            column,
            message: message.into(),
        }
    }

    pub fn lexer_error(line: usize, column: usize, message: impl Into<String>) -> Self {
        FormatError::Lexer {
            line,
            column,
            message: message.into(),
        }
    }
}