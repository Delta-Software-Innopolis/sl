#ifndef PARSER_H
#define PARSER_H

#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>

#include "../compiler_state/compiler_state.h"
#include "../token/token.h"

#define SL_ERROR_MAX_TRACE 64

typedef struct {
    const char* file;
    const char* func;
    int line;
} SLTraceFrame;

typedef struct {
    bool has_error;
    char message[256];

    const char* at;
    size_t length;
    SLTokenPosition position;

    SLTraceFrame trace[SL_ERROR_MAX_TRACE];
    size_t trace_count;
    bool trace_truncated;
} SLParseError;

typedef struct SLTokenReader {
    int cursor;
    SLTokenArray* tokens;

    bool isEOF;
    SLParseError error;
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

typedef struct SLType SLType;
typedef struct SLDecl SLDecl;
typedef struct SLStmt SLStmt;

typedef struct {
    SLDecl** items;
    size_t count;
    size_t capacity;
} SLDeclArray;

typedef enum {
    SL_TYPE_PRIMITIVE,
    SL_TYPE_NAMED,
    SL_TYPE_ARRAY,
    SL_TYPE_RECORD
} SLTypeKind;

struct SLType {
    SLTypeKind kind;
    SLSpan span;

    union {
        SLToken* primitive;
        SLToken* name;

        struct {
            SLExpr* size;
            SLType* element;
        } array;

        struct {
            SLDeclArray fields;
        } record;
    } value;
};

typedef enum {
    SL_ITEM_DECL, SL_ITEM_STMT
} SLBodyItemKind;

typedef struct {
    SLBodyItemKind kind;
    union {
        SLDecl* decl;
        SLStmt* stmt;
    } value;
} SLBodyItem;

typedef struct {
    SLBodyItem* items;
    size_t count;
    size_t capacity;
} SLBody;

typedef struct {
    SLToken* name;
    SLType* type;
    SLSpan span;
} SLParam;

typedef struct {
    SLParam* items;
    size_t count;
    size_t capacity;
} SLParamArray;

typedef enum {
    SL_DECL_VAR,
    SL_DECL_TYPE,
    SL_DECL_ROUTINE
} SLDeclKind;

typedef enum {
    SL_ROUTINE_FORWARD,
    SL_ROUTINE_BLOCK,
    SL_ROUTINE_EXPR
} SLRoutineBodyKind;

struct SLDecl {
    SLDeclKind kind;
    SLSpan span;

    union {
        struct {
            SLToken* name;
            SLType* type;   // NULL if omitted (var x = expr)
            SLExpr* init;   // NULL if omitted (var x : T)
        } var_decl;

        struct {
            SLToken* name;
            SLType* type;
        } type_decl;

        struct {
            SLToken* name;
            SLParamArray params;
            SLType* return_type;       // NULL if none
            SLRoutineBodyKind body_kind;
            SLBody body;               // used when SL_ROUTINE_BLOCK
            SLExpr* expr_body;         // used when SL_ROUTINE_EXPR
        } routine;
    } value;
};

typedef enum {
    SL_STMT_ASSIGN,
    SL_STMT_CALL,
    SL_STMT_WHILE,
    SL_STMT_FOR,
    SL_STMT_IF,
    SL_STMT_RETURN
} SLStmtKind;

struct SLStmt {
    SLStmtKind kind;
    SLSpan span;

    union {
        struct {
            SLExpr* target;  // NAME / FIELD / INDEX chain
            SLExpr* value;
        } assign;

        SLExpr* call;        // an SL_EXPR_CALL

        struct {
            SLExpr* condition;
            SLBody body;
        } while_loop;

        struct {
            SLToken* iterator;
            SLExpr* from;
            SLExpr* to;      // NULL if the range is a single expr
            bool reverse;
            SLBody body;
        } for_loop;

        struct {
            SLExpr* condition;
            SLBody then_body;
            SLBody else_body;
            bool has_else;
        } if_stmt;

        SLExpr* return_value; // NULL for bare "return"
    } value;
};

typedef struct {
    SLDeclArray decls;
} SLProgram;

SLTokenReader* SLInitTokenReader(SLCompilerState* state);
SLToken* SLGetNextToken(SLTokenReader* reader);

SLProgram* SLParseProgram(SLTokenReader* reader);
void SLFreeProgram(SLProgram* program);

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

void SLErrorRaise(SLTokenReader* r, SLToken* at, const char* file, int line,
        const char* func, const char* fmt, ...);
void SLErrorExpected(SLTokenReader* r, SLToken* found, const char* expected,
        const char* file, int line, const char* func);
void SLErrorTrace(SLTokenReader* r, const char* file, int line, const char* func);
void SLErrorPrint(const SLParseError* e, FILE* out, const char* filename,
        const char* source, size_t source_len);

#define SL_RAISE(r, at, ...) SLErrorRaise((r), (at), __FILE__, __LINE__, __func__, __VA_ARGS__)
#define SL_EXPECTED(r, found, what) SLErrorExpected((r), (found), (what), __FILE__, __LINE__, __func__)
#define SL_TRACE(r) SLErrorTrace((r), __FILE__, __LINE__, __func__)

#define SL_RETURN_TRACED(r, call) \
    do { SLExpr* _res = (call); if (_res == NULL) SL_TRACE(r); return _res; } while (0)

SLDecl* SLParseSimpleDecl(SLTokenReader* reader);
SLDecl* SLParseVarDecl(SLTokenReader* reader);
SLDecl* SLParseTypeDecl(SLTokenReader* reader);
SLDecl* SLParseRoutineDecl(SLTokenReader* reader);
bool SLParseParams(SLTokenReader* reader, SLParamArray* params);

SLType* SLParseType(SLTokenReader* reader);
SLType* SLParseArrayType(SLTokenReader* reader);
SLType* SLParseRecordType(SLTokenReader* reader);

// Parses until "end" / "else" / EOF, without consuming that token.
bool SLParseBody(SLTokenReader* reader, SLBody* body);

SLStmt* SLParseStatement(SLTokenReader* reader);
SLStmt* SLParseIDStatement(SLTokenReader* reader);
SLStmt* SLParseWhile(SLTokenReader* reader);
SLStmt* SLParseFor(SLTokenReader* reader);
SLStmt* SLParseIf(SLTokenReader* reader);
SLStmt* SLParseReturn(SLTokenReader* reader);

void SLExprDebugPrint(SLExpr* expr);
void SLTypeDebugPrint(SLType* type);
void SLDeclDebugPrint(SLDecl* decl);
void SLStmtDebugPrint(SLStmt* stmt);
void SLProgramDebugPrint(SLProgram* program);

#endif
