#include "optimize.h"

#include <math.h>

typedef struct {
    SLSemanticState* state;
    SLOptStats* stats;
} SLOpt;

static bool SLKindMatches(const SLSemType* type, SLConstKind kind) {
    switch (kind) {
        case SL_CONST_INTEGER:
            return type->kind == SL_SEM_INTEGER;
        case SL_CONST_REAL:
            return type->kind == SL_SEM_REAL;
        case SL_CONST_BOOLEAN:
            return type->kind == SL_SEM_BOOLEAN;
    }
    return false;
}

static void SLFormatReal(char* out, size_t size, double value) {
    for (int precision = 15; precision <= 17; ++precision) {
        snprintf(out, size, "%.*g", precision, value);
        if (strtod(out, NULL) == value) break;
    }
    if (strchr(out, '.') == NULL) {
        char* exponent = strpbrk(out, "eE");
        if (exponent != NULL) {
            memmove(exponent + 2, exponent, strlen(exponent) + 1);
            exponent[0] = '.';
            exponent[1] = '0';
        } else {
            strcat(out, ".0");
        }
    }
}

static bool SLReplaceWithConst(SLSemanticState* state, SLExpr* expr,
                               const SLConstValue* value) {
    char text[64];
    SLTokenType token_type;
    SLExprType expr_type;

    switch (value->kind) {
        case SL_CONST_INTEGER:
            snprintf(text, sizeof(text), "%lld", value->as.i);
            token_type = SL_TOK_INTEGER_LIT;
            expr_type = SL_EXPR_INTEGER;
            break;
        case SL_CONST_REAL:
            if (!isfinite(value->as.r)) return false;
            SLFormatReal(text, sizeof(text), value->as.r);
            token_type = SL_TOK_REAL_LIT;
            expr_type = SL_EXPR_REAL;
            break;
        case SL_CONST_BOOLEAN:
            strcpy(text, value->as.b ? "true" : "false");
            token_type = value->as.b ? SL_TOK_TRUE : SL_TOK_FALSE;
            expr_type = SL_EXPR_BOOLEAN;
            break;
        default:
            return false;
    }

    SLToken* token = SLNewOwnedToken(state, token_type, expr->span.begin, text,
                                     strlen(text));
    if (token == NULL) return false;

    SLExpr* a = NULL;
    SLExpr* b = NULL;
    if (expr->type == SL_EXPR_BINARY) {
        a = expr->value.binary.left;
        b = expr->value.binary.right;
    } else if (expr->type == SL_EXPR_UNARY) {
        a = expr->value.unary.operand;
    }
    SLFreeExpr(a);
    SLFreeExpr(b);

    memset(&expr->value, 0, sizeof(expr->value));
    expr->type = expr_type;
    expr->symbol = NULL;
    expr->value.number = token;
    return true;
}

static void SLReplaceWithChild(SLExpr* expr, SLExpr* keep, SLExpr* drop) {
    SLExpr moved = *keep;
    free(keep);
    SLFreeExpr(drop);
    *expr = moved;
}

static bool SLIsPure(const SLExpr* expr) {
    if (expr == NULL) return true;
    switch (expr->type) {
        case SL_EXPR_CALL:
            return false;
        case SL_EXPR_UNARY:
            return SLIsPure(expr->value.unary.operand);
        case SL_EXPR_BINARY: {
            SLTokenType op = expr->value.binary.op->type;
            if (op == SL_TOK_DIVIDE || op == SL_TOK_MODULO) return false;
            return SLIsPure(expr->value.binary.left) &&
                   SLIsPure(expr->value.binary.right);
        }
        case SL_EXPR_INDEX:
            return SLIsPure(expr->value.index.base) &&
                   SLIsPure(expr->value.index.index);
        case SL_EXPR_FIELD:
            return SLIsPure(expr->value.field.base);
        default:
            return true;
    }
}

static bool SLIsNumberValue(const SLConstValue* v, long long n) {
    return (v->kind == SL_CONST_INTEGER && v->as.i == n) ||
           (v->kind == SL_CONST_REAL && v->as.r == (double)n);
}

static bool SLIsBoolValue(const SLConstValue* v, bool b) {
    return v->kind == SL_CONST_BOOLEAN && v->as.b == b;
}

static bool SLSimplifyBinary(SLOpt* o, SLExpr* e) {
    SLExpr* l = e->value.binary.left;
    SLExpr* r = e->value.binary.right;
    SLTokenType op = e->value.binary.op->type;

    SLConstValue lv, rv;
    bool lc = SLConstEval(l, &lv);
    bool rc = SLConstEval(r, &rv);

    SLExpr* keep = NULL;
    SLExpr* drop = NULL;

    switch (op) {
        case SL_TOK_PLUS:
            if (rc && SLIsNumberValue(&rv, 0)) {
                keep = l;
                drop = r;
            } else if (lc && SLIsNumberValue(&lv, 0)) {
                keep = r;
                drop = l;
            }
            break;
        case SL_TOK_MINUS:
            if (rc && SLIsNumberValue(&rv, 0)) {
                keep = l;
                drop = r;
            }
            break;
        case SL_TOK_MULTIPLY:
            if (rc && SLIsNumberValue(&rv, 1)) {
                keep = l;
                drop = r;
            } else if (lc && SLIsNumberValue(&lv, 1)) {
                keep = r;
                drop = l;
            }
            break;
        case SL_TOK_DIVIDE:
            if (rc && SLIsNumberValue(&rv, 1)) {
                keep = l;
                drop = r;
            }
            break;
        case SL_TOK_AND:
            if (rc && SLIsBoolValue(&rv, true)) {
                keep = l;
                drop = r;
            } else if (lc && SLIsBoolValue(&lv, true)) {
                keep = r;
                drop = l;
            }
            break;
        case SL_TOK_OR:
            if (rc && SLIsBoolValue(&rv, false)) {
                keep = l;
                drop = r;
            } else if (lc && SLIsBoolValue(&lv, false)) {
                keep = r;
                drop = l;
            }
            break;
        default:
            break;
    }

    // x + 0.0 can change the expr type
    if (keep != NULL && keep->sem_type != NULL && e->sem_type != NULL &&
        keep->sem_type->kind == e->sem_type->kind) {
        SLReplaceWithChild(e, keep, drop);
        return true;
    }

    // x * 0 = 0
    if (op == SL_TOK_MULTIPLY && e->sem_type != NULL &&
        e->sem_type->kind == SL_SEM_INTEGER) {
        bool zero_right =
            rc && rv.kind == SL_CONST_INTEGER && rv.as.i == 0 && SLIsPure(l);
        bool zero_left =
            lc && lv.kind == SL_CONST_INTEGER && lv.as.i == 0 && SLIsPure(r);
        if (zero_right || zero_left) {
            SLConstValue zero = {.kind = SL_CONST_INTEGER, .as.i = 0};
            return SLReplaceWithConst(o->state, e, &zero);
        }
    }
    return false;
}

static bool SLSimplifyUnary(SLExpr* e) {
    SLExpr* operand = e->value.unary.operand;
    SLTokenType op = e->value.unary.op->type;

    if (operand->sem_type == NULL || e->sem_type == NULL ||
        operand->sem_type->kind != e->sem_type->kind) {
        return false;
    }

    // +x = x
    if (op == SL_TOK_PLUS) {
        SLReplaceWithChild(e, operand, NULL);
        return true;
    }

    // - -x = x; not not x = x
    if ((op == SL_TOK_MINUS || op == SL_TOK_NOT) &&
        operand->type == SL_EXPR_UNARY && operand->value.unary.op->type == op) {
        SLExpr* inner = operand->value.unary.operand;
        if (inner->sem_type == NULL ||
            inner->sem_type->kind != e->sem_type->kind) {
            return false;
        }
        operand->value.unary.operand = NULL;
        SLReplaceWithChild(e, inner, operand);
        return true;
    }
    return false;
}

static void SLOptExpr(SLOpt* o, SLExpr* e) {
    if (e == NULL) return;

    switch (e->type) {
        case SL_EXPR_UNARY:
            SLOptExpr(o, e->value.unary.operand);
            break;
        case SL_EXPR_BINARY:
            SLOptExpr(o, e->value.binary.left);
            SLOptExpr(o, e->value.binary.right);
            break;
        case SL_EXPR_CALL:
            for (size_t i = 0; i < e->value.call.arguments.count; ++i) {
                SLOptExpr(o, e->value.call.arguments.items[i]);
            }
            return;
        case SL_EXPR_INDEX:
            SLOptExpr(o, e->value.index.base);
            SLOptExpr(o, e->value.index.index);
            return;
        case SL_EXPR_FIELD:
            SLOptExpr(o, e->value.field.base);
            return;
        default:
            return;
    }

    SLConstValue value;
    if (e->sem_type != NULL && SLConstEval(e, &value) &&
        SLKindMatches(e->sem_type, value.kind) &&
        SLReplaceWithConst(o->state, e, &value)) {
        o->stats->folded++;
        return;
    }

    bool changed =
        e->type == SL_EXPR_BINARY ? SLSimplifyBinary(o, e) : SLSimplifyUnary(e);
    if (changed) o->stats->simplified++;
}

static bool SLBodyHasDecl(const SLBody* body) {
    for (size_t i = 0; i < body->count; ++i) {
        if (body->items[i].kind == SL_ITEM_DECL) return true;
    }
    return false;
}

static void SLFreeItem(SLBodyItem* item) {
    if (item->kind == SL_ITEM_DECL) {
        SLFreeDecl(item->value.decl);
    } else {
        SLFreeStmt(item->value.stmt);
    }
}

static void SLDropItem(SLBody* body, size_t index) {
    SLFreeItem(&body->items[index]);
    memmove(&body->items[index], &body->items[index + 1],
            (body->count - index - 1) * sizeof(SLBodyItem));
    body->count--;
}

/*
 * `if <const> then A else B` -> the items of the taken branch, spliced into
 * the parent body in place of the `if`. Only called when the taken branch
 * declares nothing: its items would otherwise move into the parent scope and
 * could start shadowing (or clashing with) outer names.
 */
static bool SLSpliceIf(SLBody* parent, size_t index, bool take_then) {
    SLStmt* stmt = parent->items[index].value.stmt;
    SLBody* taken = take_then ? &stmt->value.if_stmt.then_body
                              : &stmt->value.if_stmt.else_body;
    SLBody* other = take_then ? &stmt->value.if_stmt.else_body
                              : &stmt->value.if_stmt.then_body;
    size_t n = taken->count;
    size_t new_count = parent->count - 1 + n;

    if (new_count > parent->capacity) {
        SLBodyItem* items = realloc(parent->items, new_count * sizeof(*items));
        if (items == NULL) return false;  //
        parent->items = items;
        parent->capacity = new_count;
    }

    memmove(&parent->items[index + n], &parent->items[index + 1],
            (parent->count - index - 1) * sizeof(SLBodyItem));
    if (n != 0)
        memcpy(&parent->items[index], taken->items, n * sizeof(SLBodyItem));
    parent->count = new_count;

    // taken items now belong to parent; free only array
    free(taken->items);
    *taken = (SLBody){0};

    SLFreeExpr(stmt->value.if_stmt.condition);
    SLFreeBody(other);
    free(stmt);
    return true;
}

static void SLOptBody(SLOpt* o, SLBody* body);

static void SLOptDecl(SLOpt* o, SLDecl* decl) {
    switch (decl->kind) {
        case SL_DECL_VAR:
            SLOptExpr(o, decl->value.var_decl.init);
            break;
        case SL_DECL_TYPE:
            break;
        case SL_DECL_ROUTINE:
            if (decl->value.routine.body_kind == SL_ROUTINE_BLOCK) {
                SLOptBody(o, &decl->value.routine.body);
            } else if (decl->value.routine.body_kind == SL_ROUTINE_EXPR) {
                SLOptExpr(o, decl->value.routine.expr_body);
            }
            break;
    }
}

static void SLOptStmt(SLOpt* o, SLStmt* stmt) {
    switch (stmt->kind) {
        case SL_STMT_ASSIGN:
            SLOptExpr(o, stmt->value.assign.target);
            SLOptExpr(o, stmt->value.assign.value);
            break;
        case SL_STMT_CALL:
            SLOptExpr(o, stmt->value.call);
            break;
        case SL_STMT_WHILE:
            SLOptExpr(o, stmt->value.while_loop.condition);
            SLOptBody(o, &stmt->value.while_loop.body);
            break;
        case SL_STMT_FOR:
            SLOptExpr(o, stmt->value.for_loop.from);
            SLOptExpr(o, stmt->value.for_loop.to);
            SLOptBody(o, &stmt->value.for_loop.body);
            break;
        case SL_STMT_IF:
            SLOptExpr(o, stmt->value.if_stmt.condition);
            SLOptBody(o, &stmt->value.if_stmt.then_body);
            if (stmt->value.if_stmt.has_else) {
                SLOptBody(o, &stmt->value.if_stmt.else_body);
            }
            break;
        case SL_STMT_RETURN:
            SLOptExpr(o, stmt->value.return_value);
            break;
    }
}

static void SLOptBody(SLOpt* o, SLBody* body) {
    for (size_t i = 0; i < body->count;) {
        SLBodyItem* item = &body->items[i];

        if (item->kind == SL_ITEM_DECL) {
            SLOptDecl(o, item->value.decl);
            ++i;
            continue;
        }

        SLStmt* stmt = item->value.stmt;
        SLOptStmt(o, stmt);

        bool constant;

        /* while false */
        if (stmt->kind == SL_STMT_WHILE &&
            SLConstCondition(stmt->value.while_loop.condition, &constant) &&
            !constant) {
            SLDropItem(body, i);
            o->stats->dead_statements++;
            continue;
        }

        /* if <const> */
        if (stmt->kind == SL_STMT_IF &&
            SLConstCondition(stmt->value.if_stmt.condition, &constant)) {
            const SLBody* taken = constant ? &stmt->value.if_stmt.then_body
                                           : &stmt->value.if_stmt.else_body;
            if (!SLBodyHasDecl(taken) && SLSpliceIf(body, i, constant)) {
                o->stats->dead_statements++;
                continue;
            }
        }

        /* after return or an infinite loop is unreachable */
        if (SLStmtTerminates(stmt)) {
            for (size_t j = i + 1; j < body->count; ++j) {
                SLFreeItem(&body->items[j]);
                o->stats->dead_statements++;
            }
            body->count = i + 1;
            return;
        }

        ++i;
    }
}

bool SLOptimizeProgram(SLSemanticState* state, SLProgram* program,
                       SLOptStats* stats) {
    if (state == NULL || program == NULL || state->error.has_error)
        return false;

    SLOptStats local = {0};
    if (stats != NULL) *stats = (SLOptStats){0};

    SLOpt o = {state, stats != NULL ? stats : &local};
    for (size_t i = 0; i < program->decls.count; ++i) {
        SLOptDecl(&o, program->decls.items[i]);
    }
    return true;
}

bool SLAnalyzeAndOptimize(SLSemanticState* state, SLProgram* program,
                          SLOptStats* stats) {
    return SLAnalyzeProgram(state, program) && SLRunChecks(state, program) &&
           SLOptimizeProgram(state, program, stats);
}
