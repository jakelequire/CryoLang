use anyhow::Result;
use clap::Parser;
use cryofmt::{format_file, format_string, Config, FormatOptions};
use std::fs;
use std::io::{self, Read, Write};
use std::path::PathBuf;
use termcolor::{Color, ColorChoice, ColorSpec, StandardStream, WriteColor};
use walkdir::WalkDir;

#[derive(Parser)]
#[command(name = "cryofmt")]
#[command(about = "A formatter for CryoLang source code")]
#[command(version = env!("CARGO_PKG_VERSION"))]
struct Cli {
    /// Files or directories to format
    #[arg(value_name = "FILE")]
    files: Vec<PathBuf>,

    /// Write result to stdout instead of modifying files
    #[arg(short = 'd', long = "diff")]
    diff: bool,

    /// List files that would be reformatted
    #[arg(short = 'l', long = "list")]
    list_files: bool,

    /// Write result to stdout instead of modifying files
    #[arg(short = 'w', long = "write")]
    write: bool,

    /// Check if files are already formatted (exit 1 if not)
    #[arg(long = "check")]
    check: bool,

    /// Configuration file path
    #[arg(short = 'c', long = "config")]
    config: Option<PathBuf>,

    /// Verbose output
    #[arg(short = 'v', long = "verbose")]
    verbose: bool,

    /// Disable colored output
    #[arg(long = "no-color")]
    no_color: bool,

    /// Tab width for indentation
    #[arg(long = "tab-width", default_value = "4")]
    tab_width: usize,

    /// Use tabs instead of spaces
    #[arg(long = "use-tabs")]
    use_tabs: bool,

    /// Maximum line length
    #[arg(long = "max-width", default_value = "100")]
    max_width: usize,

    /// Process directories recursively
    #[arg(short = 'r', long = "recursive")]
    recursive: bool,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    
    let color_choice = if cli.no_color {
        ColorChoice::Never
    } else {
        ColorChoice::Auto
    };
    
    let mut config = if let Some(config_path) = &cli.config {
        Config::from_file(config_path)?
    } else {
        Config::find_and_load()?
    };

    // Override config with CLI options
    if cli.use_tabs {
        config.indent.use_tabs = true;
    }
    if cli.tab_width != 4 {
        config.indent.tab_width = cli.tab_width;
    }
    if cli.max_width != 100 {
        config.format.max_width = cli.max_width;
    }

    let format_options = FormatOptions::from_config(&config);

    if cli.files.is_empty() {
        // Read from stdin
        let mut input = String::new();
        io::stdin().read_to_string(&mut input)?;
        
        match format_string(&input, &format_options) {
            Ok(formatted) => {
                if cli.check {
                    if input != formatted {
                        print_error("Input is not formatted", color_choice)?;
                        std::process::exit(1);
                    }
                } else {
                    print!("{}", formatted);
                }
            }
            Err(e) => {
                print_error(&format!("Format error: {}", e), color_choice)?;
                std::process::exit(1);
            }
        }
    } else {
        process_files(&cli, &format_options, color_choice)?;
    }

    Ok(())
}

fn process_files(cli: &Cli, format_options: &FormatOptions, color_choice: ColorChoice) -> Result<()> {
    let mut files_to_process = Vec::new();
    
    for file_path in &cli.files {
        if file_path.is_file() {
            if is_cryo_file(file_path) {
                files_to_process.push(file_path.clone());
            }
        } else if file_path.is_dir() {
            if cli.recursive {
                for entry in WalkDir::new(file_path).into_iter().filter_map(|e| e.ok()) {
                    let path = entry.path();
                    if path.is_file() && is_cryo_file(path) {
                        files_to_process.push(path.to_path_buf());
                    }
                }
            } else {
                for entry in fs::read_dir(file_path)? {
                    let entry = entry?;
                    let path = entry.path();
                    if path.is_file() && is_cryo_file(&path) {
                        files_to_process.push(path);
                    }
                }
            }
        }
    }

    let mut needs_formatting = false;
    let mut error_count = 0;

    for file_path in files_to_process {
        match process_single_file(&file_path, cli, format_options) {
            Ok(changed) => {
                if changed {
                    needs_formatting = true;
                    if cli.list_files {
                        println!("{}", file_path.display());
                    } else if cli.verbose && !cli.check {
                        print_success(&format!("Formatted {}", file_path.display()), color_choice)?;
                    }
                }
            }
            Err(e) => {
                error_count += 1;
                print_error(
                    &format!("Error processing {}: {}", file_path.display(), e),
                    color_choice,
                )?;
            }
        }
    }

    if cli.check && needs_formatting {
        print_error("Some files are not formatted", color_choice)?;
        std::process::exit(1);
    }

    if error_count > 0 {
        std::process::exit(1);
    }

    Ok(())
}

fn process_single_file(
    file_path: &PathBuf,
    cli: &Cli,
    format_options: &FormatOptions,
) -> Result<bool> {
    let original_content = fs::read_to_string(file_path)?;
    let formatted_content = format_string(&original_content, format_options)?;

    let changed = original_content != formatted_content;

    if cli.diff && changed {
        print_diff(file_path, &original_content, &formatted_content)?;
    } else if cli.check {
        // Just return whether it changed
    } else if cli.list_files {
        // List mode handles output in caller
    } else if cli.write || (!cli.diff && !cli.list_files) {
        if changed {
            fs::write(file_path, &formatted_content)?;
        }
    } else {
        // Write to stdout
        print!("{}", formatted_content);
    }

    Ok(changed)
}

fn is_cryo_file(path: &std::path::Path) -> bool {
    path.extension()
        .and_then(|ext| ext.to_str())
        .map_or(false, |ext| ext == "cryo")
}

fn print_diff(file_path: &PathBuf, original: &str, formatted: &str) -> Result<()> {
    println!("--- {}", file_path.display());
    println!("+++ {}", file_path.display());
    
    let original_lines: Vec<&str> = original.lines().collect();
    let formatted_lines: Vec<&str> = formatted.lines().collect();
    
    for (i, (orig, fmt)) in original_lines.iter().zip(formatted_lines.iter()).enumerate() {
        if orig != fmt {
            println!("-{}: {}", i + 1, orig);
            println!("+{}: {}", i + 1, fmt);
        }
    }
    
    Ok(())
}

fn print_error(message: &str, color_choice: ColorChoice) -> Result<()> {
    let mut stderr = StandardStream::stderr(color_choice);
    stderr.set_color(ColorSpec::new().set_fg(Some(Color::Red)).set_bold(true))?;
    write!(&mut stderr, "error:")?;
    stderr.reset()?;
    writeln!(&mut stderr, " {}", message)?;
    Ok(())
}

fn print_success(message: &str, color_choice: ColorChoice) -> Result<()> {
    let mut stdout = StandardStream::stdout(color_choice);
    stdout.set_color(ColorSpec::new().set_fg(Some(Color::Green)))?;
    writeln!(&mut stdout, "{}", message)?;
    stdout.reset()?;
    Ok(())
}