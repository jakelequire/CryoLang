#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TokenType {
    // Literals
    Identifier,
    StringLiteral,
    NumericLiteral,
    CharLiteral,
    BooleanLiteral,

    // Keywords
    Const,
    Mut,
    Function,
    Type,
    Struct,
    Class,
    Enum,
    Trait,
    Implement,
    Namespace,
    Import,
    Export,
    If,
    Else,
    While,
    For,
    Match,
    Case,
    Default,
    Switch,
    Break,
    Continue,
    Return,
    True,
    False,
    Null,
    This,
    Super,
    New,
    Delete,
    Sizeof,
    Typeof,

    // Types
    Void,
    Boolean,
    Int,
    I8,
    I16,
    I32,
    I64,
    Uint,
    U8,
    U16,
    U32,
    U64,
    Float,
    F32,
    F64,
    Double,
    Char,
    String,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Equal,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    EqualEqual,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    AndAnd,
    Or,
    OrOr,
    Not,
    Caret,
    Tilde,
    LessLess,
    GreaterGreater,
    PlusPlus,
    MinusMinus,
    Question,
    QuestionQuestion,

    // Punctuation
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Semicolon,
    Comma,
    Colon,
    ColonColon,
    Dot,
    DotDot,
    DotDotDot,
    Arrow,
    FatArrow,
    At,
    Hash,

    // Comments
    LineComment,
    BlockComment,
    DocComment,

    // Whitespace and Special
    Newline,
    Whitespace,
    Eof,
    Unknown,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub token_type: TokenType,
    pub text: String,
    pub line: usize,
    pub column: usize,
    pub position: usize,
}

impl Token {
    pub fn new(token_type: TokenType, text: String, line: usize, column: usize, position: usize) -> Self {
        Token {
            token_type,
            text,
            line,
            column,
            position,
        }
    }

    pub fn is_keyword(&self) -> bool {
        matches!(
            self.token_type,
            TokenType::Const
                | TokenType::Mut
                | TokenType::Function
                | TokenType::Type
                | TokenType::Struct
                | TokenType::Class
                | TokenType::Enum
                | TokenType::Trait
                | TokenType::Implement
                | TokenType::Namespace
                | TokenType::Import
                | TokenType::Export
                | TokenType::If
                | TokenType::Else
                | TokenType::While
                | TokenType::For
                | TokenType::Match
                | TokenType::Case
                | TokenType::Default
                | TokenType::Switch
                | TokenType::Break
                | TokenType::Continue
                | TokenType::Return
                | TokenType::True
                | TokenType::False
                | TokenType::Null
                | TokenType::This
                | TokenType::Super
                | TokenType::New
                | TokenType::Delete
                | TokenType::Sizeof
                | TokenType::Typeof
        )
    }

    pub fn is_type_keyword(&self) -> bool {
        matches!(
            self.token_type,
            TokenType::Void
                | TokenType::Boolean
                | TokenType::Int
                | TokenType::I8
                | TokenType::I16
                | TokenType::I32
                | TokenType::I64
                | TokenType::Uint
                | TokenType::U8
                | TokenType::U16
                | TokenType::U32
                | TokenType::U64
                | TokenType::Float
                | TokenType::F32
                | TokenType::F64
                | TokenType::Double
                | TokenType::Char
                | TokenType::String
        )
    }

    pub fn is_operator(&self) -> bool {
        matches!(
            self.token_type,
            TokenType::Plus
                | TokenType::Minus
                | TokenType::Star
                | TokenType::Slash
                | TokenType::Percent
                | TokenType::Equal
                | TokenType::PlusEqual
                | TokenType::MinusEqual
                | TokenType::StarEqual
                | TokenType::SlashEqual
                | TokenType::PercentEqual
                | TokenType::EqualEqual
                | TokenType::NotEqual
                | TokenType::Less
                | TokenType::LessEqual
                | TokenType::Greater
                | TokenType::GreaterEqual
                | TokenType::And
                | TokenType::AndAnd
                | TokenType::Or
                | TokenType::OrOr
                | TokenType::Not
                | TokenType::Caret
                | TokenType::Tilde
                | TokenType::LessLess
                | TokenType::GreaterGreater
                | TokenType::PlusPlus
                | TokenType::MinusMinus
                | TokenType::Question
                | TokenType::QuestionQuestion
        )
    }

    pub fn is_binary_operator(&self) -> bool {
        matches!(
            self.token_type,
            TokenType::Plus
                | TokenType::Minus
                | TokenType::Star
                | TokenType::Slash
                | TokenType::Percent
                | TokenType::EqualEqual
                | TokenType::NotEqual
                | TokenType::Less
                | TokenType::LessEqual
                | TokenType::Greater
                | TokenType::GreaterEqual
                | TokenType::AndAnd
                | TokenType::OrOr
                | TokenType::And
                | TokenType::Or
                | TokenType::Caret
                | TokenType::LessLess
                | TokenType::GreaterGreater
        )
    }

    pub fn is_assignment_operator(&self) -> bool {
        matches!(
            self.token_type,
            TokenType::Equal
                | TokenType::PlusEqual
                | TokenType::MinusEqual
                | TokenType::StarEqual
                | TokenType::SlashEqual
                | TokenType::PercentEqual
        )
    }

    pub fn is_punctuation(&self) -> bool {
        matches!(
            self.token_type,
            TokenType::LeftParen
                | TokenType::RightParen
                | TokenType::LeftBrace
                | TokenType::RightBrace
                | TokenType::LeftBracket
                | TokenType::RightBracket
                | TokenType::Semicolon
                | TokenType::Comma
                | TokenType::Colon
                | TokenType::ColonColon
                | TokenType::Dot
                | TokenType::DotDot
                | TokenType::DotDotDot
                | TokenType::Arrow
                | TokenType::FatArrow
                | TokenType::At
                | TokenType::Hash
        )
    }

    pub fn is_comment(&self) -> bool {
        matches!(
            self.token_type,
            TokenType::LineComment | TokenType::BlockComment | TokenType::DocComment
        )
    }

    pub fn is_whitespace(&self) -> bool {
        matches!(self.token_type, TokenType::Whitespace | TokenType::Newline)
    }
}