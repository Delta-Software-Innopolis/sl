#ifndef TOKEN_H
#define TOKEN_H

#pragma once

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG 0

typedef struct {
    char* start;
    size_t length;
} SLSlice;

typedef struct {
    int line;
    int position;
} SLTokenPosition;

typedef enum {
    SL_TOK_COLON,
    SL_TOK_SEMICOLON,
    SL_TOK_ASSIGN,
    SL_TOK_DOT,
    SL_TOK_COMMA,
    SL_TOK_LPAR,
    SL_TOK_RPAR,
    SL_TOK_LBRACKET,
    SL_TOK_RBRACKET,
    SL_TOK_NEWLINE,

    SL_TOK_VAR,
    SL_TOK_IDENTIFIER,
    SL_TOK_INTEGER,
    SL_TOK_BOOLEAN,
    SL_TOK_REAL,
    SL_TOK_ARRAY,
    SL_TOK_RECORD,
    SL_TOK_TYPE,

    SL_TOK_FOR,
    SL_TOK_IF,
    SL_TOK_ELSE,
    SL_TOK_WHILE,
    SL_TOK_ROUTINE,
    SL_TOK_LOOP,
    SL_TOK_RETURN,
    SL_TOK_END,
    SL_TOK_THEN,

    SL_TOK_INLINE_FUNC,
    SL_TOK_REVERSE,
    SL_TOK_RANGE,
    SL_TOK_IN,

    SL_TOK_EQUAL,
    SL_TOK_NOT_EQ,
    SL_TOK_LESS,
    SL_TOK_LESS_EQ,
    SL_TOK_GREATER,
    SL_TOK_GREATER_EQ,

    SL_TOK_NOT,
    SL_TOK_OR,
    SL_TOK_AND,
    SL_TOK_XOR,

    SL_TOK_PLUS,
    SL_TOK_MINUS,
    SL_TOK_MULTIPLY,
    SL_TOK_DIVIDE,
    SL_TOK_MODULO,

    SL_TOK_TRUE,
    SL_TOK_FALSE,

    SL_TOK_INTEGER_LIT,
    SL_TOK_REAL_LIT,
} SLTokenType;

typedef struct {
    SLTokenType type;

    SLTokenPosition position;
    SLSlice value;

} SLToken;

typedef struct SLTokenArray {
    SLToken* items;
    size_t count;
    size_t capacity;
} SLTokenArray;

bool SLTokenArrayPush(SLTokenArray* array, SLToken token);
bool SLTokenEquals(const char* start, int length, const char* expected);
bool SLAppendKeywordOrID(SLTokenArray* array, char buffer[100], int length,
                         int position, int line, char* start);
void SLTokenDebugPrint(const char* token_type, char value[128], int line,
                       int position);
void SLTokenDebugPrintSlice(const char* token_type, char* start, int length,
                            int line, int position);

#endif
