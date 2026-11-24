#include <llvm-c/Analysis.h>

#include "codegen_internal.h"

bool SLCGFail(SLCodegenContext* ctx, SLSpan span, const char* message) {
    if (!ctx->error.has_error) {
        ctx->error.has_error = true;
        ctx->error.span = span;
        snprintf(ctx->error.message, sizeof(ctx->error.message), "%s", message);
    }

    return false;
}

bool SLCGBind(SLCodegenContext* ctx, const SLSymbol* symbol,
              LLVMValueRef value) {
    if (ctx->binding_count == ctx->binding_capacity) {
        size_t capacity =
            ctx->binding_capacity ? 2 * ctx->binding_capacity : 16;

        SLCodegenBinding* bindings =
            realloc(ctx->bindings, capacity * sizeof(*bindings));

        if (!bindings)
            return SLCGFail(ctx, symbol->span, "out of memory binding symbol");

        ctx->bindings = bindings;
        ctx->binding_capacity = capacity;
    }

    ctx->bindings[ctx->binding_count++] = (SLCodegenBinding){symbol, value};

    return true;
}

LLVMValueRef SLCGLookup(SLCodegenContext* ctx, const SLSymbol* symbol,
                        SLSpan span) {
    for (size_t i = ctx->binding_count; i > 0; --i) {
        if (ctx->bindings[i - 1].symbol == symbol)
            return ctx->bindings[i - 1].value;
    }
    SLCGFail(ctx, span, "symbol has no LLVM binding");
    return NULL;
}

LLVMBasicBlockRef SLCGBlock(SLCodegenContext* ctx, const char* name) {
    return LLVMAppendBasicBlockInContext(ctx->llvm, ctx->current_function,
                                         name);
}

/* A declaration inside a loop reuses its stack slot on each iteration. */
LLVMValueRef SLCGSlot(SLCodegenContext* ctx, LLVMTypeRef type) {
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx->llvm);
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_function);
    LLVMValueRef first = LLVMGetFirstInstruction(entry);

    if (first)
        LLVMPositionBuilderBefore(builder, first);
    else
        LLVMPositionBuilderAtEnd(builder, entry);

    LLVMValueRef slot = LLVMBuildAlloca(builder, type, "slot");
    LLVMDisposeBuilder(builder);

    return slot;
}

bool SLGenerateCode(const SLProgram* program, const SLSemanticState* semantics,
                    const SLCodegenOptions* options, SLCodegenError* error) {
    SLCodegenContext ctx = {0};
    ctx.options = options;

    char* message = NULL;

    if (!program || !semantics || !semantics->global_scope ||
        semantics->error.has_error || !options || !options->output_path ||
        !*options->output_path) {
        SLCGFail(&ctx, (SLSpan){0},
                 "expected an analyzed program and output path");
    } else {
        ctx.llvm = LLVMContextCreate();
        ctx.module = LLVMModuleCreateWithNameInContext("sl", ctx.llvm);
        ctx.builder = LLVMCreateBuilderInContext(ctx.llvm);

        if (SLCGDeclareProgram(&ctx, program, semantics)) {
            if (options->output == SL_OUTPUT_EXECUTABLE && !options->entry &&
                !LLVMGetNamedFunction(ctx.module, "main")) {

                SLSymbol* main_symbol = SLSymbolTableFind(
                    &semantics->global_scope->symbols, (SLSlice){"main", 4});

                SLCGFail(
                    &ctx, main_symbol ? main_symbol->span : (SLSpan){0},
                    "executable requires routine main() with no parameters "
                    "and a void or integer result");
            } else if (LLVMVerifyModule(ctx.module, LLVMReturnStatusAction,
                                        &message)) {
                SLCGFail(&ctx, (SLSpan){0}, message);
            } else {
                SLCGEmit(&ctx, options);
            }
        }

        LLVMDisposeMessage(message);
        free(ctx.bindings);

        SLFreeCodegenTypes(&ctx);
        LLVMDisposeBuilder(ctx.builder);
        LLVMDisposeModule(ctx.module);

        LLVMContextDispose(ctx.llvm);
    }

    if (error) *error = ctx.error;
    return !ctx.error.has_error;
}
