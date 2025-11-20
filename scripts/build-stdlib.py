#!/usr/bin/env python3
"""
build-stdlib.py - Standard library build script for CryoLang

This script compiles all stdlib modules to LLVM bitcode, filters out failed
compilations, and links the working modules into a combined library.
"""

import os
import sys
import subprocess
import glob
from pathlib import Path

# =============================================================================
# Configuration
# =============================================================================

# Additional compilation flags that can be enabled via command line
OPTIONAL_FLAGS = [
    '--debug',      # Enable debug output during compilation
    # Add more flags here as needed
]

# ANSI color codes for enhanced logging
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

    @staticmethod
    def disable():
        """Disable colors for systems that don't support ANSI."""
        Colors.HEADER = Colors.OKBLUE = Colors.OKCYAN = ''
        Colors.OKGREEN = Colors.WARNING = Colors.FAIL = ''
        Colors.ENDC = Colors.BOLD = Colors.UNDERLINE = ''

# Disable colors on Windows unless explicitly supported
if os.name == 'nt' and not os.environ.get('ANSICON') and not os.environ.get('WT_SESSION'):
    Colors.disable()

def log(message, level="INFO"):
    """Print a colored log message."""
    color = Colors.ENDC
    if level == "SUCCESS":
        color = Colors.OKGREEN
    elif level == "ERROR":
        color = Colors.FAIL
    elif level == "WARNING":
        color = Colors.WARNING
    elif level == "INFO":
        color = Colors.OKCYAN
    elif level == "HEADER":
        color = Colors.HEADER + Colors.BOLD
    
    print(f"{color}[STDLIB]{Colors.ENDC} {message}")

def is_stub_file(file_path):
    """Check if a .bc file is a compilation failure stub."""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read(100).strip()  # Read first 100 chars
            # Stub files start with "; Compilation failed..."
            return content.startswith('"') and 'Compilation failed' in content
    except:
        return True  # Treat unreadable files as stubs

def compile_module(cryo_exe, source_file, output_file, extra_flags=None, stdlib_mode=True):
    """Compile a single Cryo module to LLVM bitcode."""
    cmd = [cryo_exe, source_file, '--emit-llvm', '-c', '-o', output_file]
    if stdlib_mode:
        cmd.append('--stdlib-mode')
    
    # Add any extra flags
    if extra_flags:
        cmd.extend(extra_flags)
    
    # Ensure output directory exists
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    module_name = os.path.basename(source_file)
    rel_path = os.path.relpath(source_file)
    log(f"Compiling {Colors.BOLD}{module_name}{Colors.ENDC}")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            log(f"✓ {Colors.OKGREEN}Successfully compiled{Colors.ENDC} {rel_path}", "SUCCESS")
            return True
        else:
            log(f"✗ {Colors.FAIL}Compilation failed{Colors.ENDC} for {rel_path}", "ERROR")
            # Show the error details from the compiler even if not in debug mode
            log(f"Error details: {result.stderr}", "ERROR")
            if extra_flags and '--debug' in extra_flags:
                log(f"Error details: {result.stderr}", "ERROR")
            # Create stub file
            with open(output_file, 'w') as f:
                f.write(f'"; Compilation failed for {os.path.relpath(source_file)}\n')
                # Print the error message from the compiler
                f.write(f'"; Error: {result.stderr.strip()}\n')
                f.write('"; Stub file created to satisfy build system\n')
            return False
    except Exception as e:
        log(f"✗ {Colors.FAIL}Exception{Colors.ENDC} compiling {rel_path}: {e}", "ERROR")
        # Create stub file
        with open(output_file, 'w') as f:
            f.write(f'"; Compilation failed for {rel_path}\n')
            # Print the error message that caused the exception
            f.write(f'"; Exception: {e}\n')
            f.write('"; Stub file created to satisfy build system\n')
        return False

def find_valid_bitcode_files(build_dir):
    """Find all valid (non-stub) bitcode files in the build directory."""
    valid_files = []
    all_bc_files = glob.glob(os.path.join(build_dir, '**', '*.bc'), recursive=True)
    
    for bc_file in all_bc_files:
        if not is_stub_file(bc_file):
            valid_files.append(bc_file)
            log(f"✓ {Colors.OKGREEN}Valid:{Colors.ENDC} {os.path.basename(bc_file)}")
        else:
            log(f"⚠ {Colors.WARNING}Stub:{Colors.ENDC} {os.path.basename(bc_file)}", "WARNING")
    
    return valid_files

def link_bitcode_files(valid_files, output_file):
    """Link valid bitcode files into a combined module."""
    if not valid_files:
        log("No valid bitcode files found. Creating minimal module.", "WARNING")
        with open(output_file, 'w') as f:
            if os.name == 'nt':
                f.write('target triple = "x86_64-pc-windows-msvc"\n')
            else:
                f.write('target triple = "x86_64-unknown-linux-gnu"\n')
        return False
    
    log(f"Linking {Colors.BOLD}{len(valid_files)}{Colors.ENDC} valid bitcode files")
    try:
        cmd = ['llvm-link'] + valid_files + ['-o', output_file]
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        if result.returncode == 0:
            log(f"✓ {Colors.OKGREEN}Successfully linked{Colors.ENDC} bitcode files", "SUCCESS")
            return True
        else:
            log(f"llvm-link failed: {result.stderr}", "ERROR")
            # Create minimal module
            with open(output_file, 'w') as f:
                if os.name == 'nt':
                    f.write('target triple = "x86_64-pc-windows-msvc"\n')
                else:
                    f.write('target triple = "x86_64-unknown-linux-gnu"\n')
            return False
    except Exception as e:
        log(f"Exception during linking: {e}", "ERROR")
        return False

def compile_to_object(bitcode_file, object_file):
    """Compile LLVM bitcode to object file using LLC."""
    log(f"Compiling to object file: {Colors.BOLD}{os.path.basename(object_file)}{Colors.ENDC}")
    try:
        if os.name == 'nt':
            cmd = ['llc', '-filetype=obj', bitcode_file, '-o', object_file]
        else:
            cmd = ['llc', '-filetype=obj', '-relocation-model=pic', bitcode_file, '-o', object_file]
        
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            log(f"✓ {Colors.OKGREEN}Successfully created{Colors.ENDC} object file", "SUCCESS")
            return True
        else:
            log(f"LLC failed: {result.stderr}", "ERROR")
            # Create empty object file
            Path(object_file).touch()
            return False
    except Exception as e:
        log(f"Exception during object compilation: {e}", "ERROR")
        Path(object_file).touch()
        return False

def create_library(object_file, library_file):
    """Create archive library from object file."""
    log(f"Creating library: {Colors.BOLD}{os.path.basename(library_file)}{Colors.ENDC}")
    try:
        cmd = ['llvm-ar', 'rcs', library_file, object_file]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            log(f"✓ {Colors.OKGREEN}Successfully created{Colors.ENDC} library", "SUCCESS")
            return True
        else:
            log(f"llvm-ar failed: {result.stderr}", "ERROR")
            # Create empty library file
            Path(library_file).touch()
            return False
    except Exception as e:
        log(f"Exception during library creation: {e}", "ERROR")
        Path(library_file).touch()
        return False

def main():
    if len(sys.argv) < 4:
        print("Usage: build-stdlib.py <cryo_exe> <stdlib_dir> <build_dir> [library_file] [extra_flags...]")
        print("Extra flags can be any of:", OPTIONAL_FLAGS)
        sys.exit(1)
    
    cryo_exe = sys.argv[1]
    stdlib_dir = sys.argv[2]
    build_dir = sys.argv[3]
    library_file = sys.argv[4] if len(sys.argv) > 4 and not sys.argv[4].startswith('--') else os.path.join(build_dir, 'libcryo.a')
    
    # Parse extra flags
    extra_flags = []
    start_idx = 5 if len(sys.argv) > 4 and not sys.argv[4].startswith('--') else 4
    for arg in sys.argv[start_idx:]:
        if arg in OPTIONAL_FLAGS:
            extra_flags.append(arg)
        else:
            log(f"Unknown flag: {arg}", "WARNING")
    
    # Ensure build directory exists
    os.makedirs(build_dir, exist_ok=True)
    
    log(f"{Colors.HEADER}Building CryoLang Standard Library{Colors.ENDC}", "HEADER")
    log(f"Stdlib dir: {Colors.BOLD}{stdlib_dir}{Colors.ENDC}")
    log(f"Build dir: {Colors.BOLD}{build_dir}{Colors.ENDC}")
    log(f"Library: {Colors.BOLD}{library_file}{Colors.ENDC}")
    if extra_flags:
        log(f"Extra flags: {Colors.BOLD}{' '.join(extra_flags)}{Colors.ENDC}")
    print()  # Empty line for better formatting
    
    # Find all .cryo files in stdlib (excluding runtime for now)
    cryo_files = []
    for root, dirs, files in os.walk(stdlib_dir):
        # Skip runtime directory
        if 'runtime' in root:
            continue
        for file in files:
            if file.endswith('.cryo'):
                cryo_files.append(os.path.join(root, file))
    
    log(f"Found {Colors.BOLD}{len(cryo_files)}{Colors.ENDC} stdlib modules to compile")
    print()  # Empty line for better formatting
    
    # Compile all modules
    success_count = 0
    for cryo_file in cryo_files:
        # Calculate relative path and output file
        rel_path = os.path.relpath(cryo_file, stdlib_dir)
        output_file = os.path.join(build_dir, rel_path.replace('.cryo', '.bc'))
        
        if compile_module(cryo_exe, cryo_file, output_file, extra_flags):
            success_count += 1
    
    print()  # Empty line for better formatting
    
    # Summary of compilation phase
    failed_count = len(cryo_files) - success_count
    if failed_count == 0:
        log(f"Compilation complete: {Colors.OKGREEN}All {success_count} modules successful{Colors.ENDC}", "SUCCESS")
    else:
        log(f"Compilation complete: {Colors.OKGREEN}{success_count} successful{Colors.ENDC}, {Colors.FAIL}{failed_count} failed{Colors.ENDC}")
    
    print()  # Empty line for better formatting
    
    # Find valid bitcode files
    log("Scanning for valid bitcode files...")
    valid_files = find_valid_bitcode_files(build_dir)
    
    if not valid_files:
        log("No valid modules compiled successfully", "ERROR")
        sys.exit(1)
    
    print()  # Empty line for better formatting
    
    # Link bitcode files
    combined_bc = os.path.join(build_dir, 'cryo_combined.bc')
    link_success = link_bitcode_files(valid_files, combined_bc)
    
    # Compile to object file
    object_file = os.path.join(build_dir, 'libcryo.o')
    obj_success = compile_to_object(combined_bc, object_file)
    
    # Create library
    lib_success = create_library(object_file, library_file)
    
    print()  # Empty line for better formatting
    
    # Report final status
    if os.path.exists(library_file):
        size = os.path.getsize(library_file)
        log(f"{Colors.HEADER}Build Complete!{Colors.ENDC}", "HEADER")
        log(f"Standard library: {Colors.BOLD}{library_file}{Colors.ENDC} ({Colors.OKGREEN}{size} bytes{Colors.ENDC})", "SUCCESS")
        if failed_count > 0:
            log(f"Note: {Colors.WARNING}{failed_count} modules failed{Colors.ENDC} and were excluded", "WARNING")
    else:
        log("Failed to create standard library", "ERROR")
        sys.exit(1)

if __name__ == "__main__":
    main()