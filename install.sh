#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e
# Set the IFS to only split on newlines and tabs
IFS=$'\n\t'
# Set the shell options
shopt -s nullglob
# Set the trap to cleanup on termination
trap EXIT

# Console Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
TEAL='\033[0;36m'
PURPLE='\033[0;35m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
GREY='\033[0;37m'
BOLD='\033[1m'
ITALIC='\033[3m'
UNDERLINE='\033[4m'
COLOR_RESET='\033[0m'
NEW_LINE=$'\n'

echo -e "$TEAL"
echo -e "                  #               "
echo -e "                = #^.             "
echo -e "      =        ^# # #.            "
echo -e "       ## ^##^# ##### #           "
echo -e "       # ## # ## ### ## #         "
echo -e "        ###^^# #(###=# #(=#       "
echo -e "        ## # ## #   # ## #.#<     :::::::::  :::   :::  ::::::::   "
echo -e "      ## # ## #       # ## # #.   :+:    :+: :+:   :+: :+:    :+:  "
echo -e "   # # #-## #^                    +:+    +:+  +:+ +:+  +:+    +:+  "
echo -e "   # # #=}# #<                    +#++:++#:    +#++:   +#+    +:+  "
echo -e "      ## # ## #       # ## # #.   +#+    +#+    +#+    +#+    +#+  "
echo -e "        ## # ## #   # ## #-#<     #+#    #+#    #+#    #+#    #+#  "
echo -e "        ###<=# #(###=# #<^#       ###    ###    ###     ########   "
echo -e "       # ## # ## ### ## #         "
echo -e "       ## =##(# ##@## # "
echo -e "      =        ^# # #.  "
echo -e "                =.#<-  "
echo -e "                  #   "
echo -e "$COLOR_RESET"
echo -e "$TEAL$BOLD                         Cryo Programming Language Installer $COLOR_RESET"
echo " "
echo "This script will install the Cryo Programming Language on your system."
echo "It will install/compile the following components:"
echo " "
echo -e "$BLUE$BOLD  1. Cryo CLI$COLOR_RESET"
echo -e "$BLUE$BOLD  2. Cryo Compiler$COLOR_RESET"
echo -e "$BLUE$BOLD  3. cryo-path$COLOR_RESET"
echo -e "$BLUE$BOLD  4. LSP Debug Server$COLOR_RESET"
echo " "
echo "In the installation process, the Cryo Compiler will be built from the source code."
echo "After the compilation, it will also link the Cryo CLI to the global path."
echo " "
echo "This script will also install the following dependencies if they are not already installed:"
echo " "
echo -e "$GREEN$BOLD  1. LLVM 20$COLOR_RESET"
echo -e "$GREEN$BOLD  2. Clang 20$COLOR_RESET"
echo -e "$GREEN$BOLD  3. Make$COLOR_RESET"
echo " "
echo " "
# Get confirmation from the user
read -p "Do you want to continue with the installation? (Y/n): " choice
if [ "$choice" != "Y" ] && [ "$choice" != "y" ]; then
    echo -e "$RED $BOLD Installation cancelled! $COLOR_RESET"
    exit 1
fi

# ================================================================================
# Helper Functions
# ================================================================================

log_info() {
    echo -e "$BLUE$BOLD[INFO]$COLOR_RESET $1"
}

log_success() {
    echo -e "$GREEN$BOLD[SUCCESS]$COLOR_RESET $1"
}

log_error() {
    echo -e "$RED$BOLD[ERROR]$COLOR_RESET $1"
}

log_warning() {
    echo -e "$YELLOW$BOLD[WARNING]$COLOR_RESET $1"
}

check_command() {
    if command -v "$1" &> /dev/null; then
        log_success "$1 is installed"
        return 0
    else
        log_warning "$1 is not installed"
        return 1
    fi
}

# Detect the OS
detect_os() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        OS="linux"
        # Detect Linux distribution
        if [ -f /etc/os-release ]; then
            . /etc/os-release
            DISTRO=$ID
        else
            DISTRO="unknown"
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
        DISTRO="macos"
    else
        log_error "Unsupported operating system: $OSTYPE"
        exit 1
    fi
    log_info "Detected OS: $OS ($DISTRO)"
}

# Install dependencies based on OS
install_dependencies() {
    log_info "Installing dependencies..."
    
    case $DISTRO in
        "ubuntu"|"debian")
            log_info "Installing dependencies for Ubuntu/Debian..."
            sudo apt-get update
            sudo apt-get install -y build-essential cmake git curl wget
            
            # Install LLVM 20
            if ! check_command "clang-20"; then
                log_info "Installing LLVM 20..."
                wget -O - https://apt.llvm.org/llvm.sh | sudo bash -s -- 20
                sudo apt-get install -y clang-20 clang++-20 lldb-20 lld-20
            fi
            ;;
        "fedora"|"rhel"|"centos")
            log_info "Installing dependencies for Red Hat based systems..."
            sudo dnf install -y gcc gcc-c++ make cmake git curl wget
            
            # Install LLVM 20
            if ! check_command "clang-20"; then
                log_info "Installing LLVM 20..."
                sudo dnf install -y clang llvm-devel
            fi
            ;;
        "arch")
            log_info "Installing dependencies for Arch Linux..."
            sudo pacman -Syu --noconfirm
            sudo pacman -S --noconfirm base-devel cmake git curl wget clang llvm
            ;;
        "macos")
            log_info "Installing dependencies for macOS..."
            if ! command -v brew &> /dev/null; then
                log_info "Installing Homebrew..."
                /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
            fi
            
            brew update
            brew install cmake git curl wget
            
            # Install LLVM
            if ! check_command "clang"; then
                log_info "Installing LLVM..."
                brew install llvm
                # Add LLVM to PATH
                echo 'export PATH="/opt/homebrew/opt/llvm/bin:$PATH"' >> ~/.bashrc
                echo 'export PATH="/opt/homebrew/opt/llvm/bin:$PATH"' >> ~/.zshrc
                export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
            fi
            ;;
        *)
            log_warning "Unknown distribution. Please install build-essential, cmake, git, clang, and llvm manually."
            ;;
    esac
}

# Verify dependencies
verify_dependencies() {
    log_info "Verifying dependencies..."
    
    local missing_deps=()
    
    if ! check_command "make"; then
        missing_deps+=("make")
    fi
    
    if ! check_command "cmake"; then
        missing_deps+=("cmake")
    fi
    
    if ! check_command "git"; then
        missing_deps+=("git")
    fi
    
    # Check for clang (with various possible names)
    if ! check_command "clang-20" && ! check_command "clang"; then
        missing_deps+=("clang")
    fi
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        log_info "Please install the missing dependencies manually or run with --install-deps"
        exit 1
    fi
    
    log_success "All dependencies are installed"
}

# Build the project
build_project() {
    log_info "Building Cryo Programming Language..."
    
    # Clean previous builds
    log_info "Cleaning previous builds..."
    make clean || true
    
    # Build the runtime libraries first
    log_info "Building runtime libraries..."
    make -C runtime all
    make -C runtime install
    
    # Build main compiler
    log_info "Building main compiler..."
    make all -j$(nproc)
    
    # Build LSP server
    log_info "Building LSP server..."
    make lsp
    
    log_success "Build completed successfully!"
}

# Add binaries to PATH
install_binaries() {
    log_info "Adding Cryo binaries to system PATH..."
    
    # Use the current project's bin directory
    local project_bin_dir="$(pwd)/bin"
    
    # Verify that binaries exist
    if [ ! -f "$project_bin_dir/cryo" ]; then
        log_error "cryo binary not found in $project_bin_dir"
        return 1
    fi
    log_success "Found cryo at $project_bin_dir/cryo"
    
    # Check for LSP server binary
    if [ -f "$project_bin_dir/cryo-lsp" ]; then
        log_success "Found cryo-lsp at $project_bin_dir/cryo-lsp"
    else
        log_warning "cryo-lsp not found at $project_bin_dir/cryo-lsp - LSP server may not be available"
    fi
    
    # Check for runtime libraries
    if [ -f "$project_bin_dir/libcryoruntime.so" ] || [ -f "$project_bin_dir/libcryoruntime.a" ]; then
        log_success "Found runtime libraries in $project_bin_dir"
    fi
    
    # Make binaries executable
    chmod +x "$project_bin_dir/cryo" 2>/dev/null || true
    chmod +x "$project_bin_dir/cryo-lsp" 2>/dev/null || true
    
    # Add to shell configuration files
    local shell_configs=("$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile")
    local path_export_line="export PATH=\"$project_bin_dir:\$PATH\""
    local added_to_config=false
    
    for config_file in "${shell_configs[@]}"; do
        if [ -f "$config_file" ] || [ "$config_file" == "$HOME/.bashrc" ]; then
            # Check if already added
            if [ -f "$config_file" ] && grep -q "$project_bin_dir" "$config_file"; then
                log_success "$project_bin_dir already in $config_file"
            else
                # Add to shell config
                echo "" >> "$config_file"
                echo "# Added by Cryo Programming Language installer" >> "$config_file"
                echo "$path_export_line" >> "$config_file"
                log_success "Added $project_bin_dir to $config_file"
                added_to_config=true
            fi
        fi
    done
    
    if [ "$added_to_config" = false ]; then
        log_warning "Could not find shell configuration files. Please manually add to PATH:"
        log_info "$path_export_line"
    fi
    
    # Update current session PATH
    export PATH="$project_bin_dir:$PATH"
    
    log_success "Cryo binaries are now accessible from: $project_bin_dir"
    
    return 0
}

# Verify installation
verify_installation() {
    log_info "Verifying installation..."
    
    local project_bin_dir="$(pwd)/bin"
    
    # Check if the project bin directory is in PATH
    if [[ ":$PATH:" == *":$project_bin_dir:"* ]]; then
        log_success "Project bin directory ($project_bin_dir) is in PATH"
    else
        log_warning "Project bin directory may not be properly added to PATH"
    fi
    
    if command -v cryo &> /dev/null; then
        local cryo_path=$(command -v cryo)
        log_success "cryo is accessible at: $cryo_path"
        
        # Try to get version
        local version=$(cryo --version 2>/dev/null || echo "")
        if [ -n "$version" ]; then
            log_success "cryo version: $version"
        else
            log_info "cryo is accessible but version check failed (this is normal if --version is not implemented)"
        fi
    else
        log_error "cryo is not accessible in PATH"
        log_info "Expected location: $project_bin_dir/cryo"
        if [ -f "$project_bin_dir/cryo" ]; then
            log_info "Binary exists but PATH may need to be refreshed. Try opening a new terminal or run:"
            log_info "source ~/.bashrc"
        fi
        return 1
    fi
    
    if command -v cryo-lsp &> /dev/null; then
        local lsp_path=$(command -v cryo-lsp)
        log_success "cryo-lsp is accessible at: $lsp_path"
    else
        log_warning "cryo-lsp is not accessible in PATH"
        if [ -f "$project_bin_dir/cryo-lsp" ]; then
            log_info "cryo-lsp binary exists but may not be in PATH. Try opening a new terminal or run:"
            log_info "source ~/.bashrc"
        fi
    fi
    
    return 0
}

# ================================================================================
# Main Installation Process
# ================================================================================

log_info "Starting Cryo Programming Language installation..."

# Parse command line arguments
INSTALL_DEPS=false
for arg in "$@"; do
    case $arg in
        --install-deps)
            INSTALL_DEPS=true
            shift
            ;;
        --help)
            echo "Cryo Programming Language Installer"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --install-deps    Install system dependencies automatically"
            echo "  --help           Show this help message"
            exit 0
            ;;
    esac
done

# Step 1: Detect operating system
detect_os

# Step 2: Install dependencies if requested
if [ "$INSTALL_DEPS" = true ]; then
    install_dependencies
fi

# Step 3: Verify dependencies
verify_dependencies

# Step 4: Build the project if needed
if [ ! -f "bin/cryo" ]; then
    log_info "Binary not found - building project..."
    build_project
else
    log_success "Found existing cryo binary - skipping build step"
fi

# Step 5: Add to PATH
install_binaries

# Step 6: Verify installation
if verify_installation; then
    echo " "
    echo -e "$GREEN$BOLD Installation Complete! $COLOR_RESET"
    echo " "
    echo "The Cryo Programming Language has been successfully installed on your system."
    echo "You can now start using the Cryo CLI to compile and run Cryo programs."
    echo " "
    echo "To get started, you can run the following command:"
    echo " "
    echo "cryo --help"
    echo " "
    echo "This will display the help menu for the Cryo CLI."
    echo " "
    echo "I hope you enjoy using this passion project of mine."
    echo "This is not a full-fledged programming language, but it's a start!"
    echo "You can find documentation and examples on the GitHub repository."
    echo " "
    echo -e "$TEAL$BOLD https://github.com/jakelequire/cryo $COLOR_RESET"
    echo " "
    echo "Please feel free to reach out to me if you have any questions or feedback!"
    echo " "
    echo "Happy Coding with Cryo! ❄️"
    echo " "
else
    log_error "Installation verification failed. Please check the installation manually."
    exit 1
fi

# ================================================================================
# End of Script
