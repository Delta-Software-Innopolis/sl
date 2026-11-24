#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include "codegen_internal.h"

bool SLCGByReference(const SLSemType* type) {
    return type->kind == SL_SEM_RECORD || type->kind == SL_SEM_ARRAY;
}

static LLVMTypeRef SLRoutineType(SLCodegenContext* ctx,
                                 const SLSymbol* symbol) {
    const SLRoutineSignature* sig = symbol->signature;
    size_t n = sig->parameter_count;
    if (n > UINT_MAX) {
        SLCGFail(ctx, symbol->span, "too many parameters");
        return NULL;
    }
    LLVMTypeRef* params = n ? calloc(n, sizeof(*params)) : NULL;
    if (n && !params) {
        SLCGFail(ctx, symbol->span, "out of memory lowering signature");
        return NULL;
    }
    for (size_t i = 0; i < n; ++i) {
        const SLSemType* param = sig->parameter_types[i];
        params[i] = SLCGByReference(param)
                        ? LLVMPointerTypeInContext(ctx->llvm, 0)
                        : SLCodegenType(ctx, param, symbol->span);
        if (!params[i]) {
            free(params);
            return NULL;
        }
    }
    LLVMTypeRef ret = SLCodegenType(ctx, sig->return_type, symbol->span);
    LLVMTypeRef fn =
        ret ? LLVMFunctionType(ret, params, (unsigned)n, false) : NULL;
    free(params);
    return fn;
}

static bool SLDeclareGlobal(SLCodegenContext* ctx, const SLSymbol* symbol) {
    if (symbol->kind == SL_SYMBOL_TYPE) return true;
    bool routine = symbol->kind == SL_SYMBOL_ROUTINE;
    LLVMTypeRef type = routine ? SLRoutineType(ctx, symbol)
                               : SLCodegenType(ctx, symbol->type, symbol->span);
    if (!type) return false;
    LLVMValueRef value;
    if (routine)
        value = LLVMAddFunction(ctx->module, "", type);
    else {
        value = LLVMAddGlobal(ctx->module, type, "");
        LLVMSetInitializer(value, LLVMConstNull(type));
    }
    SLSlice name = symbol->name;
    if (name.length == 4 && !memcmp(name.start, "main", 4))
        LLVMSetValueName2(value, "sl.main", 7);
    else
        LLVMSetValueName2(value, name.start, name.length);

    if (routine && !symbol->declaration) {
        for (size_t i = 0; i < symbol->signature->parameter_count; ++i) {
            if (symbol->signature->parameter_types[i]->kind == SL_SEM_BOOLEAN) {
                unsigned kind = LLVMGetEnumAttributeKindForName("zeroext", 7);

                LLVMAddAttributeAtIndex(
                    value, (unsigned)i + 1,
                    LLVMCreateEnumAttribute(ctx->llvm, kind, 0));
            }
        }
    }
    return SLCGBind(ctx, symbol, value);
}

static bool SLHasDefaults(const SLSemType* type) {
    if (type->kind == SL_SEM_ARRAY)
        return SLHasDefaults(type->as.array.element);

    if (type->kind == SL_SEM_RECORD) {
        for (size_t i = 0; i < type->as.record.count; ++i) {
            const SLSemField* field = &type->as.record.fields[i];
            if (field->init || SLHasDefaults(field->type)) return true;
        }
    }
    return false;
}

static bool SLFieldDefaults(SLCodegenContext* ctx, const SLSemType* type,
                            LLVMValueRef address, SLSpan span) {
    LLVMTypeRef lowered = SLCodegenType(ctx, type, span);
    if (!lowered) return false;

    if (type->kind == SL_SEM_ARRAY) {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->llvm);

        LLVMBasicBlockRef before = LLVMGetInsertBlock(ctx->builder);
        LLVMBasicBlockRef body = SLCGBlock(ctx, "defaults.body");
        LLVMBasicBlockRef end = SLCGBlock(ctx, "defaults.end");

        LLVMBuildBr(ctx->builder, body);
        LLVMPositionBuilderAtEnd(ctx->builder, body);

        LLVMValueRef index = LLVMBuildPhi(ctx->builder, i64, "index");
        LLVMValueRef zero = LLVMConstNull(i64);

        LLVMAddIncoming(index, &zero, &before, 1);
        LLVMValueRef element = SLCGElement(ctx, lowered, address, index);

        if (!SLFieldDefaults(ctx, type->as.array.element, element, span))
            return false;

        LLVMValueRef next = LLVMBuildAdd(ctx->builder, index,
                                         LLVMConstInt(i64, 1, false), "next");
        LLVMBasicBlockRef latch = LLVMGetInsertBlock(ctx->builder);

        LLVMAddIncoming(index, &next, &latch, 1);

        LLVMValueRef length = LLVMConstInt(i64, type->as.array.length, false);
        LLVMValueRef done = LLVMBuildICmp(ctx->builder, LLVMIntEQ, next, length, "done");

        LLVMBuildCondBr(ctx->builder, done, end, body);
        LLVMPositionBuilderAtEnd(ctx->builder, end);

    } else {
        for (size_t i = 0; i < type->as.record.count; ++i) {
            const SLSemField* field = &type->as.record.fields[i];
            if (!field->init && !SLHasDefaults(field->type)) continue;

            LLVMValueRef addr = LLVMBuildStructGEP2(ctx->builder, lowered, address, (unsigned)i, "field");

            if (field->init) {
                LLVMValueRef v = SLCGConvertedExpr(ctx, field->init, field->type);

                if (!v) return false;

                LLVMBuildStore(ctx->builder, v, addr);
            } else if (!SLFieldDefaults(ctx, field->type, addr, span))
                return false;
        }
    }
    return true;
}

static bool SLInitialize(SLCodegenContext* ctx, const SLDecl* decl,
                         LLVMValueRef address) {
    const SLSemType* type = decl->symbol->type;
    const SLExpr* init = decl->value.var_decl.init;

    if (init) {
        LLVMValueRef v = SLCGConvertedExpr(ctx, init, type);

        if (!v) return false;

        LLVMBuildStore(ctx->builder, v, address);
    } else {
        LLVMTypeRef lowered = SLCodegenType(ctx, type, decl->span);

        if (!lowered) return false;

        LLVMBuildStore(ctx->builder, LLVMConstNull(lowered), address);

        if (SLHasDefaults(type))
            return SLFieldDefaults(ctx, type, address, decl->span);
    }

    return true;
}

bool SLCGLocal(SLCodegenContext* ctx, const SLDecl* decl) {
    if (decl->kind == SL_DECL_TYPE) return true;
    LLVMTypeRef type = SLCodegenType(ctx, decl->symbol->type, decl->span);

    if (!type) return false;

    LLVMValueRef slot = SLCGSlot(ctx, type);

    return SLInitialize(ctx, decl, slot) && SLCGBind(ctx, decl->symbol, slot);
}

static void SLBeginFunction(SLCodegenContext* ctx, LLVMValueRef fn) {
    ctx->current_function = fn;

    LLVMPositionBuilderAtEnd(ctx->builder, SLCGBlock(ctx, "entry"));
}

static bool SLDefineRoutine(SLCodegenContext* ctx, const SLDecl* decl) {
    const SLSymbol* symbol = decl->symbol;
    LLVMValueRef fn = SLCGLookup(ctx, symbol, decl->span);

    if (!fn) return false;

    SLBeginFunction(ctx, fn);
    ctx->return_type = symbol->signature->return_type;
    size_t outer = ctx->binding_count;

    const SLParamArray* params = &decl->value.routine.params;

    for (size_t i = 0; i < params->count; ++i) {
        const SLSymbol* param = params->items[i].symbol;
        LLVMValueRef value = LLVMGetParam(fn, (unsigned)i);
        LLVMValueRef addr = value;

        if (!SLCGByReference(param->type)) {
            addr = SLCGSlot(ctx, LLVMTypeOf(value));
            LLVMBuildStore(ctx->builder, value, addr);
        }

        if (!SLCGBind(ctx, param, addr)) return false;
    }

    if (decl->value.routine.body_kind == SL_ROUTINE_EXPR) {
        if (!SLCGReturn(ctx, decl->value.routine.expr_body)) return false;
    } else {
        if (!SLCGBody(ctx, &decl->value.routine.body)) return false;

        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
            if (ctx->return_type->kind == SL_SEM_VOID)
                LLVMBuildRetVoid(ctx->builder);
            else
                LLVMBuildUnreachable(ctx->builder);
        }
    }
    ctx->binding_count = outer;
    return true;
}

static void SLRegisterInitializer(SLCodegenContext* ctx, LLVMValueRef fn) {
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->llvm, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->llvm);
    LLVMValueRef fields[] = {LLVMConstInt(i32, 65535, false), fn,
                             LLVMConstNull(ptr)};

    LLVMValueRef entry = LLVMConstStructInContext(ctx->llvm, fields, 3, false);
    LLVMValueRef entries = LLVMConstArray2(LLVMTypeOf(entry), &entry, 1);
    LLVMValueRef ctors =
        LLVMAddGlobal(ctx->module, LLVMTypeOf(entries), "llvm.global_ctors");

    LLVMSetLinkage(ctors, LLVMAppendingLinkage);
    LLVMSetInitializer(ctors, entries);
}

static void SLMainWrapper(SLCodegenContext* ctx, LLVMValueRef fn,
                          const SLRoutineSignature* sig) {
    if (sig->parameter_count || (sig->return_type->kind != SL_SEM_VOID &&
                                 sig->return_type->kind != SL_SEM_INTEGER))
        return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->llvm);
    LLVMTypeRef type = LLVMFunctionType(i32, NULL, 0, false);

    SLBeginFunction(ctx, LLVMAddFunction(ctx->module, "main", type));
    LLVMValueRef result = LLVMBuildCall2(
        ctx->builder, LLVMGlobalGetValueType(fn), fn, NULL, 0, "");

    LLVMValueRef status =
        sig->return_type->kind == SL_SEM_VOID
            ? LLVMConstNull(i32)
            : LLVMBuildTrunc(ctx->builder, result, i32, "status");

    LLVMBuildRet(ctx->builder, status);
}

static bool SLEntryWrapper(SLCodegenContext* ctx, const SLSymbol* symbol) {
    const SLCodegenOptions* opt = ctx->options;
    const SLRoutineSignature* sig = symbol->signature;
    size_t n = sig->parameter_count;
    char msg[160];

    if (n != opt->arg_count) {
        snprintf(msg, sizeof msg, "routine expects %zu argument(s), got %zu", n, opt->arg_count);
        return SLCGFail(ctx, symbol->span, msg);
    }
    LLVMValueRef fn = SLCGLookup(ctx, symbol, symbol->span);
    if (!fn) return false;

    LLVMValueRef* args = n ? calloc(n, sizeof *args) : NULL;
    if (n && !args) return SLCGFail(ctx, symbol->span, "out of memory");

    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->llvm);
    for (size_t i = 0; i < n; ++i) {
        const char* s = opt->args[i];
        char* end;
        switch (sig->parameter_types[i]->kind) {
            case SL_SEM_INTEGER: {
                errno = 0;
                long long v = strtoll(s, &end, 10);
                if (errno || end == s || *end) goto bad;
                args[i] = LLVMConstInt(i64, (unsigned long long)v, true);
                break;
            }
            case SL_SEM_REAL: {
                double d = strtod(s, &end);
                if (end == s || *end) goto bad;
                args[i] = LLVMConstReal(LLVMDoubleTypeInContext(ctx->llvm), d);
                break;
            }
            case SL_SEM_BOOLEAN:
                if (strcmp(s, "true") && strcmp(s, "false")) goto bad;
                args[i] = LLVMConstInt(LLVMInt1TypeInContext(ctx->llvm), !strcmp(s, "true"), false);
                break;
            default:
                free(args);
                return SLCGFail(ctx, symbol->span, "only integer, real and boolean arguments can be passed from the command line");
        }
        continue;
    bad:
        free(args);
        snprintf(msg, sizeof msg, "invalid value '%s' for argument %zu", s, i + 1);
        return SLCGFail(ctx, symbol->span, msg);
    }

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->llvm);
    SLBeginFunction(ctx, LLVMAddFunction(ctx->module, "main", LLVMFunctionType(i32, NULL, 0, false)));
    LLVMValueRef result = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(fn), fn, args, (unsigned)n, "");
    free(args);

    LLVMValueRef status = sig->return_type->kind == SL_SEM_INTEGER
                              ? LLVMBuildTrunc(ctx->builder, result, i32, "status")
                              : LLVMConstNull(i32);
    LLVMBuildRet(ctx->builder, status);
    return true;
}

bool SLCGDeclareProgram(SLCodegenContext* ctx, const SLProgram* program,
                        const SLSemanticState* semantics) {
    const SLSymbolTable* globals = &semantics->global_scope->symbols;

    for (size_t i = 0; i < globals->count; ++i)
        if (!SLDeclareGlobal(ctx, globals->items[i])) return false;

    LLVMValueRef init = NULL;

    for (size_t i = 0; i < program->decls.count; ++i) {
        const SLDecl* decl = program->decls.items[i];

        if (decl->kind != SL_DECL_VAR) continue;
        if (!decl->value.var_decl.init && !SLHasDefaults(decl->symbol->type))
            continue;

        if (!init) {
            LLVMTypeRef type = LLVMFunctionType(
                LLVMVoidTypeInContext(ctx->llvm), NULL, 0, false);
            init = LLVMAddFunction(ctx->module, "sl.init", type);
            LLVMSetLinkage(init, LLVMInternalLinkage);

            SLBeginFunction(ctx, init);
        }

        LLVMValueRef addr = SLCGLookup(ctx, decl->symbol, decl->span);
        if (!addr || !SLInitialize(ctx, decl, addr)) return false;
    }

    if (init) {
        LLVMBuildRetVoid(ctx->builder);
        SLRegisterInitializer(ctx, init);
    }

    for (size_t i = 0; i < program->decls.count; ++i) {
        const SLDecl* decl = program->decls.items[i];

        if (decl->kind != SL_DECL_ROUTINE ||
            decl->value.routine.body_kind == SL_ROUTINE_FORWARD)
            continue;

        if (!SLDefineRoutine(ctx, decl)) return false;

        SLSlice name = decl->symbol->name;

        if (name.length == 4 && !memcmp(name.start, "main", 4))
            SLMainWrapper(ctx, ctx->current_function, decl->symbol->signature);
    }

    if (ctx->options->entry) {
        const char* e = ctx->options->entry;
        SLSymbol* sym = SLSymbolTableFind(globals, (SLSlice){e, strlen(e)});
        if (!sym || sym->kind != SL_SYMBOL_ROUTINE)
            return SLCGFail(ctx, (SLSpan){0}, "entry routine not found");
        return SLEntryWrapper(ctx, sym);
    }

    return true;
}
