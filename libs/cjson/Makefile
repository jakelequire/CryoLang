# CJson Library Makefile

# OS Detection
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
    RM := del /Q
    MKDIR := mkdir
    PATHSEP := \\
else
    DETECTED_OS := $(shell uname -s)
    RM := rm -f
    MKDIR := mkdir -p
    PATHSEP := /
endif

# Compiler settings
CXX = clang++
ifeq ($(OS),Windows_NT)
    CXXFLAGS = -std=c++23 -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -DWIN32_LEAN_AND_MEAN -w -g -O2 -fexceptions -D_UCRT
    DEBUG_CXXFLAGS = -std=c++23 -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -DWIN32_LEAN_AND_MEAN -w -g -O0 -DDEBUG -fexceptions -D_UCRT
else
    CXXFLAGS = -std=c++23 -Wall -Wextra -O2 -fPIC
    DEBUG_CXXFLAGS = -std=c++23 -Wall -Wextra -g -O0 -DDEBUG -fPIC
endif

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests
EXAMPLE_DIR = examples
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/.o
INSTALL_PREFIX = /usr/local

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Library name
LIBRARY_NAME = libcjson.a
SHARED_LIBRARY_NAME = libcjson.so

# Test executable
TEST_EXECUTABLE = $(BUILD_DIR)/test_cjson

# Example executables
EXAMPLE_SOURCES = $(wildcard $(EXAMPLE_DIR)/*.cpp)
EXAMPLE_EXECUTABLES = $(EXAMPLE_SOURCES:$(EXAMPLE_DIR)/%.cpp=$(BUILD_DIR)/%)

# Default target
all: static shared examples

# Create build directories
$(BUILD_DIR):
	$(MKDIR) $(BUILD_DIR)

$(OBJ_DIR): | $(BUILD_DIR)
ifeq ($(OS),Windows_NT)
	if not exist "$(OBJ_DIR)" $(MKDIR) "$(OBJ_DIR)"
else
	$(MKDIR) $(OBJ_DIR)
endif

# Compile object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

# Build static library
static: $(BUILD_DIR)/$(LIBRARY_NAME)

$(BUILD_DIR)/$(LIBRARY_NAME): $(OBJECTS)
	ar rcs $@ $^

# Build shared library
shared: $(BUILD_DIR)/$(SHARED_LIBRARY_NAME)

$(BUILD_DIR)/$(SHARED_LIBRARY_NAME): $(OBJECTS)
	$(CXX) -shared -o $@ $^

# Build tests
test: $(TEST_EXECUTABLE)

$(TEST_EXECUTABLE): $(TEST_DIR)/test_main.cpp $(BUILD_DIR)/$(LIBRARY_NAME)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $< -L$(BUILD_DIR) -lcjson -o $@

# Run tests
run-tests: $(TEST_EXECUTABLE)
	./$(TEST_EXECUTABLE)

# Build examples
examples: $(EXAMPLE_EXECUTABLES)

$(BUILD_DIR)/%: $(EXAMPLE_DIR)/%.cpp $(BUILD_DIR)/$(LIBRARY_NAME)
ifeq ($(OS),Windows_NT)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $< $(BUILD_DIR)/$(LIBRARY_NAME) -static -lstdc++ -o $@
else
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $< -L$(BUILD_DIR) -lcjson -o $@
endif

# Run examples
run-examples: examples
	@echo "Running basic_usage example:"
	@./$(BUILD_DIR)/basic_usage
	@echo -e "\n" 
	@echo "Running advanced_features example:"
	@./$(BUILD_DIR)/advanced_features

# Debug build
debug: CXXFLAGS = $(DEBUG_CXXFLAGS)
debug: clean static test

# Install library
install: static shared
	install -d $(INSTALL_PREFIX)/lib
	install -d $(INSTALL_PREFIX)/include/cjson
	install -m 644 $(BUILD_DIR)/$(LIBRARY_NAME) $(INSTALL_PREFIX)/lib/
	install -m 755 $(BUILD_DIR)/$(SHARED_LIBRARY_NAME) $(INSTALL_PREFIX)/lib/
	install -m 644 $(INCLUDE_DIR)/cjson/*.hpp $(INSTALL_PREFIX)/include/cjson/
	ldconfig

# Uninstall library
uninstall:
	rm -f $(INSTALL_PREFIX)/lib/$(LIBRARY_NAME)
	rm -f $(INSTALL_PREFIX)/lib/$(SHARED_LIBRARY_NAME)
	rm -rf $(INSTALL_PREFIX)/include/cjson
	ldconfig

# Clean build files
clean:
ifeq ($(OS),Windows_NT)
	if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
else
	rm -rf $(BUILD_DIR)
endif

# Create distribution package
dist: clean
	tar -czf cjson-1.0.tar.gz --exclude=.git --exclude=*.tar.gz .

# Format code (requires clang-format)
format:
	find $(SRC_DIR) $(INCLUDE_DIR) $(TEST_DIR) $(EXAMPLE_DIR) -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i

# Static analysis (requires cppcheck)
analyze:
	cppcheck --enable=all --std=c++17 $(SRC_DIR) $(INCLUDE_DIR)

# Generate documentation (requires doxygen)
docs:
	doxygen Doxyfile

# Benchmark (requires benchmark executable)
benchmark: static
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) benchmark/benchmark.cpp -L$(BUILD_DIR) -lcjson -o $(BUILD_DIR)/benchmark
	./$(BUILD_DIR)/benchmark

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build static and shared libraries, examples"
	@echo "  static       - Build static library"
	@echo "  shared       - Build shared library"
	@echo "  test         - Build test executable"
	@echo "  run-tests    - Build and run tests"
	@echo "  examples     - Build example programs"
	@echo "  run-examples - Build and run examples"
	@echo "  debug        - Build with debug flags"
	@echo "  install      - Install library to system"
	@echo "  uninstall    - Remove library from system"
	@echo "  clean        - Remove build files"
	@echo "  dist         - Create distribution package"
	@echo "  format       - Format source code"
	@echo "  analyze      - Run static analysis"
	@echo "  docs         - Generate documentation"
	@echo "  benchmark    - Build and run benchmarks"
	@echo "  help         - Show this help message"

# Declare phony targets
.PHONY: all static shared test run-tests examples run-examples debug install uninstall clean dist format analyze docs benchmark help

# Dependencies
-include $(OBJECTS:.o=.d)

$(OBJ_DIR)/%.d: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -MM -MT $(OBJ_DIR)/$*.o $< > $@