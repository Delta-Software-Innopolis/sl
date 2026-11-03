#ifndef SL_OPTIMIZE_H
#define SL_OPTIMIZE_H

#pragma once

#include "../parser/parser.h"
#include "consteval.h"
#include "semantics.h"

// does not modify ast

bool SLStmtTerminates(const SLStmt* stmt);
bool SLBodyTerminates(const SLBody* body);

/* checks:
 * 1 routine with a return type may reach its end without return (error)
 * 2 integer division by constant zero (error)
 * 3 constant array index out of bounds (error)
 * 4 unreachable code after return or infinite loop (warning)
 * 5 local variable unused (warning)
 * 6 real division by constant zero (warning) */
bool SLRunChecks(SLSemanticState* state, const SLProgram* program);

// modifies ast

typedef struct {
    size_t folded;
    size_t simplified;
    size_t dead_statements;
} SLOptStats;

/* optimizations:
 * 1 constant folding
 * 2 mathematic operations simplification
 * 3 remove dead code */
bool SLOptimizeProgram(SLSemanticState* state, SLProgram* program,
                       SLOptStats* stats);

bool SLAnalyzeAndOptimize(SLSemanticState* state, SLProgram* program,
                          SLOptStats* stats);

#endif
