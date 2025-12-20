use crate::error::{FormatError, Result};
use crate::token::{Token, TokenType};
use std::str::Chars;
use std::iter::Peekable;

pub struct Lexer<'a> {
    input: &'a str,
    chars: Peekable<Chars<'a>>,
    position: usize,
    line: usize,
    column: usize,
    current_char: Option<char>,
}

impl<'a> Lexer<'a> {
    pub fn new(input: &'a str) -> Self {
        let mut chars = input.chars().peekable();
        let current_char = chars.next();
        
        Lexer {
            input,
            chars,
            position: 0,
            line: 1,
            column: 1,
            current_char,
        }
    }

    pub fn tokenize(&mut self) -> Result<Vec<Token>> {
        let mut tokens = Vec::new();

        while self.current_char.is_some() {
            let token = self.next_token()?;
            tokens.push(token);
        }

        tokens.push(Token::new(
            TokenType::Eof,
            String::new(),
            self.line,
            self.column,
            self.position,
        ));

        Ok(tokens)
    }

    fn next_token(&mut self) -> Result<Token> {
        match self.current_char {
            Some(ch) if ch.is_whitespace() => self.read_whitespace(),
            Some('/') => self.read_slash_or_comment(),
            Some('"') => self.read_string_literal(),
            Some('\'') => self.read_char_literal(),
            Some(ch) if ch.is_alphabetic() || ch == '_' => self.read_identifier_or_keyword(),
            Some(ch) if ch.is_ascii_digit() => self.read_numeric_literal(),
            Some('+') => self.read_plus(),
            Some('-') => self.read_minus(),
            Some('*') => self.read_star(),
            Some('%') => self.read_percent(),
            Some('=') => self.read_equal(),
            Some('!') => self.read_exclamation(),
            Some('<') => self.read_less(),
            Some('>') => self.read_greater(),
            Some('&') => self.read_ampersand(),
            Some('|') => self.read_pipe(),
            Some('^') => self.read_caret(),
            Some('~') => self.read_tilde(),
            Some('?') => self.read_question(),
            Some('(') => self.read_single_char_token(TokenType::LeftParen),
            Some(')') => self.read_single_char_token(TokenType::RightParen),
            Some('{') => self.read_single_char_token(TokenType::LeftBrace),
            Some('}') => self.read_single_char_token(TokenType::RightBrace),
            Some('[') => self.read_single_char_token(TokenType::LeftBracket),
            Some(']') => self.read_single_char_token(TokenType::RightBracket),
            Some(';') => self.read_single_char_token(TokenType::Semicolon),
            Some(',') => self.read_single_char_token(TokenType::Comma),
            Some(':') => self.read_colon(),
            Some('.') => self.read_dot(),
            Some('@') => self.read_single_char_token(TokenType::At),
            Some('#') => self.read_single_char_token(TokenType::Hash),
            Some(ch) => {
                let token = Token::new(
                    TokenType::Unknown,
                    ch.to_string(),
                    self.line,
                    self.column,
                    self.position,
                );
                self.advance();
                Ok(token)
            }
            None => Err(FormatError::lexer_error(
                self.line,
                self.column,
                "Unexpected end of input",
            )),
        }
    }

    fn read_whitespace(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;
        let mut text = String::new();
        let mut is_newline = false;

        while let Some(ch) = self.current_char {
            if ch.is_whitespace() {
                text.push(ch);
                if ch == '\n' {
                    is_newline = true;
                }
                self.advance();
            } else {
                break;
            }
        }

        let token_type = if is_newline {
            TokenType::Newline
        } else {
            TokenType::Whitespace
        };

        Ok(Token::new(token_type, text, start_line, start_column, start_position))
    }

    fn read_slash_or_comment(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance(); // consume '/'

        match self.current_char {
            Some('/') => {
                // Line comment
                self.advance(); // consume second '/'
                let mut text = String::from("//");
                
                while let Some(ch) = self.current_char {
                    if ch == '\n' {
                        break;
                    }
                    text.push(ch);
                    self.advance();
                }

                Ok(Token::new(TokenType::LineComment, text, start_line, start_column, start_position))
            }
            Some('*') => {
                // Block comment
                self.advance(); // consume '*'
                let mut text = String::from("/*");
                
                while let Some(ch) = self.current_char {
                    text.push(ch);
                    if ch == '*' {
                        self.advance();
                        if let Some('/') = self.current_char {
                            text.push('/');
                            self.advance();
                            break;
                        }
                    } else {
                        self.advance();
                    }
                }

                Ok(Token::new(TokenType::BlockComment, text, start_line, start_column, start_position))
            }
            Some('=') => {
                self.advance();
                Ok(Token::new(
                    TokenType::SlashEqual,
                    String::from("/="),
                    start_line,
                    start_column,
                    start_position,
                ))
            }
            _ => Ok(Token::new(
                TokenType::Slash,
                String::from("/"),
                start_line,
                start_column,
                start_position,
            )),
        }
    }

    fn read_string_literal(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;
        let mut text = String::new();

        text.push('"');
        self.advance(); // consume opening quote

        while let Some(ch) = self.current_char {
            if ch == '"' {
                text.push(ch);
                self.advance();
                break;
            } else if ch == '\\' {
                text.push(ch);
                self.advance();
                if let Some(escaped) = self.current_char {
                    text.push(escaped);
                    self.advance();
                }
            } else {
                text.push(ch);
                self.advance();
            }
        }

        Ok(Token::new(TokenType::StringLiteral, text, start_line, start_column, start_position))
    }

    fn read_char_literal(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;
        let mut text = String::new();

        text.push('\'');
        self.advance(); // consume opening quote

        while let Some(ch) = self.current_char {
            if ch == '\'' {
                text.push(ch);
                self.advance();
                break;
            } else if ch == '\\' {
                text.push(ch);
                self.advance();
                if let Some(escaped) = self.current_char {
                    text.push(escaped);
                    self.advance();
                }
            } else {
                text.push(ch);
                self.advance();
            }
        }

        Ok(Token::new(TokenType::CharLiteral, text, start_line, start_column, start_position))
    }

    fn read_identifier_or_keyword(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;
        let mut text = String::new();

        while let Some(ch) = self.current_char {
            if ch.is_alphanumeric() || ch == '_' {
                text.push(ch);
                self.advance();
            } else {
                break;
            }
        }

        let token_type = match text.as_str() {
            "const" => TokenType::Const,
            "mut" => TokenType::Mut,
            "function" => TokenType::Function,
            "type" => TokenType::Type,
            "struct" => TokenType::Struct,
            "class" => TokenType::Class,
            "enum" => TokenType::Enum,
            "trait" => TokenType::Trait,
            "implement" => TokenType::Implement,
            "namespace" => TokenType::Namespace,
            "import" => TokenType::Import,
            "export" => TokenType::Export,
            "if" => TokenType::If,
            "else" => TokenType::Else,
            "while" => TokenType::While,
            "for" => TokenType::For,
            "match" => TokenType::Match,
            "case" => TokenType::Case,
            "default" => TokenType::Default,
            "switch" => TokenType::Switch,
            "break" => TokenType::Break,
            "continue" => TokenType::Continue,
            "return" => TokenType::Return,
            "true" => TokenType::True,
            "false" => TokenType::False,
            "null" => TokenType::Null,
            "this" => TokenType::This,
            "super" => TokenType::Super,
            "new" => TokenType::New,
            "delete" => TokenType::Delete,
            "sizeof" => TokenType::Sizeof,
            "typeof" => TokenType::Typeof,
            "void" => TokenType::Void,
            "boolean" => TokenType::Boolean,
            "int" => TokenType::Int,
            "i8" => TokenType::I8,
            "i16" => TokenType::I16,
            "i32" => TokenType::I32,
            "i64" => TokenType::I64,
            "uint" => TokenType::Uint,
            "u8" => TokenType::U8,
            "u16" => TokenType::U16,
            "u32" => TokenType::U32,
            "u64" => TokenType::U64,
            "float" => TokenType::Float,
            "f32" => TokenType::F32,
            "f64" => TokenType::F64,
            "double" => TokenType::Double,
            "char" => TokenType::Char,
            "string" => TokenType::String,
            _ => TokenType::Identifier,
        };

        Ok(Token::new(token_type, text, start_line, start_column, start_position))
    }

    fn read_numeric_literal(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;
        let mut text = String::new();

        while let Some(ch) = self.current_char {
            if ch.is_ascii_digit() || ch == '.' || ch == 'e' || ch == 'E' || ch == 'x' || ch == 'X' || ch == 'b' || ch == 'B' {
                text.push(ch);
                self.advance();
            } else if (ch == '+' || ch == '-') && text.ends_with(['e', 'E']) {
                text.push(ch);
                self.advance();
            } else {
                break;
            }
        }

        Ok(Token::new(TokenType::NumericLiteral, text, start_line, start_column, start_position))
    }

    fn read_plus(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('+') => {
                self.advance();
                Ok(Token::new(TokenType::PlusPlus, String::from("++"), start_line, start_column, start_position))
            }
            Some('=') => {
                self.advance();
                Ok(Token::new(TokenType::PlusEqual, String::from("+="), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Plus, String::from("+"), start_line, start_column, start_position))
        }
    }

    fn read_minus(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('-') => {
                self.advance();
                Ok(Token::new(TokenType::MinusMinus, String::from("--"), start_line, start_column, start_position))
            }
            Some('=') => {
                self.advance();
                Ok(Token::new(TokenType::MinusEqual, String::from("-="), start_line, start_column, start_position))
            }
            Some('>') => {
                self.advance();
                Ok(Token::new(TokenType::Arrow, String::from("->"), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Minus, String::from("-"), start_line, start_column, start_position))
        }
    }

    fn read_star(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('=') => {
                self.advance();
                Ok(Token::new(TokenType::StarEqual, String::from("*="), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Star, String::from("*"), start_line, start_column, start_position))
        }
    }

    fn read_percent(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('=') => {
                self.advance();
                Ok(Token::new(TokenType::PercentEqual, String::from("%="), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Percent, String::from("%"), start_line, start_column, start_position))
        }
    }

    fn read_equal(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('=') => {
                self.advance();
                Ok(Token::new(TokenType::EqualEqual, String::from("=="), start_line, start_column, start_position))
            }
            Some('>') => {
                self.advance();
                Ok(Token::new(TokenType::FatArrow, String::from("=>"), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Equal, String::from("="), start_line, start_column, start_position))
        }
    }

    fn read_exclamation(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('=') => {
                self.advance();
                Ok(Token::new(TokenType::NotEqual, String::from("!="), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Not, String::from("!"), start_line, start_column, start_position))
        }
    }

    fn read_less(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('=') => {
                self.advance();
                Ok(Token::new(TokenType::LessEqual, String::from("<="), start_line, start_column, start_position))
            }
            Some('<') => {
                self.advance();
                Ok(Token::new(TokenType::LessLess, String::from("<<"), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Less, String::from("<"), start_line, start_column, start_position))
        }
    }

    fn read_greater(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('=') => {
                self.advance();
                Ok(Token::new(TokenType::GreaterEqual, String::from(">="), start_line, start_column, start_position))
            }
            Some('>') => {
                self.advance();
                Ok(Token::new(TokenType::GreaterGreater, String::from(">>"), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Greater, String::from(">"), start_line, start_column, start_position))
        }
    }

    fn read_ampersand(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('&') => {
                self.advance();
                Ok(Token::new(TokenType::AndAnd, String::from("&&"), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::And, String::from("&"), start_line, start_column, start_position))
        }
    }

    fn read_pipe(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('|') => {
                self.advance();
                Ok(Token::new(TokenType::OrOr, String::from("||"), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Or, String::from("|"), start_line, start_column, start_position))
        }
    }

    fn read_caret(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();
        Ok(Token::new(TokenType::Caret, String::from("^"), start_line, start_column, start_position))
    }

    fn read_tilde(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();
        Ok(Token::new(TokenType::Tilde, String::from("~"), start_line, start_column, start_position))
    }

    fn read_question(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('?') => {
                self.advance();
                Ok(Token::new(TokenType::QuestionQuestion, String::from("??"), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Question, String::from("?"), start_line, start_column, start_position))
        }
    }

    fn read_colon(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some(':') => {
                self.advance();
                Ok(Token::new(TokenType::ColonColon, String::from("::"), start_line, start_column, start_position))
            }
            _ => Ok(Token::new(TokenType::Colon, String::from(":"), start_line, start_column, start_position))
        }
    }

    fn read_dot(&mut self) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;

        self.advance();

        match self.current_char {
            Some('.') => {
                self.advance();
                match self.current_char {
                    Some('.') => {
                        self.advance();
                        Ok(Token::new(TokenType::DotDotDot, String::from("..."), start_line, start_column, start_position))
                    }
                    _ => Ok(Token::new(TokenType::DotDot, String::from(".."), start_line, start_column, start_position))
                }
            }
            _ => Ok(Token::new(TokenType::Dot, String::from("."), start_line, start_column, start_position))
        }
    }

    fn read_single_char_token(&mut self, token_type: TokenType) -> Result<Token> {
        let start_position = self.position;
        let start_line = self.line;
        let start_column = self.column;
        let ch = self.current_char.unwrap();
        
        self.advance();
        Ok(Token::new(token_type, ch.to_string(), start_line, start_column, start_position))
    }

    fn advance(&mut self) {
        if let Some(ch) = self.current_char {
            self.position += ch.len_utf8();
            if ch == '\n' {
                self.line += 1;
                self.column = 1;
            } else {
                self.column += 1;
            }
        }
        self.current_char = self.chars.next();
    }
}