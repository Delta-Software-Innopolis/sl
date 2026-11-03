#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#pragma once

#include "../parser/parser.h"
#include "../token/token.h"

typedef enum {
    SL_SEM_INTEGER,
    SL_SEM_REAL,
    SL_SEM_BOOLEAN,
    SL_SEM_VOID,
    SL_SEM_ARRAY,
    SL_SEM_RECORD,
    SL_SEM_KIND_COUNT
} SLSemTypeKind;

typedef struct SLSemField {
    SLSlice name;
    struct SLSemType* type;
} SLSemField;

typedef struct SLSemType {
    SLSemTypeKind kind;
    struct SLSemType* alloc_next;

    union {
        struct {
            struct SLSemType* element;
            long long length;
        } array;

        struct {
            SLSemField* fields;
            size_t count;
            SLSlice name;
        } record;
    } as;
} SLSemType;

typedef struct {
    SLSemType** parameter_types;
    size_t parameter_count;
    SLSemType* return_type;
} SLRoutineSignature;

typedef enum {
    SL_TYPE_UNRESOLVED,
    SL_TYPE_RESOLVING,
    SL_TYPE_RESOLVED
} SLTypeResolutionState;

typedef enum {
    SL_SYMBOL_VAR,
    SL_SYMBOL_PARAM,
    SL_SYMBOL_TYPE,
    SL_SYMBOL_ROUTINE,
    SL_SYMBOL_ITERATOR
} SLSymbolKind;

typedef struct SLSymbol {
    SLSlice name;
    SLSymbolKind kind;

    SLSemType* type;
    SLSpan span;
    bool is_mutable;

    SLDecl* declaration;
    SLDecl* definition;

    SLTypeResolutionState resolution_state;
    SLRoutineSignature* signature;

    unsigned use_count;
} SLSymbol;

typedef struct {
    SLSymbol** items;
    size_t count;
    size_t capacity;
} SLSymbolTable;

bool SLSymbolTablePush(SLSymbolTable* table, SLSymbol* symbol);
SLSymbol* SLSymbolTableFind(const SLSymbolTable* table, SLSlice name);

#endif
