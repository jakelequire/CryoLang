param(
    [switch]$InstallDeps,
    [switch]$Force,
    [switch]$WhatIf
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# Console Colors
$Colors = @{
    Red    = [ConsoleColor]::Red
    Green  = [ConsoleColor]::Green
    Blue   = [ConsoleColor]::Blue
    Cyan   = [ConsoleColor]::Cyan
    Yellow = [ConsoleColor]::Yellow
    White  = [ConsoleColor]::White
}

function Write-ColorText {
    param([string]$Text, [ConsoleColor]$ForegroundColor = [ConsoleColor]::White)
    $originalColor = $Host.UI.RawUI.ForegroundColor
    $Host.UI.RawUI.ForegroundColor = $ForegroundColor
    Write-Host $Text
    $Host.UI.RawUI.ForegroundColor = $originalColor
}

# ASCII Art Header
Write-ColorText -Text "                  #               " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "                = #^.             " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "      =        ^# # #.            " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "       ## ^##^# ##### #           " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "       # ## # ## ### ## #         " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "        ###^^# #(###=# #(=#       " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "        ## # ## #   # ## #.#<     :::::::::  :::   :::  ::::::::   " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "      ## # ## #       # ## # #.   :+:    :+: :+:   :+: :+:    :+:  " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "   # # #-## #^                    +:+    +:+  +:+ +:+  +:+    +:+  " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "   # # #=}# #<                    +#++:++#:    +#++:   +#+    +:+  " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "      ## # ## #       # ## # #.   +#+    +#+    +#+    +#+    +#+  " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "        ## # ## #   # ## #-#<     #+#    #+#    #+#    #+#    #+#  " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "        ###<=# #(###=# #<^#       ###    ###    ###     ########   " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "       # ## # ## ### ## #         " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "       ## =##(# ##@## # " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "      =        ^# # #.  " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "                =.#<-  " -ForegroundColor $Colors.Cyan
Write-ColorText -Text "                  #   " -ForegroundColor $Colors.Cyan

Write-Host ""
Write-ColorText -Text "                         Cryo Programming Language Installer" -ForegroundColor $Colors.Cyan
Write-Host ""
Write-Host "This script will install the Cryo Programming Language on your system."
Write-Host "It will install/compile the following components:"
Write-Host ""
Write-ColorText -Text "  1. Cryo CLI" -ForegroundColor $Colors.Blue
Write-ColorText -Text "  2. Cryo Compiler" -ForegroundColor $Colors.Blue
Write-ColorText -Text "  3. cryo-path" -ForegroundColor $Colors.Blue
Write-ColorText -Text "  4. LSP Debug Server" -ForegroundColor $Colors.Blue
Write-Host ""
Write-Host "In the installation process, the Cryo Compiler will be built from the source code."
Write-Host "After the compilation, it will also link the Cryo CLI to the global path."
Write-Host ""
Write-Host "This script will also install the following dependencies if they are not already installed:"
Write-Host ""
Write-ColorText -Text "  1. LLVM 20" -ForegroundColor $Colors.Green
Write-ColorText -Text "  2. Clang 20" -ForegroundColor $Colors.Green
Write-ColorText -Text "  3. Make (via Visual Studio Build Tools)" -ForegroundColor $Colors.Green
Write-ColorText -Text "  4. Git" -ForegroundColor $Colors.Green
Write-Host ""

if (-not $WhatIf) {
    $choice = Read-Host "Do you want to continue with the installation? (Y/n)"
    if ($choice -ne "Y" -and $choice -ne "y" -and $choice -ne "") {
        Write-ColorText -Text "Installation cancelled!" -ForegroundColor $Colors.Red
        exit 1
    }
}

function Write-LogInfo { param([string]$Message); Write-ColorText -Text "[INFO] $Message" -ForegroundColor $Colors.Blue }
function Write-LogSuccess { param([string]$Message); Write-ColorText -Text "[SUCCESS] $Message" -ForegroundColor $Colors.Green }
function Write-LogError { param([string]$Message); Write-ColorText -Text "[ERROR] $Message" -ForegroundColor $Colors.Red }
function Write-LogWarning { param([string]$Message); Write-ColorText -Text "[WARNING] $Message" -ForegroundColor $Colors.Yellow }

function Test-Command {
    param([string]$Command)
    try { 
        $null = Get-Command $Command -ErrorAction Stop
        Write-LogSuccess "$Command is installed"
        return $true 
    } catch { 
        Write-LogWarning "$Command is not installed"
        return $false 
    }
}

function Test-VisualStudioBuildTools {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $installations = & $vsWhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
        if ($installations.Count -gt 0) {
            Write-LogSuccess "Visual Studio Build Tools found"
            return $true
        }
    }
    Write-LogWarning "Visual Studio Build Tools not found"
    return $false
}

function Test-Administrator {
    $currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-WithElevation {
    param([string]$Command, [string[]]$Arguments = @())
    
    if (Test-Administrator) {
        & $Command @Arguments
        return $LASTEXITCODE
    }
    
    # Try using Windows sudo first (Windows 11 and newer)
    if (Test-Command "sudo") {
        Write-LogInfo "Using Windows sudo for elevation..."
        if ($Arguments.Count -gt 0) {
            $argString = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
            $result = Start-Process "sudo" -ArgumentList "$Command $argString" -Wait -PassThru
        } else {
            $result = Start-Process "sudo" -ArgumentList $Command -Wait -PassThru
        }
        return $result.ExitCode
    }
    
    # Fallback to UAC prompt
    Write-LogInfo "Requesting administrator privileges..."
    $argumentString = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $result = Start-Process $Command -ArgumentList $argumentString -Verb RunAs -Wait -PassThru
    return $result.ExitCode
}

function Install-Dependencies {
    Write-LogInfo "Installing dependencies..."
    
    if (Test-Command "winget") {
        Write-LogInfo "Using winget to install dependencies..."
        
        if (-not (Test-Command "git")) {
            Write-LogInfo "Installing Git..."
            $exitCode = Invoke-WithElevation "winget" @("install", "Git.Git", "-e", "--silent")
            if ($exitCode -ne 0) { Write-LogError "Failed to install Git"; return $false }
        }
        
        if (-not (Test-Command "clang")) {
            Write-LogInfo "Installing LLVM (includes Clang)..."
            $exitCode = Invoke-WithElevation "winget" @("install", "LLVM.LLVM", "-e", "--silent")
            if ($exitCode -ne 0) { Write-LogError "Failed to install LLVM"; return $false }
            
            # Add LLVM to PATH for current session
            $llvmPath = "${env:ProgramFiles}\LLVM\bin"
            if (Test-Path $llvmPath) {
                $env:PATH = "$llvmPath;$env:PATH"
                Write-LogInfo "Added LLVM to current session PATH"
            }
        }
        
        if (-not (Test-VisualStudioBuildTools)) {
            Write-LogInfo "Installing Visual Studio Build Tools..."
            $exitCode = Invoke-WithElevation "winget" @("install", "Microsoft.VisualStudio.2022.BuildTools", "-e", "--silent")
            if ($exitCode -ne 0) { Write-LogWarning "Failed to install Visual Studio Build Tools" }
        }
    } elseif (Test-Command "choco") {
        Write-LogInfo "Using chocolatey to install dependencies..."
        
        if (-not (Test-Command "git")) {
            $exitCode = Invoke-WithElevation "choco" @("install", "git", "-y")
            if ($exitCode -ne 0) { Write-LogError "Failed to install Git"; return $false }
        }
        
        if (-not (Test-Command "clang")) {
            $exitCode = Invoke-WithElevation "choco" @("install", "llvm", "-y")
            if ($exitCode -ne 0) { Write-LogError "Failed to install LLVM"; return $false }
        }
        
        if (-not (Test-VisualStudioBuildTools)) {
            $exitCode = Invoke-WithElevation "choco" @("install", "visualstudio2022buildtools", "-y")
            if ($exitCode -ne 0) { Write-LogWarning "Failed to install Visual Studio Build Tools" }
        }
    } else {
        Write-LogError "Neither winget nor chocolatey found. Please install dependencies manually."
        return $false
    }
    
    # Refresh environment variables
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
    return $true
}

function Test-Dependencies {
    Write-LogInfo "Verifying dependencies..."
    $missingDeps = @()
    
    if (-not (Test-Command "git")) { $missingDeps += "git" }
    if (-not (Test-Command "clang")) { $missingDeps += "clang/LLVM" }
    if (-not (Test-VisualStudioBuildTools)) { $missingDeps += "Visual Studio Build Tools" }
    if (-not (Test-Command "make") -and -not (Test-Command "nmake")) { $missingDeps += "make/nmake" }
    
    if ($missingDeps.Count -gt 0) {
        Write-LogError "Missing dependencies: $($missingDeps -join ', ')"
        if (-not $Force) {
            Write-LogInfo "Run with -InstallDeps to install dependencies automatically, or -Force to continue anyway"
            return $false
        } else {
            Write-LogWarning "Continuing with missing dependencies due to -Force flag"
        }
    }
    
    Write-LogSuccess "All required dependencies are available"
    return $true
}

function Build-Project {
    Write-LogInfo "Building Cryo Programming Language..."
    
    if ($WhatIf) {
        Write-LogInfo "WhatIf: Would build the project here"
        return $true
    }
    
    try {
        # Set up Visual Studio environment if available
        $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vsWhere) {
            $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vsPath) {
                $vcvarsPath = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
                if (Test-Path $vcvarsPath) {
                    Write-LogInfo "Setting up Visual Studio environment..."
                    cmd /c "`"$vcvarsPath`" && set" | ForEach-Object {
                        if ($_ -match "^(.*?)=(.*)$") {
                            Set-Content "env:\$($matches[1])" $matches[2]
                        }
                    }
                }
            }
        }
        
        Write-LogInfo "Cleaning previous builds..."
        if (Test-Path "bin") { Remove-Item "bin\*" -Force -Recurse -ErrorAction SilentlyContinue }
        try { & make clean 2>$null } catch { }
        
        Write-LogInfo "Building runtime libraries..."
        Set-Location "runtime"
        & make all; if ($LASTEXITCODE -ne 0) { throw "Failed to build runtime libraries" }
        & make install; if ($LASTEXITCODE -ne 0) { throw "Failed to install runtime libraries" }
        Set-Location ".."
        
        Write-LogInfo "Building main compiler..."
        $numCores = if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { 4 }
        & make all -j $numCores; if ($LASTEXITCODE -ne 0) { throw "Failed to build main compiler" }
        
        Write-LogInfo "Building LSP server..."
        & make lsp; if ($LASTEXITCODE -ne 0) { Write-LogWarning "Failed to build LSP server, but continuing..." }
        
        Write-LogSuccess "Build completed successfully!"
        return $true
    } catch {
        Write-LogError "Build failed: $_"
        return $false
    }
}

function Install-Binaries {
    Write-LogInfo "Adding Cryo binaries to system PATH..."
    
    if ($WhatIf) {
        Write-LogInfo "WhatIf: Would add $(Join-Path $PWD 'bin') to system PATH"
        return $true
    }
    
    try {
        # Use the current project's bin directory
        $projectBinDir = Join-Path $PWD "bin"
        
        # Verify that binaries exist
        $cryoExe = Join-Path $projectBinDir "cryo.exe"
        if (-not (Test-Path $cryoExe)) {
            Write-LogError "cryo.exe not found in $projectBinDir"
            return $false
        }
        Write-LogSuccess "Found cryo.exe at $cryoExe"
        
        # Check for LSP server binary
        $lspExe = Join-Path $projectBinDir "cryo-lsp.exe"
        if (Test-Path $lspExe) {
            Write-LogSuccess "Found cryo-lsp.exe at $lspExe"
        } else {
            Write-LogWarning "cryo-lsp.exe not found at $lspExe - LSP server may not be available"
        }
        
        # Check for runtime libraries
        $runtimeFiles = Get-ChildItem "$projectBinDir\*cryoruntime*" -ErrorAction SilentlyContinue
        if ($runtimeFiles) {
            Write-LogSuccess "Found runtime libraries in $projectBinDir"
        }
        
        # Try to add to system PATH first, fall back to user PATH
        $systemInstall = $false
        try {
            # Try system PATH
            $systemPathKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
            $currentSystemPath = (Get-ItemProperty -Path $systemPathKey -Name "Path" -ErrorAction Stop).Path
            
            if ($currentSystemPath -notlike "*$projectBinDir*") {
                $newSystemPath = "$currentSystemPath;$projectBinDir"
                $exitCode = Invoke-WithElevation "reg" @("add", "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment", "/v", "Path", "/t", "REG_EXPAND_SZ", "/d", $newSystemPath, "/f")
                if ($exitCode -eq 0) {
                    Write-LogSuccess "Added $projectBinDir to system PATH"
                    $systemInstall = $true
                } else {
                    throw "Failed to add to system PATH"
                }
            } else {
                Write-LogSuccess "$projectBinDir is already in system PATH"
                $systemInstall = $true
            }
        } catch {
            Write-LogWarning "Could not add to system PATH: $_"
            Write-LogInfo "Falling back to user PATH..."
            
            # Fall back to user PATH
            try {
                $userPathKey = "HKCU:\Environment"
                $currentUserPath = (Get-ItemProperty -Path $userPathKey -Name "Path" -ErrorAction SilentlyContinue).Path
                if (-not $currentUserPath) { $currentUserPath = "" }
                
                if ($currentUserPath -notlike "*$projectBinDir*") {
                    $newUserPath = if ($currentUserPath) { "$currentUserPath;$projectBinDir" } else { $projectBinDir }
                    Set-ItemProperty -Path $userPathKey -Name "Path" -Value $newUserPath
                    Write-LogSuccess "Added $projectBinDir to user PATH"
                } else {
                    Write-LogSuccess "$projectBinDir is already in user PATH"
                }
            } catch {
                Write-LogError "Failed to add to user PATH: $_"
                Write-LogWarning "Please manually add $projectBinDir to your PATH"
                return $false
            }
        }
        
        # Update current session PATH
        $env:Path = "$env:Path;$projectBinDir"
        
        Write-LogSuccess "Cryo binaries are now accessible from: $projectBinDir"
        return $true
    } catch {
        Write-LogError "PATH installation failed: $_"
        return $false
    }
}

function Test-Installation {
    Write-LogInfo "Verifying installation..."
    
    if ($WhatIf) {
        Write-LogInfo "WhatIf: Would verify that $($PWD)\bin is accessible in PATH"
        return $true
    }
    
    # Refresh PATH for current session
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
    
    $projectBinDir = Join-Path $PWD "bin"
    
    # Check if the project bin directory is in PATH
    if ($env:Path -like "*$projectBinDir*") {
        Write-LogSuccess "Project bin directory ($projectBinDir) is in PATH"
    } else {
        Write-LogWarning "Project bin directory may not be properly added to PATH"
    }
    
    if (Test-Command "cryo") {
        try {
            $cryoPath = (Get-Command "cryo").Source
            Write-LogSuccess "cryo is accessible at: $cryoPath"
            
            # Try to get version
            try {
                $version = & cryo --version 2>$null
                if ($version) {
                    Write-LogSuccess "cryo version: $version"
                }
            } catch {
                Write-LogInfo "cryo is accessible but version check failed (this is normal if --version is not implemented)"
            }
        } catch {
            Write-LogSuccess "cryo is installed and accessible"
        }
    } else {
        Write-LogError "cryo is not accessible in PATH"
        Write-LogInfo "Expected location: $(Join-Path $projectBinDir 'cryo.exe')"
        if (Test-Path (Join-Path $projectBinDir "cryo.exe")) {
            Write-LogInfo "Binary exists but PATH may need to be refreshed. Try opening a new terminal."
        }
        return $false
    }
    
    if (Test-Command "cryo-lsp") {
        try {
            $lspPath = (Get-Command "cryo-lsp").Source
            Write-LogSuccess "cryo-lsp is accessible at: $lspPath"
        } catch {
            Write-LogSuccess "cryo-lsp is installed and accessible"
        }
    } else {
        Write-LogWarning "cryo-lsp is not accessible in PATH"
        if (Test-Path (Join-Path $projectBinDir "cryo-lsp.exe")) {
            Write-LogInfo "cryo-lsp.exe exists but may not be in PATH. Try opening a new terminal."
        }
    }
    
    return $true
}

# Main Installation Process
Write-LogInfo "Starting Cryo Programming Language installation..."

try {
    # Step 1: Install dependencies if requested
    if ($InstallDeps) {
        if (-not (Install-Dependencies)) {
            throw "Failed to install dependencies"
        }
    }
    
    # Step 2: Verify dependencies
    if (-not (Test-Dependencies)) {
        throw "Dependencies check failed"
    }
    
    # Step 3: Build the project if needed
    $cryoExe = Join-Path $PWD "bin\cryo.exe"
    if (-not (Test-Path $cryoExe)) {
        Write-LogInfo "Binary not found - building project..."
        if (-not (Build-Project)) {
            throw "Build failed"
        }
    } else {
        Write-LogSuccess "Found existing cryo.exe - skipping build step"
    }
    
    # Step 4: Add to PATH
    if (-not (Install-Binaries)) {
        throw "PATH installation failed"
    }
    
    # Step 5: Verify installation
    if (Test-Installation) {
        Write-Host ""
        Write-ColorText -Text "Installation Complete!" -ForegroundColor $Colors.Green
        Write-Host ""
        Write-Host "The Cryo Programming Language has been successfully installed on your system."
        Write-Host "You can now start using the Cryo CLI to compile and run Cryo programs."
        Write-Host ""
        Write-Host "To get started, you can run the following command:"
        Write-Host ""
        Write-Host "cryo --help"
        Write-Host ""
        Write-Host "This will display the help menu for the Cryo CLI."
        Write-Host ""
        Write-Host "I hope you enjoy using this passion project of mine."
        Write-Host "This is not a full-fledged programming language, but it's a start!"
        Write-Host "You can find documentation and examples on the GitHub repository."
        Write-Host ""
        Write-ColorText -Text "https://github.com/jakelequire/cryo" -ForegroundColor $Colors.Cyan
        Write-Host ""
        Write-Host "Please feel free to reach out to me if you have any questions or feedback!"
        Write-Host ""
        Write-Host "Happy Coding with Cryo! "
        Write-Host ""
    } else {
        throw "Installation verification failed"
    }
} catch {
    Write-LogError "Installation failed: $_"
    Write-Host ""
    Write-Host "Please check the error messages above and try again."
    Write-Host "You can also try running with -InstallDeps to install dependencies automatically."
    exit 1
}

# End of Script
