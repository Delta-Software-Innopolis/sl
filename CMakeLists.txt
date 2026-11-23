cmake_minimum_required(VERSION 3.20)

project(sl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVMConfig.cmake: ${LLVM_DIR}")

find_program(SL_CLANG NAMES clang HINTS "${LLVM_TOOLS_BINARY_DIR}" REQUIRED)
add_library(sl_runtime STATIC src/runtime/sl_runtime.c)

add_executable(sl
    src/main.c

    src/codegen/codegen.h
    src/codegen/codegen_internal.h
    src/codegen/emit.c
    src/codegen/codegen.c
    src/codegen/expr.c
    src/codegen/decl.c
    src/codegen/stmt.c
    src/codegen/types.c

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

add_dependencies(sl sl_runtime)
target_compile_definitions(sl PRIVATE
    SL_CLANG_PATH="${SL_CLANG}"
    SL_RUNTIME_PATH="$<TARGET_FILE:sl_runtime>"
)
if(APPLE AND CMAKE_OSX_SYSROOT)
    target_compile_definitions(sl PRIVATE SL_SYSROOT="${CMAKE_OSX_SYSROOT}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME native_factorial COMMAND "${CMAKE_COMMAND}"
        "-DSL=$<TARGET_FILE:sl>"
        "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/06_factorial.sl"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/test-native"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_factorial.cmake")
endif()
