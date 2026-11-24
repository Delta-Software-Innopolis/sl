#ifndef SL_CODEGEN_H
#define SL_CODEGEN_H

#include "../semantics/semantics.h"

typedef enum {
    SL_OUTPUT_EXECUTABLE,
    SL_OUTPUT_OBJECT,
    SL_OUTPUT_LLVM_IR
} SLCodegenOutput;

typedef struct {
    SLCodegenOutput output;
    const char* output_path;
    const char* entry;
    const char** args;
    size_t arg_count;
} SLCodegenOptions;

typedef struct {
    bool has_error;
    SLSpan span;
    char message[256];
} SLCodegenError;

bool SLGenerateCode(const SLProgram* program, const SLSemanticState* semantics,
                    const SLCodegenOptions* options, SLCodegenError* error);

#endif
