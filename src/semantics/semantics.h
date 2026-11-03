#ifndef SEMANTICS_H
#define SEMANTICS_H

#pragma once

#include "symbol_table.h"

typedef struct SLScope {
    struct SLScope* parent;
    struct SLScope* alloc_next;
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

    SLSemType primitive_types[SL_SEM_KIND_COUNT];
    SLSemanticError error;

    SLScope* all_scopes;
    SLSemType* current_return_type;

    SLSemType* all_types;

    // tokens created by the optimizer
    SLToken** owned_tokens;
    size_t owned_token_count;
    size_t owned_token_capacity;

    unsigned warning_count;

    bool globals_collected;
} SLSemanticState;

SLScope* SLScopePush(SLSemanticState* state);
void SLScopePop(SLSemanticState* state);
SLSymbol* SLScopeDeclare(SLSemanticState* state, SLSlice name,
                         SLSymbolKind kind, SLSemType* type, SLSpan span,
                         bool is_mutable);

SLSemType* SLCheckExpr(SLSemanticState* state, SLExpr* expr);
bool SLCheckStmt(SLSemanticState* state, SLStmt* stmt);
bool SLAnalyzeRoutines(SLSemanticState* state);

void SLSemanticErrorPrint(const SLSemanticState* state, FILE* out);
bool SLAnalyzeGlobalInits(SLSemanticState* state);

bool SLAnalyzeProgram(SLSemanticState* state, const SLProgram* program);

bool SLAnalyzeGlobal(SLSemanticState* state, const SLProgram* program);

SLSymbol* SLScopeLookupLocal(const SLScope* scope, SLSlice name);
SLSymbol* SLScopeLookup(const SLScope* scope, SLSlice name);

SLSemType* SLResolveType(SLSemanticState* state, const SLType* type);

bool SLAnalyzeGlobalTypes(SLSemanticState* state);

void SLFreeSemanticState(SLSemanticState* state);

bool SLSemanticFail(SLSemanticState* state, SLSpan span, const char* fmt, ...);
void SLSemanticWarn(SLSemanticState* state, SLSpan span, const char* fmt, ...);

SLToken* SLNewOwnedToken(SLSemanticState* state, SLTokenType type,
                         SLTokenPosition position, const char* text,
                         size_t length);

#endif
