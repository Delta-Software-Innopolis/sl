#ifndef SL_CODEGEN_INTERNAL_H
#define SL_CODEGEN_INTERNAL_H

#include <llvm-c/Core.h>

#include "codegen.h"

typedef struct SLRecordTypeEntry SLRecordTypeEntry;

typedef struct {
    const SLSymbol* symbol;
    LLVMValueRef value;
} SLCodegenBinding;

/* One zero-initialized context per module. Semantic pointers are borrowed. */
typedef struct {
    LLVMContextRef llvm;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMValueRef current_function;
    const SLSemType* return_type;

    SLCodegenBinding* bindings;
    size_t binding_count;
    size_t binding_capacity;
    SLRecordTypeEntry* record_types;
    SLCodegenError error;
    const SLCodegenOptions* options;
} SLCodegenContext;

LLVMTypeRef SLCodegenType(SLCodegenContext* ctx, const SLSemType* type,
                          SLSpan span);
/* value is an SL rvalue in ctx->llvm. For casts, position the builder first. */
LLVMValueRef SLCodegenConvert(SLCodegenContext* ctx, LLVMValueRef value,
                              const SLSemType* target, SLSpan span);
/* Call at module teardown. LLVM owns the types until context disposal. */
void SLFreeCodegenTypes(SLCodegenContext* ctx);

bool SLCGFail(SLCodegenContext* ctx, SLSpan span, const char* message);
bool SLCGEmit(SLCodegenContext* ctx, const SLCodegenOptions* options);
bool SLCGBind(SLCodegenContext* ctx, const SLSymbol* symbol,
              LLVMValueRef value);
LLVMValueRef SLCGLookup(SLCodegenContext* ctx, const SLSymbol* symbol,
                        SLSpan span);
LLVMValueRef SLCGSlot(SLCodegenContext* ctx, LLVMTypeRef type);
LLVMBasicBlockRef SLCGBlock(SLCodegenContext* ctx, const char* name);
bool SLCGDeclareProgram(SLCodegenContext* ctx, const SLProgram* program,
                        const SLSemanticState* semantics);
bool SLCGLocal(SLCodegenContext* ctx, const SLDecl* decl);
LLVMValueRef SLCGExpr(SLCodegenContext* ctx, const SLExpr* expr);
LLVMValueRef SLCGConvertedExpr(SLCodegenContext* ctx, const SLExpr* expr,
                               const SLSemType* target);
LLVMValueRef SLCGAddress(SLCodegenContext* ctx, const SLExpr* expr);
LLVMValueRef SLCGElement(SLCodegenContext* ctx, LLVMTypeRef array,
                         LLVMValueRef address, LLVMValueRef index);
bool SLCGBody(SLCodegenContext* ctx, const SLBody* body);
bool SLCGReturn(SLCodegenContext* ctx, const SLExpr* expr);
bool SLCGByReference(const SLSemType* type);

#endif
