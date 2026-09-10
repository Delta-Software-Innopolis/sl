#ifndef TOKEN_H
#define TOKEN_H

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define DEBUG 1

typedef enum {
    T_COLON,
    T_SEMICOLON,
    T_ASSIGN,
    T_DOT,
    T_COMMA,
    T_LPAR,
    T_RPAR,
    T_LBRACKET,
    T_RBRACKET,

    T_VAR,
    T_IDENTIFIER,
    T_INTEGER,
    T_BOOLEAN,
    T_REAL,
    T_ARRAY,
    T_RECORD,
    
    T_FOR,
    T_IF,
    T_ELSE,
    T_WHILE,
    T_ROUTINE,
    T_LOOP,
    T_RETURN,
    T_END,

    T_INLINE_FUNC,
    T_REVERSE,
    T_RANGE,

    T_EQUAL,
    T_NOT_EQ,
    T_LESS,
    T_LESS_EQ,
    T_GREATER,
    T_GREATER_EQ,

    T_NOT,
    T_OR,
    T_AND,
    T_XOR,

    T_PLUS,
    T_MINUS,
    T_MULTIPLY,
    T_DIVIDE,
    T_MODULO,

    T_TRUE,
    T_FALSE

} SLTokenType;

typedef struct {
    SLTokenType type;

    int line, position;

    char* start;
    int length;

} SLToken;


typedef struct SLTokenArray {
    SLToken* items;
    size_t count;
    size_t capacity;
} SLTokenArray;

bool SLTokenArrayPush(SLTokenArray* array, SLToken token);
bool SLTokenEquals(const char* start, int length, const char* expected);
bool SLAppendKeywordOrID(SLTokenArray* array, char buffer[100], int length, int position, int line, char* start);

#endif