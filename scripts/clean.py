#*******************************************************************************
#  Copyright 2025 Jacob LeQuire                                                *
#  SPDX-License-Identifier: Apache-2.0                                         *
#    Licensed under the Apache License, Version 2.0 (the "License");           *
#    you may not use this file except in compliance with the License.          *
#    You may obtain a copy of the License at                                   *
#                                                                              *
#    http://www.apache.org/licenses/LICENSE-2.0                                *
#                                                                              *
#    Unless required by applicable law or agreed to in writing, software       *
#    distributed under the License is distributed on an "AS IS" BASIS,         *
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
#    See the License for the specific language governing permissions and       *
#    limitations under the License.                                            *
#                                                                              *
#*******************************************************************************/
# clean.py
import os
import shutil
import platform
import subprocess
from datetime import datetime
from pathlib import Path

object_files = []

# One directory up from this script
root_dir = Path(__file__).parent.parent.resolve()

def recursive_file_names():
    """Recursively collect all files in the .o directory"""
    folder = root_dir / "bin" / ".o"
    file_list = []
    
    if folder.exists():
        for file_path in folder.rglob("*"):
            if file_path.is_file():
                file_list.append(str(file_path))
    
    object_files.extend(file_list)  # Use extend instead of append
    return file_list

def delete_binaries():
    """Delete all binaries in the bin/ directory (executables only)"""
    bin_folder = root_dir / "bin"
    
    if not bin_folder.exists():
        print(f"Directory {bin_folder} does not exist")
        return
    
    try:
        # Get list of files to delete (skip .o directory and cleaned.txt)
        files_deleted = []
        for item in bin_folder.iterdir():
            if item.is_file() and item.name != "cleaned.txt":
                item.unlink()
                files_deleted.append(str(item))
            elif item.is_dir() and item.name != ".o":
                shutil.rmtree(item)
                files_deleted.append(str(item))
        
        if files_deleted:
            print(f"Deleted {len(files_deleted)} items from {bin_folder}")
        else:
            print(f"No binaries found to delete in {bin_folder}")
            
    except OSError as e:
        print(f"Error deleting binaries in {bin_folder}: {e}")

def delete_folder():
    """Delete the .o folder and all its contents"""
    folder = root_dir / "bin" / ".o"
    
    if folder.exists():
        try:
            shutil.rmtree(folder)
            print(f"Deleted {folder}")
        except OSError as e:
            print(f"Error deleting {folder}: {e}")
    else:
        print(f"Directory {folder} does not exist")

def get_compiler_version():
    """Get compiler version in a cross-platform way"""
    compilers = ["clang", "gcc", "cl"]  # cl is MSVC on Windows
    
    for compiler in compilers:
        try:
            if compiler == "cl":
                # MSVC uses different flag
                result = subprocess.run([compiler], capture_output=True, text=True, timeout=5)
                if result.returncode == 0 or "Microsoft" in result.stderr:
                    # Extract version from stderr (where MSVC outputs version info)
                    lines = result.stderr.split('\n')
                    for line in lines:
                        if "Microsoft" in line and "Compiler" in line:
                            return line.strip()
            else:
                result = subprocess.run([compiler, "--version"], capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    return result.stdout.strip().split('\n')[0]
        except (subprocess.TimeoutExpired, subprocess.CalledProcessError, FileNotFoundError):
            continue
    
    return f"No compiler found (Platform: {platform.system()} {platform.release()})"

def create_cleaned_log(file_list):
    """Create cleaned.txt log file"""
    # Ensure bin directory exists
    bin_dir = root_dir / "bin"
    bin_dir.mkdir(exist_ok=True)
    
    cleaned_file_path = bin_dir / "cleaned.txt"
    current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    compiler_version = get_compiler_version()
    
    print("Cleaned Files List:")
    for file in file_list:
        print(f"  {file}")

    try:
        with open(cleaned_file_path, "w", encoding="utf-8") as f:
            f.write(f"Last cleaned: {current_time}\n")
            f.write(f"Platform: {platform.system()} {platform.release()}\n")
            f.write(f"Compiler version: {compiler_version}\n")
            f.write(f"Files cleaned: {len(file_list)}\n")
            f.write("\nCleaned files:\n")
            for file in file_list:
                f.write(f"  {file}\n")
        
        print(f"Log written to {cleaned_file_path}")
        
    except OSError as e:
        print(f"Error writing log file: {e}")

if __name__ == "__main__":
    print(f"Root directory: {root_dir}")
    print(f"Platform: {platform.system()}")
    
    object_files = recursive_file_names()
    delete_folder()
    delete_binaries()
    create_cleaned_log(object_files)
    print("Cleaned successfully.")