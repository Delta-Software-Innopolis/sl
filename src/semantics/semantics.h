#ifndef SEMANTICS_H
#define SEMANTICS_H

#pragma once

#include "symbol_table.h"

typedef struct SLScope {
    struct SLScope* parent;
    SLSymbolTable symbols;
} SLScope;

typedef struct {
    bool has_error;
    char message[256];
    SLSpan span;
} SLSemanticError;

typedef struct {
    SLCompilerState* compiler_state;

    SLScope* global_scope;
    SLScope* current_scope;
    SLDecl* current_routine;

    SLSemType primitive_types[SL_SEM_ERROR + 1];
    SLSemanticError error;
} SLSemanticState;


bool SLAnalyzeProgram(SLSemanticState* state, const SLProgram* program);

bool SLAnalyzeGlobal(SLSemanticState* state, const SLProgram* program);

SLSymbol* SLScopeLookupLocal(const SLScope* scope, SLSlice name);
SLSymbol* SLScopeLookup(const SLScope* scope, SLSlice name);


SLSemType* SLResolveType(SLSemanticState* state, const SLType* type);

bool SLAnalyzeGlobalTypes(SLSemanticState* state);

void SLFreeSemanticState(SLSemanticState* state);

#endif
