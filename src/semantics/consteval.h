#ifndef SL_CONSTEVAL_H
#define SL_CONSTEVAL_H

#pragma once

#include "../parser/parser.h"

typedef enum { SL_CONST_INTEGER, SL_CONST_REAL, SL_CONST_BOOLEAN } SLConstKind;

typedef struct {
    SLConstKind kind;
    union {
        long long i;
        double r;
        bool b;
    } as;
} SLConstValue;

// return false when the expression is not constant, or
// when evaluating it would cause error (handle in runtime checks)
bool SLConstEval(const SLExpr* expr, SLConstValue* out);

bool SLConstCondition(const SLExpr* condition, bool* value);

#endif
