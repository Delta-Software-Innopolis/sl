#ifndef PARSER_H
#define PARSER_H

#pragma once

#include <stdio.h>
#include <stdbool.h>

#include "../compiler_state/compiler_state.h"
#include "../token/token.h"


typedef struct SLTokenReader {
    int cursor;
    SLTokenArray* tokens;

    bool isEOF;
} SLTokenReader;

typedef struct SLExpr SLExpr;

typedef enum {
    SL_EXPR_INTEGER,
    SL_EXPR_REAL,
    SL_EXPR_BOOLEAN,
    SL_EXPR_NAME,
    SL_EXPR_UNARY,
    SL_EXPR_BINARY,
    SL_EXPR_CALL,
    SL_EXPR_INDEX,
    SL_EXPR_FIELD
} SLExprType;


typedef struct {
    SLExpr** items;
    size_t count;
    size_t capacity;
} SLExprArray;

bool SLExprArrayPush(SLExprArray* list, SLExpr* expr);

typedef struct {
    SLTokenPosition begin;
    SLTokenPosition end;
} SLSpan;


struct SLExpr {
    SLExprType type;
    SLSpan span;

    union {
        SLToken* number;
        SLToken* boolean;
        SLToken* name;

        struct {
            SLToken* op;
            SLExpr* operand;
        } unary;

        struct {
            SLToken* op;
            SLExpr* left;
            SLExpr* right;
        } binary;

        struct {
            SLToken* callee;
            SLExprArray arguments;
        } call;

        struct {
            SLExpr* base;
            SLExpr* index;
        } index;

        struct {
            SLExpr* base;
            SLToken* name;
        } field;
    } value;
};

void SLExprDebugPrint(SLExpr* expr);

SLTokenReader* SLInitTokenReader(SLCompilerState* state);
SLToken* SLGetNextToken(SLTokenReader* reader);

SLExpr* SLParseExpr(SLTokenReader* reader);
SLExpr* SLParseRelation(SLTokenReader* reader);
SLExpr* SLParseSimple(SLTokenReader* reader);
SLExpr* SLParseFactor(SLTokenReader* reader);
SLExpr* SLParseSummand(SLTokenReader* reader);
SLExpr* SLParseUnary(SLTokenReader* reader);
SLExpr* SLParsePrimary(SLTokenReader* reader);
SLExpr* SLParseIDPrimary(SLTokenReader* reader);
bool SLParseCallTail(SLTokenReader* reader, SLExprArray* list);
// Takes ownership of base; frees it if parsing the tail fails.
SLExpr* SLParseModifiableTail(SLTokenReader* reader, SLExpr* base);
SLToken* SLParseSign(SLTokenReader* reader);



#endif
