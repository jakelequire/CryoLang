use crate::config::FormatOptions;
use crate::error::{FormatError, Result};
use crate::lexer::Lexer;
use crate::parser::{AstNode, Parser};
use crate::token::{Token, TokenType};
use std::fs;
use std::path::Path;

pub fn format_file<P: AsRef<Path>>(path: P, options: &FormatOptions) -> Result<String> {
    let content = fs::read_to_string(&path)
        .map_err(|e| FormatError::Io(e))?;
    format_string(&content, options)
}

pub fn format_string(input: &str, options: &FormatOptions) -> Result<String> {
    let mut lexer = Lexer::new(input);
    let tokens = lexer.tokenize()?;
    
    let mut parser = Parser::new(tokens);
    let ast = parser.parse()?;
    
    let mut formatter = Formatter::new(options);
    formatter.format_ast(&ast)
}

struct Formatter<'a> {
    options: &'a FormatOptions,
    output: String,
    indent_level: usize,
    current_line_length: usize,
    needs_spacing: bool,
}

impl<'a> Formatter<'a> {
    fn new(options: &'a FormatOptions) -> Self {
        Formatter {
            options,
            output: String::new(),
            indent_level: 0,
            current_line_length: 0,
            needs_spacing: false,
        }
    }

    fn format_ast(&mut self, ast: &AstNode) -> Result<String> {
        self.format_node(ast)?;
        
        if self.options.format.insert_final_newline && !self.output.ends_with('\n') {
            self.output.push('\n');
        }
        
        if self.options.format.trim_trailing_whitespace {
            self.output = self.output
                .lines()
                .map(|line| line.trim_end())
                .collect::<Vec<_>>()
                .join("\n");
            
            if self.options.format.insert_final_newline {
                self.output.push('\n');
            }
        }
        
        Ok(self.output.clone())
    }

    fn format_node(&mut self, node: &AstNode) -> Result<()> {
        match node {
            AstNode::Program { items } => {
                let mut first = true;
                for item in items {
                    if !first {
                        self.write_newline();
                        if self.should_add_blank_line_between_items() {
                            self.write_newline();
                        }
                    }
                    first = false;
                    self.format_node(item)?;
                }
            }

            AstNode::VariableDeclaration {
                is_mutable,
                name,
                type_annotation,
                initializer,
            } => {
                self.write_indent();
                
                if *is_mutable {
                    self.write("mut");
                } else {
                    self.write("const");
                }
                
                self.write_space();
                self.write(name);
                
                if let Some(type_ann) = type_annotation {
                    self.write(":");
                    if self.options.spacing.after_colon {
                        self.write_space();
                    }
                    self.write(type_ann);
                }
                
                if let Some(init) = initializer {
                    if self.options.spacing.assignment_operators {
                        self.write_space();
                    }
                    self.write("=");
                    if self.options.spacing.assignment_operators {
                        self.write_space();
                    }
                    self.format_node(init)?;
                }
                
                self.write(";");
            }

            AstNode::FunctionDeclaration {
                name,
                parameters,
                return_type,
                body,
            } => {
                self.write_indent();
                self.write("function");
                self.write_space();
                self.write(name);
                
                self.write("(");
                
                if !parameters.is_empty() {
                    if self.options.spacing.inside_parentheses {
                        self.write_space();
                    }
                    
                    for (i, (param_name, param_type)) in parameters.iter().enumerate() {
                        if i > 0 {
                            self.write(",");
                            if self.options.spacing.after_comma {
                                self.write_space();
                            }
                        }
                        
                        self.write(param_name);
                        self.write(":");
                        if self.options.spacing.after_colon {
                            self.write_space();
                        }
                        self.write(param_type);
                    }
                    
                    if self.options.spacing.inside_parentheses {
                        self.write_space();
                    }
                }
                
                self.write(")");
                
                if let Some(ret_type) = return_type {
                    self.write_space();
                    self.write("->");
                    self.write_space();
                    self.write(ret_type);
                }
                
                self.write_space();
                self.format_node(body)?;
            }

            AstNode::StructDeclaration { name, fields } => {
                self.write_indent();
                self.write("type struct");
                self.write_space();
                self.write(name);
                self.write_space();
                self.write("{");
                
                if !fields.is_empty() {
                    self.write_newline();
                    self.indent_level += 1;
                    
                    for (i, (field_name, field_type)) in fields.iter().enumerate() {
                        if i > 0 {
                            self.write(",");
                            self.write_newline();
                        }
                        
                        self.write_indent();
                        self.write(field_name);
                        self.write(":");
                        if self.options.spacing.after_colon {
                            self.write_space();
                        }
                        self.write(field_type);
                    }
                    
                    self.indent_level -= 1;
                    self.write_newline();
                    self.write_indent();
                }
                
                self.write("}");
            }

            AstNode::Block { statements } => {
                self.write("{");
                
                if !statements.is_empty() {
                    self.write_newline();
                    self.indent_level += 1;
                    
                    for statement in statements {
                        self.format_node(statement)?;
                        self.write_newline();
                    }
                    
                    self.indent_level -= 1;
                    self.write_indent();
                }
                
                self.write("}");
            }

            AstNode::Expression(expr) => {
                self.format_node(expr)?;
                self.write(";");
            }

            AstNode::BinaryExpression { left, operator, right } => {
                self.format_node(left)?;
                
                if self.options.spacing.binary_operators {
                    self.write_space();
                }
                self.write(operator);
                if self.options.spacing.binary_operators {
                    self.write_space();
                }
                
                self.format_node(right)?;
            }

            AstNode::UnaryExpression { operator, operand } => {
                self.write(operator);
                self.format_node(operand)?;
            }

            AstNode::CallExpression { callee, arguments } => {
                self.format_node(callee)?;
                self.write("(");
                
                if !arguments.is_empty() {
                    if self.options.spacing.inside_parentheses {
                        self.write_space();
                    }
                    
                    for (i, arg) in arguments.iter().enumerate() {
                        if i > 0 {
                            self.write(",");
                            if self.options.spacing.after_comma {
                                self.write_space();
                            }
                        }
                        self.format_node(arg)?;
                    }
                    
                    if self.options.spacing.inside_parentheses {
                        self.write_space();
                    }
                }
                
                self.write(")");
            }

            AstNode::Identifier(name) => {
                self.write(name);
            }

            AstNode::Literal(value) => {
                self.write(value);
            }

            AstNode::ReturnStatement { value } => {
                self.write_indent();
                self.write("return");
                
                if let Some(val) = value {
                    self.write_space();
                    self.format_node(val)?;
                }
                
                self.write(";");
            }

            AstNode::IfStatement {
                condition,
                then_branch,
                else_branch,
            } => {
                self.write_indent();
                self.write("if");
                self.write_space();
                self.write("(");
                self.format_node(condition)?;
                self.write(")");
                self.write_space();
                
                self.format_node(then_branch)?;
                
                if let Some(else_stmt) = else_branch {
                    self.write_space();
                    self.write("else");
                    self.write_space();
                    self.format_node(else_stmt)?;
                }
            }

            AstNode::WhileLoop { condition, body } => {
                self.write_indent();
                self.write("while");
                self.write_space();
                self.write("(");
                self.format_node(condition)?;
                self.write(")");
                self.write_space();
                self.format_node(body)?;
            }

            AstNode::ForLoop {
                initializer,
                condition,
                increment,
                body,
            } => {
                self.write_indent();
                self.write("for");
                self.write_space();
                self.write("(");
                
                if let Some(init) = initializer {
                    self.format_node(init)?;
                } else {
                    self.write(";");
                }
                
                self.write_space();
                
                if let Some(cond) = condition {
                    self.format_node(cond)?;
                }
                self.write(";");
                
                if let Some(inc) = increment {
                    self.write_space();
                    self.format_node(inc)?;
                }
                
                self.write(")");
                self.write_space();
                self.format_node(body)?;
            }
        }
        
        Ok(())
    }

    fn write(&mut self, text: &str) {
        self.output.push_str(text);
        self.current_line_length += text.len();
        self.needs_spacing = false;
    }

    fn write_space(&mut self) {
        if !self.needs_spacing {
            self.output.push(' ');
            self.current_line_length += 1;
            self.needs_spacing = true;
        }
    }

    fn write_newline(&mut self) {
        self.output.push('\n');
        self.current_line_length = 0;
        self.needs_spacing = false;
    }

    fn write_indent(&mut self) {
        let indent = self.options.get_indent_string();
        for _ in 0..self.indent_level {
            self.output.push_str(&indent);
            self.current_line_length += indent.len();
        }
        self.needs_spacing = false;
    }

    fn should_add_blank_line_between_items(&self) -> bool {
        // Add logic for when to add blank lines between top-level items
        // For now, add blank line between functions and structs
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_format_variable_declaration() {
        let input = "const   x:int=42;";
        let options = FormatOptions::default();
        let result = format_string(input, &options).unwrap();
        assert_eq!(result.trim(), "const x: int = 42;");
    }

    #[test]
    fn test_format_function_declaration() {
        let input = "function main()->int{return 0;}";
        let options = FormatOptions::default();
        let result = format_string(input, &options).unwrap();
        assert_eq!(result.trim(), "function main() -> int {\n    return 0;\n}");
    }

    #[test]
    fn test_format_struct_declaration() {
        let input = "type struct Point{x:int,y:int}";
        let options = FormatOptions::default();
        let result = format_string(input, &options).unwrap();
        assert_eq!(result.trim(), "type struct Point {\n    x: int,\n    y: int\n}");
    }

    #[test]
    fn test_format_binary_expression() {
        let input = "const result:int=a+b*c;";
        let options = FormatOptions::default();
        let result = format_string(input, &options).unwrap();
        assert_eq!(result.trim(), "const result: int = a + b * c;");
    }

    #[test]
    fn test_format_function_with_parameters() {
        let input = "function add(a:int,b:int)->int{return a+b;}";
        let options = FormatOptions::default();
        let result = format_string(input, &options).unwrap();
        assert_eq!(result.trim(), "function add(a: int, b: int) -> int {\n    return a + b;\n}");
    }
}