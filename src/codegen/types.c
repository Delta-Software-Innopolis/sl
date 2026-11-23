#include "codegen_internal.h"

#include <limits.h>
#include <stdlib.h>

struct SLRecordTypeEntry {
    const SLSemType* source;
    LLVMTypeRef type;
    SLRecordTypeEntry* next;
};

static void SLTypeError(SLCodegenContext* ctx, SLSpan span, const char* msg) {
    if (ctx->error.has_error) return;
    ctx->error.has_error = true;
    ctx->error.span = span;
    snprintf(ctx->error.message, sizeof(ctx->error.message), "%s", msg);
}

static LLVMTypeRef SLRecordType(SLCodegenContext* ctx, const SLSemType* type,
                                SLSpan span) {
    for (SLRecordTypeEntry* e = ctx->record_types; e != NULL; e = e->next) {
        if (e->source == type) return e->type;
    }

    size_t count = type->as.record.count;
    if (count > UINT_MAX) {
        SLTypeError(ctx, span, "too many fields in record");
        return NULL;
    }
    LLVMTypeRef* fields = NULL;
    if (count != 0) {
        fields = calloc(count, sizeof(*fields));
        if (fields == NULL) {
            SLTypeError(ctx, span, "out of memory lowering record fields");
            return NULL;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        fields[i] = SLCodegenType(ctx, type->as.record.fields[i].type, span);
        if (fields[i] == NULL) {
            free(fields);
            return NULL;
        }
    }

    SLRecordTypeEntry* entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        free(fields);
        SLTypeError(ctx, span, "out of memory caching record type");
        return NULL;
    }
    /* Identified structs preserve SL's nominal record identity. LLVM gives
       each new declaration a distinct name, including anonymous records. */
    entry->source = type;
    entry->type = LLVMStructCreateNamed(ctx->llvm, "sl.record");
    LLVMStructSetBody(entry->type, fields, (unsigned)count, false);
    free(fields);
    entry->next = ctx->record_types;
    ctx->record_types = entry;
    return entry->type;
}

LLVMTypeRef SLCodegenType(SLCodegenContext* ctx, const SLSemType* type,
                          SLSpan span) {
    if (ctx->error.has_error) return NULL;
    if (type != NULL) {
        switch (type->kind) {
            case SL_SEM_INTEGER:
                return LLVMInt64TypeInContext(ctx->llvm);
            case SL_SEM_REAL:
                return LLVMDoubleTypeInContext(ctx->llvm);
            case SL_SEM_BOOLEAN:
                return LLVMInt1TypeInContext(ctx->llvm);
            case SL_SEM_VOID:
                return LLVMVoidTypeInContext(ctx->llvm);
            case SL_SEM_ARRAY: {
                if (type->as.array.length <= 0) {
                    SLTypeError(ctx, span, "invalid array length");
                    return NULL;
                }
                LLVMTypeRef element =
                    SLCodegenType(ctx, type->as.array.element, span);
                if (element == NULL) return NULL;
                return LLVMArrayType2(element, (uint64_t)type->as.array.length);
            }
            case SL_SEM_RECORD:
                return SLRecordType(ctx, type, span);
            case SL_SEM_KIND_COUNT:
                break;
        }
    }
    SLTypeError(ctx, span, "missing or invalid semantic type");
    return NULL;
}

LLVMValueRef SLCodegenConvert(SLCodegenContext* ctx, LLVMValueRef value,
                              const SLSemType* target, SLSpan span) {
    LLVMTypeRef to = SLCodegenType(ctx, target, span);
    if (to == NULL) return NULL;
    if (value == NULL) {
        SLTypeError(ctx, span, "missing value for conversion");
        return NULL;
    }
    LLVMTypeRef from = LLVMTypeOf(value);
    if (from == to) return value;

    LLVMTypeRef integer = LLVMInt64TypeInContext(ctx->llvm);
    LLVMTypeRef boolean = LLVMInt1TypeInContext(ctx->llvm);
    if (from == integer && target->kind == SL_SEM_REAL)
        return LLVMBuildSIToFP(ctx->builder, value, to, "int.to.real");
    if (from == boolean && to == integer)
        return LLVMBuildZExt(ctx->builder, value, to, "bool.to.int");
    if (from == integer && to == boolean)
        return LLVMBuildICmp(ctx->builder, LLVMIntNE, value,
                             LLVMConstNull(integer), "int.to.bool");

    SLTypeError(ctx, span, "unsupported implicit conversion");
    return NULL;
}

void SLFreeCodegenTypes(SLCodegenContext* ctx) {
    while (ctx->record_types != NULL) {
        SLRecordTypeEntry* entry = ctx->record_types;
        ctx->record_types = entry->next;
        free(entry);
    }
}
