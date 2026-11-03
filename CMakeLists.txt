cmake_minimum_required(VERSION 3.20)

project(sl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVMConfig.cmake: ${LLVM_DIR}")

add_executable(sl
    src/main.c

    src/compiler_state/compiler_state.h
    src/compiler_state/compiler_state.c

    src/token/token.h
    src/token/token.c

    src/parser/parser.h
    src/parser/parser.c

    src/lexer/lexer.h
    src/lexer/lexer.c

    src/semantics/symbol_table.h
    src/semantics/symbol_table.c

    src/semantics/checks.c
    src/semantics/consteval.c
    src/semantics/consteval.h
    src/semantics/optimize.c
    src/semantics/optimize.h
    src/semantics/semantics.c
    src/semantics/semantics.h
    src/semantics/symbol_table.c
    src/semantics/symbol_table.h
)


target_include_directories(sl SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
)

separate_arguments(
    LLVM_DEFINITIONS_LIST
    NATIVE_COMMAND
    "${LLVM_DEFINITIONS}"
)

target_compile_options(sl PRIVATE
    ${LLVM_DEFINITIONS_LIST}
)

target_link_libraries(sl PRIVATE
    LLVM
)


set_target_properties(sl PROPERTIES
    LINKER_LANGUAGE CXX
)
