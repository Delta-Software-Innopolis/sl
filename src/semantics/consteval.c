#include "consteval.h"

#include <errno.h>
#include <limits.h>

static bool SLParseInteger(SLSlice text, long long* out) {
    char buffer[64];
    if (text.length == 0 || text.length >= sizeof(buffer)) return false;
    memcpy(buffer, text.start, text.length);
    buffer[text.length] = '\0';

    char* end = NULL;
    errno = 0;
    long long value = strtoll(buffer, &end, 10);
    if (errno != 0 || end == buffer || *end != '\0') return false;
    *out = value;
    return true;
}

static bool SLParseReal(SLSlice text, double* out) {
    char buffer[64];
    if (text.length == 0 || text.length >= sizeof(buffer)) return false;
    memcpy(buffer, text.start, text.length);
    buffer[text.length] = '\0';

    char* end = NULL;
    errno = 0;
    double value = strtod(buffer, &end);
    if (errno != 0 || end == buffer || *end != '\0') return false;
    *out = value;
    return true;
}

static bool SLIsNumber(const SLConstValue* v) {
    return v->kind == SL_CONST_INTEGER || v->kind == SL_CONST_REAL;
}

static double SLAsReal(const SLConstValue* v) {
    return v->kind == SL_CONST_REAL ? v->as.r : (double)v->as.i;
}

static int SLCompareNumbers(const SLConstValue* a, const SLConstValue* b) {
    if (a->kind == SL_CONST_INTEGER && b->kind == SL_CONST_INTEGER) {
        return (a->as.i > b->as.i) - (a->as.i < b->as.i);
    }
    double x = SLAsReal(a);
    double y = SLAsReal(b);
    return (x > y) - (x < y);
}

static bool SLEvalUnary(const SLToken* op, const SLConstValue* a,
                        SLConstValue* out) {
    switch (op->type) {
        case SL_TOK_MINUS:
            if (a->kind == SL_CONST_INTEGER) {
                if (a->as.i == LLONG_MIN) return false;
                out->kind = SL_CONST_INTEGER;
                out->as.i = -a->as.i;
                return true;
            }
            if (a->kind == SL_CONST_REAL) {
                out->kind = SL_CONST_REAL;
                out->as.r = -a->as.r;
                return true;
            }
            return false;
        case SL_TOK_PLUS:
            if (!SLIsNumber(a)) return false;
            *out = *a;
            return true;
        case SL_TOK_NOT:
            if (a->kind != SL_CONST_BOOLEAN) return false;
            out->kind = SL_CONST_BOOLEAN;
            out->as.b = !a->as.b;
            return true;
        default:
            return false;
    }
}

static bool SLEvalBinary(const SLToken* op, const SLConstValue* a,
                         const SLConstValue* b, SLConstValue* out) {
    bool numeric = SLIsNumber(a) && SLIsNumber(b);
    bool ints = a->kind == SL_CONST_INTEGER && b->kind == SL_CONST_INTEGER;
    bool bools = a->kind == SL_CONST_BOOLEAN && b->kind == SL_CONST_BOOLEAN;

    switch (op->type) {
        case SL_TOK_PLUS:
        case SL_TOK_MINUS:
        case SL_TOK_MULTIPLY:
        case SL_TOK_DIVIDE:
            if (!numeric) return false;
            if (ints) {
                long long r;
                switch (op->type) {
                    case SL_TOK_PLUS:
                        if (__builtin_add_overflow(a->as.i, b->as.i, &r))
                            return false;
                        break;
                    case SL_TOK_MINUS:
                        if (__builtin_sub_overflow(a->as.i, b->as.i, &r))
                            return false;
                        break;
                    case SL_TOK_MULTIPLY:
                        if (__builtin_mul_overflow(a->as.i, b->as.i, &r))
                            return false;
                        break;
                    default:  // SL_TOK_DIVIDE
                        if (b->as.i == 0) return false;
                        if (a->as.i == LLONG_MIN && b->as.i == -1) return false;
                        r = a->as.i / b->as.i;
                        break;
                }
                out->kind = SL_CONST_INTEGER;
                out->as.i = r;
                return true;
            } else {
                double x = SLAsReal(a);
                double y = SLAsReal(b);
                double r;
                switch (op->type) {
                    case SL_TOK_PLUS:
                        r = x + y;
                        break;
                    case SL_TOK_MINUS:
                        r = x - y;
                        break;
                    case SL_TOK_MULTIPLY:
                        r = x * y;
                        break;
                    default: /* SL_TOK_DIVIDE */
                        if (y == 0.0) return false;
                        r = x / y;
                        break;
                }
                out->kind = SL_CONST_REAL;
                out->as.r = r;
                return true;
            }

        case SL_TOK_MODULO:
            if (!ints || b->as.i == 0) return false;
            out->kind = SL_CONST_INTEGER;
            out->as.i = b->as.i == -1 ? 0 : a->as.i % b->as.i;
            return true;

        case SL_TOK_LESS:
        case SL_TOK_LESS_EQ:
        case SL_TOK_GREATER:
        case SL_TOK_GREATER_EQ: {
            if (!numeric) return false;
            int c = SLCompareNumbers(a, b);
            out->kind = SL_CONST_BOOLEAN;
            switch (op->type) {
                case SL_TOK_LESS:
                    out->as.b = c < 0;
                    break;
                case SL_TOK_LESS_EQ:
                    out->as.b = c <= 0;
                    break;
                case SL_TOK_GREATER:
                    out->as.b = c > 0;
                    break;
                default:
                    out->as.b = c >= 0;
                    break;
            }
            return true;
        }

        case SL_TOK_EQUAL:
        case SL_TOK_NOT_EQ: {
            bool equal;
            if (numeric) {
                equal = SLCompareNumbers(a, b) == 0;
            } else if (bools) {
                equal = a->as.b == b->as.b;
            } else {
                return false;
            }
            out->kind = SL_CONST_BOOLEAN;
            out->as.b = op->type == SL_TOK_EQUAL ? equal : !equal;
            return true;
        }

        case SL_TOK_AND:
        case SL_TOK_OR:
        case SL_TOK_XOR:
            if (!bools) return false;
            out->kind = SL_CONST_BOOLEAN;
            out->as.b = op->type == SL_TOK_AND  ? (a->as.b && b->as.b)
                        : op->type == SL_TOK_OR ? (a->as.b || b->as.b)
                                                : (a->as.b != b->as.b);
            return true;

        default:
            return false;
    }
}

bool SLConstEval(const SLExpr* expr, SLConstValue* out) {
    if (expr == NULL) return false;

    switch (expr->type) {
        case SL_EXPR_INTEGER:
            out->kind = SL_CONST_INTEGER;
            return SLParseInteger(expr->value.number->value, &out->as.i);

        case SL_EXPR_REAL:
            out->kind = SL_CONST_REAL;
            return SLParseReal(expr->value.number->value, &out->as.r);

        case SL_EXPR_BOOLEAN: {
            const SLToken* token = expr->value.boolean;
            out->kind = SL_CONST_BOOLEAN;
            out->as.b = token->type == SL_TOK_TRUE ||
                        (token->value.length == 4 &&
                         memcmp(token->value.start, "true", 4) == 0);
            return true;
        }

        case SL_EXPR_UNARY: {
            SLConstValue operand;
            if (!SLConstEval(expr->value.unary.operand, &operand)) return false;
            return SLEvalUnary(expr->value.unary.op, &operand, out);
        }

        case SL_EXPR_BINARY: {
            SLConstValue left, right;
            if (!SLConstEval(expr->value.binary.left, &left)) return false;
            if (!SLConstEval(expr->value.binary.right, &right)) return false;
            return SLEvalBinary(expr->value.binary.op, &left, &right, out);
        }

        default:
            return false;
    }
}

bool SLConstCondition(const SLExpr* condition, bool* value) {
    SLConstValue v;
    if (!SLConstEval(condition, &v)) return false;

    if (v.kind == SL_CONST_BOOLEAN) {
        *value = v.as.b;
        return true;
    }
    // integer to boolean is only allowed for 0 and 1
    if (v.kind == SL_CONST_INTEGER && (v.as.i == 0 || v.as.i == 1)) {
        *value = v.as.i == 1;
        return true;
    }
    return false;
}
