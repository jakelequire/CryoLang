use assert_cmd::Command;
use predicates::prelude::*;
use std::fs;
use tempfile::TempDir;

#[test]
fn test_format_basic_file() {
    let temp_dir = TempDir::new().unwrap();
    let file_path = temp_dir.path().join("test.cryo");
    
    fs::write(&file_path, "const   x:int=42;").unwrap();
    
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg(file_path.to_str().unwrap())
        .assert()
        .success();
    
    let content = fs::read_to_string(&file_path).unwrap();
    assert_eq!(content.trim(), "const x: int = 42;");
}

#[test]
fn test_check_mode() {
    let temp_dir = TempDir::new().unwrap();
    let file_path = temp_dir.path().join("test.cryo");
    
    // Write unformatted code
    fs::write(&file_path, "const   x:int=42;").unwrap();
    
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg("--check")
        .arg(file_path.to_str().unwrap())
        .assert()
        .failure() // Should fail because file is not formatted
        .code(1);
    
    // Format the file
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg(file_path.to_str().unwrap())
        .assert()
        .success();
    
    // Now check should pass
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg("--check")
        .arg(file_path.to_str().unwrap())
        .assert()
        .success();
}

#[test]
fn test_diff_mode() {
    let temp_dir = TempDir::new().unwrap();
    let file_path = temp_dir.path().join("test.cryo");
    
    fs::write(&file_path, "const   x:int=42;").unwrap();
    
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg("--diff")
        .arg(file_path.to_str().unwrap())
        .assert()
        .success()
        .stdout(predicate::str::contains("const   x:int=42;"))
        .stdout(predicate::str::contains("const x: int = 42;"));
}

#[test]
fn test_list_mode() {
    let temp_dir = TempDir::new().unwrap();
    let file_path = temp_dir.path().join("test.cryo");
    
    fs::write(&file_path, "const   x:int=42;").unwrap();
    
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg("--list")
        .arg(file_path.to_str().unwrap())
        .assert()
        .success()
        .stdout(predicate::str::contains(file_path.to_str().unwrap()));
}

#[test]
fn test_stdin_formatting() {
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.write_stdin("const   x:int=42;")
        .assert()
        .success()
        .stdout(predicate::str::contains("const x: int = 42;"));
}

#[test]
fn test_recursive_formatting() {
    let temp_dir = TempDir::new().unwrap();
    let src_dir = temp_dir.path().join("src");
    fs::create_dir(&src_dir).unwrap();
    
    let file1 = src_dir.join("file1.cryo");
    let file2 = src_dir.join("file2.cryo");
    
    fs::write(&file1, "const   x:int=42;").unwrap();
    fs::write(&file2, "const   y:string=\"hello\";").unwrap();
    
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg("-r")
        .arg(src_dir.to_str().unwrap())
        .assert()
        .success();
    
    let content1 = fs::read_to_string(&file1).unwrap();
    let content2 = fs::read_to_string(&file2).unwrap();
    
    assert_eq!(content1.trim(), "const x: int = 42;");
    assert_eq!(content2.trim(), "const y: string = \"hello\";");
}

#[test]
fn test_config_file() {
    let temp_dir = TempDir::new().unwrap();
    let config_path = temp_dir.path().join("cryofmt.toml");
    let file_path = temp_dir.path().join("test.cryo");
    
    fs::write(&config_path, r#"
[indent]
use_tabs = true
tab_width = 2

[spacing]
binary_operators = false
"#).unwrap();
    
    fs::write(&file_path, "function main()->int{return a+b;}").unwrap();
    
    // Change to temp directory so config file is found
    std::env::set_current_dir(&temp_dir).unwrap();
    
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg(file_path.to_str().unwrap())
        .assert()
        .success();
    
    let content = fs::read_to_string(&file_path).unwrap();
    assert!(content.contains('\t')); // Should use tabs
    assert!(content.contains("a+b")); // Should not add spaces around operators
}

#[test]
fn test_syntax_error_handling() {
    let temp_dir = TempDir::new().unwrap();
    let file_path = temp_dir.path().join("test.cryo");
    
    // Write syntactically invalid code
    fs::write(&file_path, "const x: = ;").unwrap();
    
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg(file_path.to_str().unwrap())
        .assert()
        .failure()
        .stderr(predicate::str::contains("error"));
}

#[test]
fn test_version_flag() {
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg("--version")
        .assert()
        .success()
        .stdout(predicate::str::contains(env!("CARGO_PKG_VERSION")));
}

#[test]
fn test_help_flag() {
    let mut cmd = Command::cargo_bin("cryofmt").unwrap();
    cmd.arg("--help")
        .assert()
        .success()
        .stdout(predicate::str::contains("A formatter for CryoLang source code"));
}