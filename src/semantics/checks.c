#include "optimize.h"

bool SLBodyTerminates(const SLBody* body) {
    for (size_t i = 0; i < body->count; ++i) {
        const SLBodyItem* item = &body->items[i];
        if (item->kind == SL_ITEM_STMT && SLStmtTerminates(item->value.stmt)) {
            return true;
        }
    }
    return false;
}

bool SLStmtTerminates(const SLStmt* stmt) {
    bool constant;

    switch (stmt->kind) {
        case SL_STMT_RETURN:
            return true;

        case SL_STMT_WHILE:
            return SLConstCondition(stmt->value.while_loop.condition,
                                    &constant) &&
                   constant;

        case SL_STMT_IF: {
            const SLBody* then_body = &stmt->value.if_stmt.then_body;
            const SLBody* else_body = &stmt->value.if_stmt.else_body;
            bool has_else = stmt->value.if_stmt.has_else;

            if (SLConstCondition(stmt->value.if_stmt.condition, &constant)) {
                return constant ? SLBodyTerminates(then_body)
                                : has_else && SLBodyTerminates(else_body);
            }
            return has_else && SLBodyTerminates(then_body) &&
                   SLBodyTerminates(else_body);
        }

        default:
            return false;
    }
}

static bool SLCheckExprTree(SLSemanticState* state, const SLExpr* expr);
static bool SLCheckBodyTree(SLSemanticState* state, const SLBody* body);

static bool SLIsZero(const SLConstValue* v) {
    return (v->kind == SL_CONST_INTEGER && v->as.i == 0) ||
           (v->kind == SL_CONST_REAL && v->as.r == 0.0);
}

static bool SLCheckExprTree(SLSemanticState* state, const SLExpr* expr) {
    if (expr == NULL) return true;

    switch (expr->type) {
        case SL_EXPR_UNARY:
            return SLCheckExprTree(state, expr->value.unary.operand);

        case SL_EXPR_BINARY: {
            if (!SLCheckExprTree(state, expr->value.binary.left) ||
                !SLCheckExprTree(state, expr->value.binary.right)) {
                return false;
            }

            SLTokenType op = expr->value.binary.op->type;
            if (op != SL_TOK_DIVIDE && op != SL_TOK_MODULO) return true;

            SLConstValue divisor;
            if (!SLConstEval(expr->value.binary.right, &divisor) ||
                !SLIsZero(&divisor)) {
                return true;
            }

            bool integer_result =
                op == SL_TOK_MODULO || (expr->sem_type != NULL &&
                                        expr->sem_type->kind == SL_SEM_INTEGER);
            if (integer_result) {
                return SLSemanticFail(state, expr->value.binary.right->span,
                                      "integer division by zero");
            }
            SLSemanticWarn(state, expr->value.binary.right->span,
                           "division by zero (the result is infinite or NaN)");
            return true;
        }

        case SL_EXPR_CALL: {
            const SLExprArray* args = &expr->value.call.arguments;
            for (size_t i = 0; i < args->count; ++i) {
                if (!SLCheckExprTree(state, args->items[i])) return false;
            }
            return true;
        }

        case SL_EXPR_INDEX: {
            const SLExpr* base = expr->value.index.base;
            const SLExpr* index = expr->value.index.index;
            if (!SLCheckExprTree(state, base) ||
                !SLCheckExprTree(state, index)) {
                return false;
            }

            SLConstValue c;
            if (base->sem_type != NULL &&
                base->sem_type->kind == SL_SEM_ARRAY &&
                SLConstEval(index, &c) && c.kind == SL_CONST_INTEGER) {
                long long length = base->sem_type->as.array.length;
                if (c.as.i < 0 || c.as.i >= length) {
                    return SLSemanticFail(state, index->span,
                                          "index %lld is out of bounds for an "
                                          "array of length %lld",
                                          c.as.i, length);
                }
            }
            return true;
        }

        case SL_EXPR_FIELD:
            return SLCheckExprTree(state, expr->value.field.base);

        default:
            return true;
    }
}

static bool SLCheckStmtTree(SLSemanticState* state, const SLStmt* stmt) {
    switch (stmt->kind) {
        case SL_STMT_ASSIGN:
            return SLCheckExprTree(state, stmt->value.assign.target) &&
                   SLCheckExprTree(state, stmt->value.assign.value);
        case SL_STMT_CALL:
            return SLCheckExprTree(state, stmt->value.call);
        case SL_STMT_WHILE:
            return SLCheckExprTree(state, stmt->value.while_loop.condition) &&
                   SLCheckBodyTree(state, &stmt->value.while_loop.body);
        case SL_STMT_FOR:
            return SLCheckExprTree(state, stmt->value.for_loop.from) &&
                   SLCheckExprTree(state, stmt->value.for_loop.to) &&
                   SLCheckBodyTree(state, &stmt->value.for_loop.body);
        case SL_STMT_IF:
            return SLCheckExprTree(state, stmt->value.if_stmt.condition) &&
                   SLCheckBodyTree(state, &stmt->value.if_stmt.then_body) &&
                   (!stmt->value.if_stmt.has_else ||
                    SLCheckBodyTree(state, &stmt->value.if_stmt.else_body));
        case SL_STMT_RETURN:
            return SLCheckExprTree(state, stmt->value.return_value);
    }
    return true;
}

static bool SLCheckDeclTree(SLSemanticState* state, const SLDecl* decl);

static bool SLCheckBodyTree(SLSemanticState* state, const SLBody* body) {
    bool terminated = false;
    bool warned = false;

    for (size_t i = 0; i < body->count; ++i) {
        const SLBodyItem* item = &body->items[i];

        if (terminated && !warned) {
            SLSpan span = item->kind == SL_ITEM_DECL ? item->value.decl->span
                                                     : item->value.stmt->span;
            SLSemanticWarn(state, span, "unreachable code");
            warned = true;
        }

        bool ok = item->kind == SL_ITEM_DECL
                      ? SLCheckDeclTree(state, item->value.decl)
                      : SLCheckStmtTree(state, item->value.stmt);
        if (!ok) return false;

        if (item->kind == SL_ITEM_STMT && SLStmtTerminates(item->value.stmt)) {
            terminated = true;
        }
    }
    return true;
}

static bool SLCheckDeclTree(SLSemanticState* state, const SLDecl* decl) {
    switch (decl->kind) {
        case SL_DECL_VAR:
            return SLCheckExprTree(state, decl->value.var_decl.init);

        case SL_DECL_TYPE:
            return true;

        case SL_DECL_ROUTINE: {
            switch (decl->value.routine.body_kind) {
                case SL_ROUTINE_FORWARD:
                    return true;
                case SL_ROUTINE_EXPR:
                    return SLCheckExprTree(state,
                                           decl->value.routine.expr_body);
                case SL_ROUTINE_BLOCK: {
                    const SLBody* body = &decl->value.routine.body;
                    if (decl->value.routine.return_type != NULL &&
                        !SLBodyTerminates(body)) {
                        SLSlice name = decl->value.routine.name->value;
                        return SLSemanticFail(state, decl->span,
                                              "routine '%.*s' may reach its "
                                              "end without returning a value",
                                              (int)name.length, name.start);
                    }
                    return SLCheckBodyTree(state, body);
                }
            }
            return true;
        }
    }
    return true;
}

static int SLCompareBySpan(const void* a, const void* b) {
    const SLSymbol* x = *(SLSymbol* const*)a;
    const SLSymbol* y = *(SLSymbol* const*)b;
    if (x->span.begin.line != y->span.begin.line) {
        return x->span.begin.line < y->span.begin.line ? -1 : 1;
    }
    return (x->span.begin.position > y->span.begin.position) -
           (x->span.begin.position < y->span.begin.position);
}

static void SLWarnUnusedVariables(SLSemanticState* state) {
    size_t capacity = 0;
    for (SLScope* s = state->all_scopes; s != NULL; s = s->alloc_next) {
        capacity += s->symbols.count;
    }
    if (capacity == 0) return;

    SLSymbol** unused = malloc(capacity * sizeof(*unused));
    if (unused == NULL) return;

    size_t count = 0;
    for (SLScope* s = state->all_scopes; s != NULL; s = s->alloc_next) {
        if (s == state->global_scope) continue;
        for (size_t i = 0; i < s->symbols.count; ++i) {
            SLSymbol* symbol = s->symbols.items[i];
            if (symbol->kind != SL_SYMBOL_VAR || symbol->use_count != 0)
                continue;
            if (symbol->name.length == 0 || symbol->name.start[0] == '_')
                continue;
            unused[count++] = symbol;
        }
    }

    qsort(unused, count, sizeof(*unused), SLCompareBySpan);
    for (size_t i = 0; i < count; ++i) {
        SLSemanticWarn(state, unused[i]->span, "variable '%.*s' is never read",
                       (int)unused[i]->name.length, unused[i]->name.start);
    }
    free(unused);
}

bool SLRunChecks(SLSemanticState* state, const SLProgram* program) {
    if (state == NULL || program == NULL || state->error.has_error)
        return false;

    for (size_t i = 0; i < program->decls.count; ++i) {
        if (!SLCheckDeclTree(state, program->decls.items[i])) return false;
    }

    SLWarnUnusedVariables(state);
    return true;
}
