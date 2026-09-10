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

    src/lexer/lexer.h
    src/lexer/lexer.c
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


llvm_map_components_to_libnames(LLVM_LIBS
    core
    support
)

target_link_libraries(sl PRIVATE
    ${LLVM_LIBS}
)


set_target_properties(sl PROPERTIES
    LINKER_LANGUAGE CXX
)