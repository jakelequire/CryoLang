#!/usr/bin/env python3
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
"""
Build timer script for the Cryo Compiler project.
This script runs the build process and measures the time taken.

Copyright 2025 Jacob LeQuire
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import subprocess
import time
import sys
import os
from datetime import datetime, timedelta


# Define the Cryo logo with proper line breaks
cryo_logo = """
                  #              
                = #^.            
      =        ^# # #.           
       ## ^##^# ##### #          
       # ## # ## ### ## #        
        ###^^# #(###=# #(=#      
        ## # ## #   # ## #.#<     :::::::::  :::   :::  ::::::::  
      ## # ## #       # ## # #.   :+:    :+: :+:   :+: :+:    :+: 
   # # #-## #^                    +:+    +:+  +:+ +:+  +:+    +:+ 
   # # #=}# #<                    +#++:++#:    +#++:   +#+    +:+ 
      ## # ## #       # ## # #.   +#+    +#+    +#+    +#+    +#+ 
        ## # ## #   # ## #-#<     #+#    #+#    #+#    #+#    #+# 
        ###<=# #(###=# #<^#       ###    ###    ###     ########  
       # ## # ## ### ## #        
       ## =##(# ##@## #           _    _           _   _)  |    _    _  
      =        ^# # #.            _|   _ \   ` \   _ \  |  |   -_)   _| 
                =.#<-           \__| \___/ _|_|_|  __/ _| _| \___| _|   
                  #                              |_|                     
"""

def get_compiler_version():
    try:
        # Version path located from this script directory `../version.h`
        version_file_path = os.path.join(os.path.dirname(__file__), "../version.h")
        with open(version_file_path, "r") as version_file:
            for line in version_file:
                if line.startswith("#define _COMPILER_VERSION_"):
                    # Extract the version number from the line
                    version = line.split()[2].strip('"')
                    return version
    except FileNotFoundError:
        return "Unknown version"

def format_time(seconds):
    """Format seconds into a readable time string (HH:MM:SS.mmm)."""
    time_obj = timedelta(seconds=seconds)
    hours, remainder = divmod(time_obj.seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    milliseconds = int(time_obj.microseconds / 1000)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{milliseconds:03d}"


def print_colored(text, color_code):
    """Print text with ANSI color codes if stdout is a terminal."""
    if sys.stdout.isatty():
        print(f"\033[{color_code}m{text}\033[0m")
    else:
        print(f"\033[{color_code}m{text}\033[0m")


def print_logo():
    """Print the Cryo logo with coloring."""
    if sys.stdout.isatty():
        # Use cyan color for the logo with a light blue tint
        print(f"\033[1;36m{cryo_logo}\033[0m")
    else:
        print(f"\033[1;36m{cryo_logo}\033[0m")


def print_header(text):
    """Print a header with nice formatting."""
    print_colored("\n" + "━" * 80, "1;36")
    print_colored(f" {text}", "1;33")
    print_colored("━" * 80, "1;36")


def print_timing(label, seconds, indent=0):
    """Print timing information with nice formatting."""
    spaces = " " * indent
    if sys.stdout.isatty():
        print(f"{spaces}⏱️  {label}: \033[1;33m{format_time(seconds)}\033[0m")
    else:
        print(f"{spaces}⏱️  {label}: {format_time(seconds)}")


def run_command(command, description=None):
    """Run a command and measure its execution time."""
    if description:
        print(f"\n> {description}...")
    
    start_time = time.time()
    
    # Run the command and capture output
    process = subprocess.Popen(
        command,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
        bufsize=1
    )
    
    # Print output in real-time
    output_lines = []
    for line in iter(process.stdout.readline, ''):
        print(line, end='')
        output_lines.append(line)
    
    process.stdout.close()
    return_code = process.wait()
    end_time = time.time()
    elapsed_time = end_time - start_time
    
    if return_code != 0:
        print_colored(f"\n❌ Command failed with exit code {return_code}", "1;31")
        return elapsed_time, False
    
    return elapsed_time, True


def get_compiler_info():
    """Get compiler version information."""
    info = {}
    try:
        # Try to get C compiler version
        c_compiler_cmd = "gcc --version" if os.name == "nt" else "clang-18 --version"
        result = subprocess.run(c_compiler_cmd, shell=True, capture_output=True, text=True)
        if result.returncode == 0:
            info["c_compiler"] = result.stdout.splitlines()[0]
    except Exception:
        info["c_compiler"] = "Unknown"
    
    try:
        # Try to get C++ compiler version
        cpp_compiler_cmd = "g++ --version" if os.name == "nt" else "clang++-18 --version"
        result = subprocess.run(cpp_compiler_cmd, shell=True, capture_output=True, text=True)
        if result.returncode == 0:
            info["cpp_compiler"] = result.stdout.splitlines()[0]
    except Exception:
        info["cpp_compiler"] = "Unknown"
    
    return info


def print_centered_text(text, color_code="1;33"):
    """Print text centered within an 80-character width."""
    width = 80
    padded_text = text.center(width)
    print_colored(padded_text, color_code)


def main():
    parser = argparse.ArgumentParser(description="Cryo Compiler Build Timer")
    parser.add_argument("--clean", action="store_true", help="Clean before building")
    parser.add_argument("--no-tests", action="store_true", help="Skip building tests")
    parser.add_argument("--verbose", action="store_true", help="Show more detailed output")
    args = parser.parse_args()

    # Print Cryo logo and title
    print()  # Add some space at the top
    print_colored("━" * 80, "1;36")
    print_logo()
    print_centered_text("CRYO COMPILER: v" + get_compiler_version())
    
    # Starting banner
    build_date = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print_header(f"BUILD STARTED - {build_date}")
    
    if args.verbose:
        compiler_info = get_compiler_info()
        if "c_compiler" in compiler_info:
            print(f"C Compiler: {compiler_info['c_compiler']}")
        if "cpp_compiler" in compiler_info:
            print(f"C++ Compiler: {compiler_info['cpp_compiler']}")
    
    # Variable to track overall success
    all_success = True
    total_start_time = time.time()
    
    # Clean if requested
    if args.clean:
        clean_time, clean_success = run_command("make clean", "Cleaning build files")
        print_timing("Clean completed in", clean_time)
        all_success = all_success and clean_success
    
    # Build the compiler
    build_time, build_success = run_command("make -j build", "Building compiler")
    print_timing("Compiler build completed in", build_time)
    all_success = all_success and build_success
    
    test_time = 0
    # Build tests unless skipped
    if not args.no_tests and build_success:
        test_time, test_success = run_command("make -j tests", "Building test suite")
        print_timing("Test suite build completed in", test_time)
        all_success = all_success and test_success
    
    # Calculate total time
    total_time = time.time() - total_start_time
    
    # Print summary
    print_header("BUILD SUMMARY")
    
    if args.clean:
        print_timing("Clean time", clean_time, indent=2)
    print_timing("Compiler build time", build_time, indent=2)
    if not args.no_tests:
        print_timing("Test suite build time", test_time, indent=2)
    
    print_colored("\n" + "━" * 80, "1;36")
    print_timing("TOTAL BUILD TIME", total_time)
    
    if all_success:
        print_colored("\n✓ BUILD COMPLETED SUCCESSFULLY", "1;32")
    else:
        print_colored("\n❌ BUILD FAILED", "1;31")
    
    print_colored("━" * 80, "1;36")
    print("\n\n")

    return 0 if all_success else 1


if __name__ == "__main__":
    sys.exit(main())