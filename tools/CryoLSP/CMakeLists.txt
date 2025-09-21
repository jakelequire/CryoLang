cmake_minimum_required(VERSION 3.20)
project(CryoLSP VERSION 0.1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM (optional for now)
find_package(LLVM CONFIG)
if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    
    # Add LLVM definitions
    add_definitions(${LLVM_DEFINITIONS})
else()
    message(WARNING "LLVM not found - building without LLVM support")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include  # Main Cryo compiler headers
)

if(LLVM_FOUND)
    include_directories(${LLVM_INCLUDE_DIRS})
endif()

# Compiler flags
if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_CRT_SECURE_NO_WARNINGS -w -g -Og")
    if(LLVM_FOUND)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXXFLAGS}")
    endif()
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -g -Og -fexceptions")
    if(LLVM_FOUND)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXXFLAGS}")
    endif()
endif()

# Source files
set(LSP_SOURCES
    src/main.cpp
    src/LSPServer.cpp
    src/MessageHandler.cpp
)

# Main Cryo compiler source files (reuse existing components)
set(CRYO_SOURCES
    ../../src/AST/ASTBuilder.cpp
    ../../src/AST/ASTContext.cpp
    ../../src/AST/ASTDumper.cpp
    ../../src/AST/ASTNode.cpp
    ../../src/AST/SymbolTable.cpp
    ../../src/AST/Type.cpp
    ../../src/AST/TypeChecker.cpp
    ../../src/Compiler/CompilerInstance.cpp
    ../../src/GDM/GDM.cpp
    ../../src/Lexer/lexer.cpp
    ../../src/Parser/Parser.cpp
    ../../src/Utils/file.cpp
)

# Create the LSP executable
add_executable(cryo-lsp ${LSP_SOURCES} ${CRYO_SOURCES})

# Link libraries
if(LLVM_FOUND)
    target_link_libraries(cryo-lsp ${LLVM_LIBRARIES})
endif()

if(WIN32)
    # Windows-specific libraries
    target_link_libraries(cryo-lsp)
else()
    # Linux-specific libraries
    target_link_libraries(cryo-lsp pthread)
endif()

# Set output directory
set_target_properties(cryo-lsp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin
)

# Installation (optional)
install(TARGETS cryo-lsp 
    RUNTIME DESTINATION bin
)
