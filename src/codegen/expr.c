#include "../semantics/consteval.h"
#include "codegen_internal.h"

static void SLGuard(SLCodegenContext* ctx, LLVMValueRef valid) {
    LLVMBasicBlockRef ok = SLCGBlock(ctx, "checked");
    LLVMBasicBlockRef fail = SLCGBlock(ctx, "trap");

    LLVMBuildCondBr(ctx->builder, valid, ok, fail);
    LLVMPositionBuilderAtEnd(ctx->builder, fail);

    unsigned id = LLVMLookupIntrinsicID("llvm.trap", 9);
    LLVMValueRef trap = LLVMGetIntrinsicDeclaration(ctx->module, id, NULL, 0);

    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(trap), trap, NULL, 0,
                   "");
    LLVMBuildUnreachable(ctx->builder);
    LLVMPositionBuilderAtEnd(ctx->builder, ok);
}

LLVMValueRef SLCGElement(SLCodegenContext* ctx, LLVMTypeRef array,
                         LLVMValueRef address, LLVMValueRef index) {
    LLVMValueRef indices[] = {LLVMConstNull(LLVMTypeOf(index)), index};
    return LLVMBuildGEP2(ctx->builder, array, address, indices, 2, "element");
}

LLVMValueRef SLCGAddress(SLCodegenContext* ctx, const SLExpr* expr) {
    if (expr->type == SL_EXPR_NAME)
        return SLCGLookup(ctx, expr->symbol, expr->span);

    if (expr->type == SL_EXPR_INDEX || expr->type == SL_EXPR_FIELD) {
        bool indexed = expr->type == SL_EXPR_INDEX;

        const SLExpr* base =
            indexed ? expr->value.index.base : expr->value.field.base;

        LLVMValueRef address = SLCGAddress(ctx, base);
        LLVMTypeRef type = SLCodegenType(ctx, base->sem_type, base->span);

        if (!address || !type) return NULL;
        if (indexed) {
            LLVMValueRef index = SLCGExpr(ctx, expr->value.index.index);
            if (!index) return NULL;
            LLVMValueRef length = LLVMConstInt(
                LLVMTypeOf(index), base->sem_type->as.array.length, false);
            SLGuard(ctx, LLVMBuildICmp(ctx->builder, LLVMIntULT, index, length,
                                       "in.bounds"));
            return SLCGElement(ctx, type, address, index);
        }

        SLSlice name = expr->value.field.name->value;

        for (size_t i = 0; i < base->sem_type->as.record.count; ++i) {
            SLSlice field = base->sem_type->as.record.fields[i].name;
            if (field.length == name.length &&
                !memcmp(field.start, name.start, name.length))
                return LLVMBuildStructGEP2(ctx->builder, type, address,
                                           (unsigned)i, "field");
        }

        SLCGFail(ctx, expr->span, "unknown record field");
        return NULL;
    }
    LLVMValueRef value = SLCGExpr(ctx, expr);

    if (!value) return NULL;

    LLVMValueRef slot = SLCGSlot(ctx, LLVMTypeOf(value));
    LLVMBuildStore(ctx->builder, value, slot);

    return slot;
}

LLVMValueRef SLCGConvertedExpr(SLCodegenContext* ctx, const SLExpr* expr,
                               const SLSemType* target) {
    LLVMValueRef value = SLCGExpr(ctx, expr);

    return value ? SLCodegenConvert(ctx, value, target, expr->span) : NULL;
}

static LLVMValueRef SLCall(SLCodegenContext* ctx, const SLExpr* expr) {
    LLVMValueRef fn = SLCGLookup(ctx, expr->symbol, expr->span);

    if (!fn) return NULL;

    const SLRoutineSignature* sig = expr->symbol->signature;
    size_t count = sig->parameter_count;
    LLVMValueRef* args = count ? calloc(count, sizeof(*args)) : NULL;

    if (count && !args) {
        SLCGFail(ctx, expr->span, "out of memory generating call");
        return NULL;
    }

    for (size_t i = 0; i < count; ++i) {
        const SLExpr* arg = expr->value.call.arguments.items[i];
        const SLSemType* param = sig->parameter_types[i];

        args[i] = SLCGByReference(param) ? SLCGAddress(ctx, arg)
                                         : SLCGConvertedExpr(ctx, arg, param);
        if (!args[i]) {
            free(args);
            return NULL;
        }
    }

    LLVMValueRef result =
        LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(fn), fn, args,
                       (unsigned)count, "");

    if (!expr->symbol->declaration) {
        unsigned kind = LLVMGetEnumAttributeKindForName("zeroext", 7);

        for (size_t i = 0; i < count; ++i) {
            unsigned index = (unsigned)i + 1;

            LLVMAttributeRef attr =
                LLVMGetEnumAttributeAtIndex(fn, index, kind);
            if (attr) LLVMAddCallSiteAttribute(result, index, attr);
        }
    }

    free(args);

    return result;
}

static LLVMValueRef SLIntegerDivision(SLCodegenContext* ctx, LLVMValueRef a,
                                      LLVMValueRef b, bool modulo) {
    LLVMBuilderRef builder = ctx->builder;
    LLVMTypeRef i64 = LLVMTypeOf(a);
    LLVMValueRef valid = LLVMBuildICmp(builder, LLVMIntNE, b, LLVMConstNull(i64), "nonzero");

    LLVMValueRef neg_one = LLVMBuildICmp(builder, LLVMIntEQ, b, LLVMConstAllOnes(i64), "minus.one");

    if (modulo) {
        b = LLVMBuildSelect(builder, neg_one, LLVMConstInt(i64, 1, false), b,
                            "divisor");
    } else {
        LLVMValueRef min = LLVMConstInt(i64, (uint64_t)INT64_MIN, false);

        LLVMValueRef overflow =
            LLVMBuildAnd(builder, neg_one,
                         LLVMBuildICmp(builder, LLVMIntEQ, a, min, ""), "");

        valid = LLVMBuildAnd(builder, valid,
                             LLVMBuildNot(builder, overflow, ""), "");
    }

    SLGuard(ctx, valid);

    return modulo ? LLVMBuildSRem(builder, a, b, "rem")
                  : LLVMBuildSDiv(builder, a, b, "quot");
}

static LLVMValueRef SLBinary(SLCodegenContext* ctx, const SLExpr* expr) {
    const SLExpr* left = expr->value.binary.left;

    const SLExpr* right = expr->value.binary.right;

    bool real = left->sem_type->kind == SL_SEM_REAL ||
                right->sem_type->kind == SL_SEM_REAL;

    const SLSemType* common = left->sem_type->kind == SL_SEM_REAL ? left->sem_type : right->sem_type;

    LLVMValueRef a = SLCGConvertedExpr(ctx, left, common);

    if (!a) return NULL;
    LLVMValueRef b = SLCGConvertedExpr(ctx, right, common);

    if (!b) return NULL;

    LLVMBuilderRef ir = ctx->builder;
    LLVMIntPredicate ip;
    LLVMRealPredicate fp;

    switch (expr->value.binary.op->type) {
        case SL_TOK_PLUS:
            return real ? LLVMBuildFAdd(ir, a, b, "sum")
                        : LLVMBuildAdd(ir, a, b, "sum");
        case SL_TOK_MINUS:
            return real ? LLVMBuildFSub(ir, a, b, "diff")
                        : LLVMBuildSub(ir, a, b, "diff");
        case SL_TOK_MULTIPLY:
            return real ? LLVMBuildFMul(ir, a, b, "product")
                        : LLVMBuildMul(ir, a, b, "product");
        case SL_TOK_DIVIDE:
            return real ? LLVMBuildFDiv(ir, a, b, "quot")
                        : SLIntegerDivision(ctx, a, b, false);
        case SL_TOK_MODULO:
            return SLIntegerDivision(ctx, a, b, true);
        case SL_TOK_AND:
            return LLVMBuildAnd(ir, a, b, "and");
        case SL_TOK_OR:
            return LLVMBuildOr(ir, a, b, "or");
        case SL_TOK_XOR:
            return LLVMBuildXor(ir, a, b, "xor");
        case SL_TOK_LESS:
            ip = LLVMIntSLT;
            fp = LLVMRealOLT;
            break;
        case SL_TOK_LESS_EQ:
            ip = LLVMIntSLE;
            fp = LLVMRealOLE;
            break;
        case SL_TOK_GREATER:
            ip = LLVMIntSGT;
            fp = LLVMRealOGT;
            break;
        case SL_TOK_GREATER_EQ:
            ip = LLVMIntSGE;
            fp = LLVMRealOGE;
            break;
        case SL_TOK_EQUAL:
            ip = LLVMIntEQ;
            fp = LLVMRealOEQ;
            break;
        case SL_TOK_NOT_EQ:
            ip = LLVMIntNE;
            fp = LLVMRealUNE;
            break;
        default:
            SLCGFail(ctx, expr->span, "unknown binary operator");
            return NULL;
    }

    return real ? LLVMBuildFCmp(ir, fp, a, b, "cmp")
                : LLVMBuildICmp(ir, ip, a, b, "cmp");
}

LLVMValueRef SLCGExpr(SLCodegenContext* ctx, const SLExpr* expr) {
    LLVMTypeRef type = SLCodegenType(ctx, expr->sem_type, expr->span);
    if (!type) return NULL;
    switch (expr->type) {
        case SL_EXPR_NAME:
        case SL_EXPR_FIELD:
        case SL_EXPR_INDEX: {
            LLVMValueRef addr = SLCGAddress(ctx, expr);
            return addr ? LLVMBuildLoad2(ctx->builder, type, addr, "value")
                        : NULL;
        }
        case SL_EXPR_INTEGER:
        case SL_EXPR_REAL:
        case SL_EXPR_BOOLEAN: {
            SLConstValue literal;
            if (!SLConstEval(expr, &literal)) {
                SLCGFail(ctx, expr->span, "invalid numeric literal");
                return NULL;
            }
            if (literal.kind == SL_CONST_REAL)
                return LLVMConstReal(type, literal.as.r);
            if (literal.kind == SL_CONST_BOOLEAN)
                return LLVMConstInt(type, literal.as.b, false);
            return LLVMConstInt(type, (uint64_t)literal.as.i, true);
        }
        case SL_EXPR_CALL:
            return SLCall(ctx, expr);
        case SL_EXPR_BINARY:
            return SLBinary(ctx, expr);
        case SL_EXPR_UNARY: {
            LLVMValueRef value = SLCGExpr(ctx, expr->value.unary.operand);
            if (!value) return NULL;
            switch (expr->value.unary.op->type) {
                case SL_TOK_PLUS:
                    return value;
                case SL_TOK_NOT:
                    return LLVMBuildNot(ctx->builder, value, "not");
                case SL_TOK_MINUS:
                    return expr->sem_type->kind == SL_SEM_REAL
                               ? LLVMBuildFNeg(ctx->builder, value, "neg")
                               : LLVMBuildNeg(ctx->builder, value, "neg");
                default:
                    break;
            }
        }
    }
    SLCGFail(ctx, expr->span, "unknown expression");
    return NULL;
}
