#include "codegen_internal.h"

static bool SLBlockOpen(SLCodegenContext* ctx) {
    return !LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder));
}

bool SLCGReturn(SLCodegenContext* ctx, const SLExpr* expr) {
    LLVMValueRef value = NULL;
    if (expr) {
        value = SLCGConvertedExpr(ctx, expr, ctx->return_type);
        if (!value) return false;
    }
    if (ctx->return_type->kind == SL_SEM_VOID)
        LLVMBuildRetVoid(ctx->builder);
    else
        LLVMBuildRet(ctx->builder, value);
    return true;
}

static LLVMValueRef SLCondition(SLCodegenContext* ctx, const SLExpr* expr) {
    const SLSemType boolean = {.kind = SL_SEM_BOOLEAN};
    return SLCGConvertedExpr(ctx, expr, &boolean);
}

static bool SLIf(SLCodegenContext* ctx, const SLStmt* stmt) {
    LLVMValueRef cond = SLCondition(ctx, stmt->value.if_stmt.condition);
    if (!cond) return false;
    LLVMBasicBlockRef then_block = SLCGBlock(ctx, "if.then");
    LLVMBasicBlockRef end = SLCGBlock(ctx, "if.end");
    bool has_else = stmt->value.if_stmt.has_else;
    LLVMBasicBlockRef alt = has_else ? SLCGBlock(ctx, "if.else") : end;
    LLVMBuildCondBr(ctx->builder, cond, then_block, alt);
    LLVMPositionBuilderAtEnd(ctx->builder, then_block);
    if (!SLCGBody(ctx, &stmt->value.if_stmt.then_body)) return false;
    bool then_open = SLBlockOpen(ctx);
    if (then_open) LLVMBuildBr(ctx->builder, end);
    bool else_open = true;
    if (has_else) {
        LLVMPositionBuilderAtEnd(ctx->builder, alt);
        if (!SLCGBody(ctx, &stmt->value.if_stmt.else_body)) return false;
        else_open = SLBlockOpen(ctx);
        if (else_open) LLVMBuildBr(ctx->builder, end);
    }
    LLVMPositionBuilderAtEnd(ctx->builder, end);
    if (!then_open && !else_open) LLVMBuildUnreachable(ctx->builder);
    return true;
}

static bool SLWhile(SLCodegenContext* ctx, const SLStmt* stmt) {
    LLVMBasicBlockRef test = SLCGBlock(ctx, "while.test");
    LLVMBasicBlockRef body = SLCGBlock(ctx, "while.body");
    LLVMBasicBlockRef end = SLCGBlock(ctx, "while.end");
    LLVMBuildBr(ctx->builder, test);
    LLVMPositionBuilderAtEnd(ctx->builder, test);
    LLVMValueRef cond = SLCondition(ctx, stmt->value.while_loop.condition);
    if (!cond) return false;
    LLVMBuildCondBr(ctx->builder, cond, body, end);
    LLVMPositionBuilderAtEnd(ctx->builder, body);
    if (!SLCGBody(ctx, &stmt->value.while_loop.body)) return false;
    if (SLBlockOpen(ctx)) LLVMBuildBr(ctx->builder, test);
    LLVMPositionBuilderAtEnd(ctx->builder, end);
    return true;
}

static bool SLFor(SLCodegenContext* ctx, const SLStmt* stmt) {
    const SLExpr* from = stmt->value.for_loop.from;
    const SLExpr* to = stmt->value.for_loop.to;
    bool reverse = stmt->value.for_loop.reverse;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->llvm);
    LLVMValueRef start, limit, array = NULL;
    LLVMTypeRef array_type = NULL;
    if (to) {
        start = SLCGExpr(ctx, from);
        if (!start) return false;
        limit = SLCGExpr(ctx, to);
        if (!limit) return false;
    } else {
        array = SLCGAddress(ctx, from);
        array_type = SLCodegenType(ctx, from->sem_type, from->span);
        if (!array || !array_type) return false;
        LLVMValueRef last =
            LLVMConstInt(i64, from->sem_type->as.array.length - 1, false);
        start = reverse ? last : LLVMConstNull(i64);
        limit = reverse ? LLVMConstNull(i64) : last;
    }
    const SLSymbol* symbol = stmt->value.for_loop.symbol;
    LLVMTypeRef type = SLCodegenType(ctx, symbol->type, stmt->span);
    if (!type) return false;
    LLVMValueRef slot = SLCGSlot(ctx, type);
    if (!SLCGBind(ctx, symbol, slot)) return false;

    LLVMBasicBlockRef preheader = LLVMGetInsertBlock(ctx->builder);
    LLVMBasicBlockRef body = SLCGBlock(ctx, "for.body");
    LLVMBasicBlockRef end = SLCGBlock(ctx, "for.end");
    LLVMValueRef nonempty =
        LLVMBuildICmp(ctx->builder, reverse ? LLVMIntSGE : LLVMIntSLE, start,
                      limit, "nonempty");
    LLVMBuildCondBr(ctx->builder, nonempty, body, end);
    LLVMPositionBuilderAtEnd(ctx->builder, body);
    LLVMValueRef index = LLVMBuildPhi(ctx->builder, i64, "index");
    LLVMAddIncoming(index, &start, &preheader, 1);
    LLVMValueRef item = index;
    if (array) {
        LLVMValueRef address = SLCGElement(ctx, array_type, array, index);
        item = LLVMBuildLoad2(ctx->builder, type, address, "item");
    }
    LLVMBuildStore(ctx->builder, item, slot);
    if (!SLCGBody(ctx, &stmt->value.for_loop.body)) return false;
    if (SLBlockOpen(ctx)) {
        /* Test the inclusive endpoint before stepping past INT64_MIN/MAX. */
        LLVMBasicBlockRef step = SLCGBlock(ctx, "for.step");
        LLVMValueRef done =
            LLVMBuildICmp(ctx->builder, LLVMIntEQ, index, limit, "done");
        LLVMBuildCondBr(ctx->builder, done, end, step);
        LLVMPositionBuilderAtEnd(ctx->builder, step);
        LLVMValueRef delta = LLVMConstInt(i64, reverse ? UINT64_MAX : 1, false);
        LLVMValueRef next = LLVMBuildAdd(ctx->builder, index, delta, "next");
        LLVMBuildBr(ctx->builder, body);
        LLVMAddIncoming(index, &next, &step, 1);
    }
    LLVMPositionBuilderAtEnd(ctx->builder, end);
    return true;
}

static bool SLStatement(SLCodegenContext* ctx, const SLStmt* stmt) {
    switch (stmt->kind) {
        case SL_STMT_RETURN:
            return SLCGReturn(ctx, stmt->value.return_value);
        case SL_STMT_CALL:
            return SLCGExpr(ctx, stmt->value.call) != NULL;
        case SL_STMT_IF:
            return SLIf(ctx, stmt);
        case SL_STMT_WHILE:
            return SLWhile(ctx, stmt);
        case SL_STMT_FOR:
            return SLFor(ctx, stmt);
        case SL_STMT_ASSIGN: {
            const SLExpr* target = stmt->value.assign.target;
            LLVMValueRef addr = SLCGAddress(ctx, target);
            if (!addr) return false;
            LLVMValueRef value = SLCGConvertedExpr(
                ctx, stmt->value.assign.value, target->sem_type);
            if (!value) return false;
            LLVMBuildStore(ctx->builder, value, addr);
            return true;
        }
    }
    return SLCGFail(ctx, stmt->span, "unknown statement");
}

bool SLCGBody(SLCodegenContext* ctx, const SLBody* body) {
    size_t outer = ctx->binding_count;
    for (size_t i = 0; i < body->count && SLBlockOpen(ctx); ++i) {
        const SLBodyItem* item = &body->items[i];
        bool ok = item->kind == SL_ITEM_DECL
                      ? SLCGLocal(ctx, item->value.decl)
                      : SLStatement(ctx, item->value.stmt);
        if (!ok) return false;
    }
    ctx->binding_count = outer;
    return true;
}
