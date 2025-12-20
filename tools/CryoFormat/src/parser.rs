use crate::error::{FormatError, Result};
use crate::token::{Token, TokenType};

#[derive(Debug, Clone)]
pub enum AstNode {
    Program {
        items: Vec<AstNode>,
    },
    VariableDeclaration {
        is_mutable: bool,
        name: String,
        type_annotation: Option<String>,
        initializer: Option<Box<AstNode>>,
    },
    FunctionDeclaration {
        name: String,
        parameters: Vec<(String, String)>, // (name, type)
        return_type: Option<String>,
        body: Box<AstNode>,
    },
    StructDeclaration {
        name: String,
        fields: Vec<(String, String)>, // (name, type)
    },
    Block {
        statements: Vec<AstNode>,
    },
    Expression(Box<AstNode>),
    BinaryExpression {
        left: Box<AstNode>,
        operator: String,
        right: Box<AstNode>,
    },
    UnaryExpression {
        operator: String,
        operand: Box<AstNode>,
    },
    CallExpression {
        callee: Box<AstNode>,
        arguments: Vec<AstNode>,
    },
    Identifier(String),
    Literal(String),
    ReturnStatement {
        value: Option<Box<AstNode>>,
    },
    IfStatement {
        condition: Box<AstNode>,
        then_branch: Box<AstNode>,
        else_branch: Option<Box<AstNode>>,
    },
    WhileLoop {
        condition: Box<AstNode>,
        body: Box<AstNode>,
    },
    ForLoop {
        initializer: Option<Box<AstNode>>,
        condition: Option<Box<AstNode>>,
        increment: Option<Box<AstNode>>,
        body: Box<AstNode>,
    },
}

pub struct Parser {
    tokens: Vec<Token>,
    current: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Parser { tokens, current: 0 }
    }

    pub fn parse(&mut self) -> Result<AstNode> {
        let mut items = Vec::new();

        while !self.is_at_end() {
            // Skip whitespace and comments
            if self.check(&TokenType::Whitespace) || self.check(&TokenType::Newline) || self.peek().is_comment() {
                self.advance();
                continue;
            }

            items.push(self.parse_item()?);
        }

        Ok(AstNode::Program { items })
    }

    fn parse_item(&mut self) -> Result<AstNode> {
        if self.match_token(&TokenType::Const) || self.match_token(&TokenType::Mut) {
            self.parse_variable_declaration()
        } else if self.match_token(&TokenType::Function) {
            self.parse_function_declaration()
        } else if self.match_token(&TokenType::Type) {
            if self.match_token(&TokenType::Struct) {
                self.parse_struct_declaration()
            } else {
                Err(FormatError::parse_error(
                    self.peek().line,
                    self.peek().column,
                    "Expected 'struct' after 'type'",
                ))
            }
        } else {
            self.parse_statement()
        }
    }

    fn parse_variable_declaration(&mut self) -> Result<AstNode> {
        let is_mutable = self.previous().token_type == TokenType::Mut;

        let name = if self.check(&TokenType::Identifier) {
            self.advance().text.clone()
        } else {
            return Err(FormatError::parse_error(
                self.peek().line,
                self.peek().column,
                "Expected identifier in variable declaration",
            ));
        };

        let type_annotation = if self.match_token(&TokenType::Colon) {
            if self.check(&TokenType::Identifier) || self.peek().is_type_keyword() {
                Some(self.advance().text.clone())
            } else {
                return Err(FormatError::parse_error(
                    self.peek().line,
                    self.peek().column,
                    "Expected type annotation after ':'",
                ));
            }
        } else {
            None
        };

        let initializer = if self.match_token(&TokenType::Equal) {
            Some(Box::new(self.parse_expression()?))
        } else {
            None
        };

        self.consume(&TokenType::Semicolon, "Expected ';' after variable declaration")?;

        Ok(AstNode::VariableDeclaration {
            is_mutable,
            name,
            type_annotation,
            initializer,
        })
    }

    fn parse_function_declaration(&mut self) -> Result<AstNode> {
        let name = if self.check(&TokenType::Identifier) {
            self.advance().text.clone()
        } else {
            return Err(FormatError::parse_error(
                self.peek().line,
                self.peek().column,
                "Expected function name",
            ));
        };

        self.consume(&TokenType::LeftParen, "Expected '(' after function name")?;

        let mut parameters = Vec::new();
        if !self.check(&TokenType::RightParen) {
            loop {
                let param_name = if self.check(&TokenType::Identifier) {
                    self.advance().text.clone()
                } else {
                    return Err(FormatError::parse_error(
                        self.peek().line,
                        self.peek().column,
                        "Expected parameter name",
                    ));
                };

                self.consume(&TokenType::Colon, "Expected ':' after parameter name")?;

                let param_type = if self.check(&TokenType::Identifier) || self.peek().is_type_keyword() {
                    self.advance().text.clone()
                } else {
                    return Err(FormatError::parse_error(
                        self.peek().line,
                        self.peek().column,
                        "Expected parameter type",
                    ));
                };

                parameters.push((param_name, param_type));

                if !self.match_token(&TokenType::Comma) {
                    break;
                }
            }
        }

        self.consume(&TokenType::RightParen, "Expected ')' after parameters")?;

        let return_type = if self.match_token(&TokenType::Arrow) {
            if self.check(&TokenType::Identifier) || self.peek().is_type_keyword() {
                Some(self.advance().text.clone())
            } else {
                return Err(FormatError::parse_error(
                    self.peek().line,
                    self.peek().column,
                    "Expected return type after '->'",
                ));
            }
        } else {
            None
        };

        let body = Box::new(self.parse_block()?);

        Ok(AstNode::FunctionDeclaration {
            name,
            parameters,
            return_type,
            body,
        })
    }

    fn parse_struct_declaration(&mut self) -> Result<AstNode> {
        let name = if self.check(&TokenType::Identifier) {
            self.advance().text.clone()
        } else {
            return Err(FormatError::parse_error(
                self.peek().line,
                self.peek().column,
                "Expected struct name",
            ));
        };

        self.consume(&TokenType::LeftBrace, "Expected '{' after struct name")?;

        let mut fields = Vec::new();
        while !self.check(&TokenType::RightBrace) && !self.is_at_end() {
            // Skip whitespace and comments
            if self.check(&TokenType::Whitespace) || self.check(&TokenType::Newline) || self.peek().is_comment() {
                self.advance();
                continue;
            }

            let field_name = if self.check(&TokenType::Identifier) {
                self.advance().text.clone()
            } else {
                return Err(FormatError::parse_error(
                    self.peek().line,
                    self.peek().column,
                    "Expected field name",
                ));
            };

            self.consume(&TokenType::Colon, "Expected ':' after field name")?;

            let field_type = if self.check(&TokenType::Identifier) || self.peek().is_type_keyword() {
                self.advance().text.clone()
            } else {
                return Err(FormatError::parse_error(
                    self.peek().line,
                    self.peek().column,
                    "Expected field type",
                ));
            };

            fields.push((field_name, field_type));

            if self.match_token(&TokenType::Comma) {
                // Optional comma
            }
        }

        self.consume(&TokenType::RightBrace, "Expected '}' after struct fields")?;

        Ok(AstNode::StructDeclaration { name, fields })
    }

    fn parse_statement(&mut self) -> Result<AstNode> {
        if self.match_token(&TokenType::Return) {
            self.parse_return_statement()
        } else if self.match_token(&TokenType::If) {
            self.parse_if_statement()
        } else if self.match_token(&TokenType::While) {
            self.parse_while_statement()
        } else if self.match_token(&TokenType::For) {
            self.parse_for_statement()
        } else if self.check(&TokenType::LeftBrace) {
            self.parse_block()
        } else {
            let expr = self.parse_expression()?;
            if !self.check(&TokenType::Semicolon) {
                // Allow semicolon to be optional in some contexts
            } else {
                self.consume(&TokenType::Semicolon, "Expected ';' after expression")?;
            }
            Ok(AstNode::Expression(Box::new(expr)))
        }
    }

    fn parse_return_statement(&mut self) -> Result<AstNode> {
        let value = if self.check(&TokenType::Semicolon) {
            None
        } else {
            Some(Box::new(self.parse_expression()?))
        };

        self.consume(&TokenType::Semicolon, "Expected ';' after return statement")?;

        Ok(AstNode::ReturnStatement { value })
    }

    fn parse_if_statement(&mut self) -> Result<AstNode> {
        self.consume(&TokenType::LeftParen, "Expected '(' after 'if'")?;
        let condition = Box::new(self.parse_expression()?);
        self.consume(&TokenType::RightParen, "Expected ')' after if condition")?;

        let then_branch = Box::new(self.parse_statement()?);
        let else_branch = if self.match_token(&TokenType::Else) {
            Some(Box::new(self.parse_statement()?))
        } else {
            None
        };

        Ok(AstNode::IfStatement {
            condition,
            then_branch,
            else_branch,
        })
    }

    fn parse_while_statement(&mut self) -> Result<AstNode> {
        self.consume(&TokenType::LeftParen, "Expected '(' after 'while'")?;
        let condition = Box::new(self.parse_expression()?);
        self.consume(&TokenType::RightParen, "Expected ')' after while condition")?;

        let body = Box::new(self.parse_statement()?);

        Ok(AstNode::WhileLoop { condition, body })
    }

    fn parse_for_statement(&mut self) -> Result<AstNode> {
        self.consume(&TokenType::LeftParen, "Expected '(' after 'for'")?;

        let initializer = if self.match_token(&TokenType::Semicolon) {
            None
        } else {
            Some(Box::new(self.parse_statement()?))
        };

        let condition = if self.check(&TokenType::Semicolon) {
            None
        } else {
            Some(Box::new(self.parse_expression()?))
        };
        self.consume(&TokenType::Semicolon, "Expected ';' after for loop condition")?;

        let increment = if self.check(&TokenType::RightParen) {
            None
        } else {
            Some(Box::new(self.parse_expression()?))
        };
        self.consume(&TokenType::RightParen, "Expected ')' after for clauses")?;

        let body = Box::new(self.parse_statement()?);

        Ok(AstNode::ForLoop {
            initializer,
            condition,
            increment,
            body,
        })
    }

    fn parse_block(&mut self) -> Result<AstNode> {
        self.consume(&TokenType::LeftBrace, "Expected '{'")?;

        let mut statements = Vec::new();
        while !self.check(&TokenType::RightBrace) && !self.is_at_end() {
            // Skip whitespace and comments
            if self.check(&TokenType::Whitespace) || self.check(&TokenType::Newline) || self.peek().is_comment() {
                self.advance();
                continue;
            }

            statements.push(self.parse_statement()?);
        }

        self.consume(&TokenType::RightBrace, "Expected '}'")?;

        Ok(AstNode::Block { statements })
    }

    fn parse_expression(&mut self) -> Result<AstNode> {
        self.parse_assignment()
    }

    fn parse_assignment(&mut self) -> Result<AstNode> {
        let expr = self.parse_logical_or()?;

        if self.peek().is_assignment_operator() {
            let operator = self.advance().text.clone();
            let right = self.parse_assignment()?;
            return Ok(AstNode::BinaryExpression {
                left: Box::new(expr),
                operator,
                right: Box::new(right),
            });
        }

        Ok(expr)
    }

    fn parse_logical_or(&mut self) -> Result<AstNode> {
        let mut expr = self.parse_logical_and()?;

        while self.match_token(&TokenType::OrOr) {
            let operator = self.previous().text.clone();
            let right = self.parse_logical_and()?;
            expr = AstNode::BinaryExpression {
                left: Box::new(expr),
                operator,
                right: Box::new(right),
            };
        }

        Ok(expr)
    }

    fn parse_logical_and(&mut self) -> Result<AstNode> {
        let mut expr = self.parse_equality()?;

        while self.match_token(&TokenType::AndAnd) {
            let operator = self.previous().text.clone();
            let right = self.parse_equality()?;
            expr = AstNode::BinaryExpression {
                left: Box::new(expr),
                operator,
                right: Box::new(right),
            };
        }

        Ok(expr)
    }

    fn parse_equality(&mut self) -> Result<AstNode> {
        let mut expr = self.parse_comparison()?;

        while self.match_token(&TokenType::NotEqual) || self.match_token(&TokenType::EqualEqual) {
            let operator = self.previous().text.clone();
            let right = self.parse_comparison()?;
            expr = AstNode::BinaryExpression {
                left: Box::new(expr),
                operator,
                right: Box::new(right),
            };
        }

        Ok(expr)
    }

    fn parse_comparison(&mut self) -> Result<AstNode> {
        let mut expr = self.parse_term()?;

        while self.match_token(&TokenType::Greater)
            || self.match_token(&TokenType::GreaterEqual)
            || self.match_token(&TokenType::Less)
            || self.match_token(&TokenType::LessEqual)
        {
            let operator = self.previous().text.clone();
            let right = self.parse_term()?;
            expr = AstNode::BinaryExpression {
                left: Box::new(expr),
                operator,
                right: Box::new(right),
            };
        }

        Ok(expr)
    }

    fn parse_term(&mut self) -> Result<AstNode> {
        let mut expr = self.parse_factor()?;

        while self.match_token(&TokenType::Minus) || self.match_token(&TokenType::Plus) {
            let operator = self.previous().text.clone();
            let right = self.parse_factor()?;
            expr = AstNode::BinaryExpression {
                left: Box::new(expr),
                operator,
                right: Box::new(right),
            };
        }

        Ok(expr)
    }

    fn parse_factor(&mut self) -> Result<AstNode> {
        let mut expr = self.parse_unary()?;

        while self.match_token(&TokenType::Slash)
            || self.match_token(&TokenType::Star)
            || self.match_token(&TokenType::Percent)
        {
            let operator = self.previous().text.clone();
            let right = self.parse_unary()?;
            expr = AstNode::BinaryExpression {
                left: Box::new(expr),
                operator,
                right: Box::new(right),
            };
        }

        Ok(expr)
    }

    fn parse_unary(&mut self) -> Result<AstNode> {
        if self.match_token(&TokenType::Not)
            || self.match_token(&TokenType::Minus)
            || self.match_token(&TokenType::Plus)
        {
            let operator = self.previous().text.clone();
            let right = self.parse_unary()?;
            return Ok(AstNode::UnaryExpression {
                operator,
                operand: Box::new(right),
            });
        }

        self.parse_call()
    }

    fn parse_call(&mut self) -> Result<AstNode> {
        let mut expr = self.parse_primary()?;

        loop {
            if self.match_token(&TokenType::LeftParen) {
                expr = self.finish_call(expr)?;
            } else {
                break;
            }
        }

        Ok(expr)
    }

    fn finish_call(&mut self, callee: AstNode) -> Result<AstNode> {
        let mut arguments = Vec::new();

        if !self.check(&TokenType::RightParen) {
            loop {
                arguments.push(self.parse_expression()?);
                if !self.match_token(&TokenType::Comma) {
                    break;
                }
            }
        }

        self.consume(&TokenType::RightParen, "Expected ')' after arguments")?;

        Ok(AstNode::CallExpression {
            callee: Box::new(callee),
            arguments,
        })
    }

    fn parse_primary(&mut self) -> Result<AstNode> {
        if self.match_token(&TokenType::True) || self.match_token(&TokenType::False) || self.match_token(&TokenType::Null) {
            return Ok(AstNode::Literal(self.previous().text.clone()));
        }

        if self.check(&TokenType::NumericLiteral) || self.check(&TokenType::StringLiteral) || self.check(&TokenType::CharLiteral) {
            return Ok(AstNode::Literal(self.advance().text.clone()));
        }

        if self.check(&TokenType::Identifier) {
            return Ok(AstNode::Identifier(self.advance().text.clone()));
        }

        if self.match_token(&TokenType::LeftParen) {
            let expr = self.parse_expression()?;
            self.consume(&TokenType::RightParen, "Expected ')' after expression")?;
            return Ok(expr);
        }

        Err(FormatError::parse_error(
            self.peek().line,
            self.peek().column,
            "Expected expression",
        ))
    }

    // Helper methods
    fn match_token(&mut self, token_type: &TokenType) -> bool {
        if self.check(token_type) {
            self.advance();
            true
        } else {
            false
        }
    }

    fn check(&self, token_type: &TokenType) -> bool {
        if self.is_at_end() {
            false
        } else {
            &self.peek().token_type == token_type
        }
    }

    fn advance(&mut self) -> &Token {
        if !self.is_at_end() {
            self.current += 1;
        }
        self.previous()
    }

    fn is_at_end(&self) -> bool {
        self.current >= self.tokens.len() || self.peek().token_type == TokenType::Eof
    }

    fn peek(&self) -> &Token {
        &self.tokens[self.current.min(self.tokens.len() - 1)]
    }

    fn previous(&self) -> &Token {
        &self.tokens[self.current - 1]
    }

    fn consume(&mut self, token_type: &TokenType, message: &str) -> Result<&Token> {
        if self.check(token_type) {
            Ok(self.advance())
        } else {
            Err(FormatError::parse_error(
                self.peek().line,
                self.peek().column,
                message,
            ))
        }
    }
}