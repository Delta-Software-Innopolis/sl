#include "lexer.h"

bool SLResetBuffer(SLTokenArray* tokens, char buffer[128], int* bufWriter,
                   int position, int line, char** start, bool* readingID,
                   bool* readingInteger, bool* readingReal) {
    if (!(*readingID) && !(*readingInteger) && !(*readingReal)) return true;

    buffer[*bufWriter] = '\0';
    if (!SLAppendKeywordOrID(tokens, buffer, *bufWriter, position, line,
                             *start)) {
        return false;
    }
    *bufWriter = 0;
    *start = NULL;
    *readingID = false;
    *readingInteger = false;
    *readingReal = false;

    return true;
}

bool SLPushToBuffer(char buffer[128], char symbol, int* writer) {
    if (*writer + 1 >= 128) {
        buffer[127] = '\0';
        fprintf(stderr, "Identifier '%.64s...' is too long\n", buffer);
        return false;
    }

    buffer[(*writer)++] = symbol;

    return true;
}

bool SLScanText(SLCompilerState* state) {
    bool readingID;
    bool readingInteger;
    bool readingReal;
    char* start = NULL;
    char buffer[128];
    int bufWriter = 0;

    int line = 1;
    int position = 0;
    int realPosition = 0;

    bool skipNextToken = false;
    bool comment = false;

    for (int i = 0; i < state->text_size; i++) {
        realPosition++;
        position++;

        if (skipNextToken) {
            skipNextToken = false;
            continue;
        }

        char c = state->text[i];
        char nc = state->text[i + 1];  // if at last symbol it will read null
                                       // character so its safe
        if (comment) {
            if (c == '\n') {
                SLToken token;
                token.line = line;
                token.position = realPosition;
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_NEWLINE;
                SLTokenArrayPush(&state->tokens, token);
                if (DEBUG) {
                    SLTokenDebugPrintSlice("NEW LINE", token.start,
                                           token.length, line, realPosition);
                }

                comment = false;
                line++;
                position = 0;
                realPosition = 0;
            }
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!SLResetBuffer(&state->tokens, buffer, &bufWriter, position,
                               line, &start, &readingID, &readingInteger,
                               &readingReal)) {
                return false;
            }

            if (c == '\n') {
                SLToken token;
                token.line = line;
                token.position = realPosition;
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_NEWLINE;
                SLTokenArrayPush(&state->tokens, token);
                if (DEBUG) {
                    SLTokenDebugPrintSlice("NEW LINE", token.start,
                                           token.length, line, realPosition);
                }

                line++;
                realPosition = 0;
            }

            position = realPosition;
            continue;
        }

        if ((isalpha(c) || c == '_') && !readingID) {
            readingID = true;
            position--;

            start = &state->text[i];
            if (!SLPushToBuffer(buffer, c, &bufWriter)) return false;
            continue;
        }

        if (isdigit(c) && !readingID && !readingInteger && !readingReal) {
            readingInteger = true;
            position--;

            start = &state->text[i];
            if (!SLPushToBuffer(buffer, c, &bufWriter)) return false;
            continue;
        }

        if ((isdigit(c) && (readingInteger || readingReal)) ||
            (c == '.' && nc != '.' && readingInteger)) {
            if (!SLPushToBuffer(buffer, c, &bufWriter)) return false;
            position--;
            if (c == '.') {
                readingInteger = false;
                readingReal = true;
            }
            continue;
        } else if ((isalnum(c) || c == '_') && readingID) {
            if (!SLPushToBuffer(buffer, c, &bufWriter)) return false;
            position--;
            continue;
        } else {
            if (!SLResetBuffer(&state->tokens, buffer, &bufWriter, position,
                               line, &start, &readingID, &readingInteger,
                               &readingReal)) {
                return false;
            }
            position = realPosition;
        }

        SLToken token;
        token.line = line;
        token.position = position;

        switch (c) {
            case ':': {
                if (nc == '=') {
                    token.start = &state->text[i];
                    token.length = 2;
                    token.type = T_ASSIGN;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("ASSIGNMENT", token.start,
                                               token.length, line, position);
                    }

                    skipNextToken = true;
                } else {
                    token.start = &state->text[i];
                    token.length = 1;
                    token.type = T_COLON;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("COLON", token.start,
                                               token.length, line, position);
                    }
                }

                break;
            }

            case '(': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_LPAR;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("LEFT PARANTHESIS", token.start,
                                           token.length, line, position);
                }

                break;
            }

            case ')': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_RPAR;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("RIGHT PARANTHESIS", token.start,
                                           token.length, line, position);
                }

                break;
            }

            case ',': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_COMMA;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("COMMA", token.start, token.length,
                                           line, position);
                }

                break;
            }

            case ';': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_SEMICOLON;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("SEMICOLON", token.start,
                                           token.length, line, position);
                }

                break;
            }

            case '.': {
                if (nc == '.') {
                    token.start = &state->text[i];
                    token.length = 2;
                    token.type = T_RANGE;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("RANGE", token.start,
                                               token.length, line, position);
                    }

                    skipNextToken = true;
                } else {
                    token.start = &state->text[i];
                    token.length = 1;
                    token.type = T_DOT;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("DOT", token.start, token.length,
                                               line, position);
                    }
                }

                break;
            }

            case '[': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_LBRACKET;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("LEFT BRACKET", token.start,
                                           token.length, line, position);
                }

                break;
            }

            case ']': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_RBRACKET;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("RIGHT BRACKET", token.start,
                                           token.length, line, position);
                }

                break;
            }

            case '=': {
                if (nc == '>') {
                    token.start = &state->text[i];
                    token.length = 2;
                    token.type = T_INLINE_FUNC;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("INLINE FUNC", token.start,
                                               token.length, line, position);
                    }

                    skipNextToken = true;
                } else {
                    token.start = &state->text[i];
                    token.length = 1;
                    token.type = T_EQUAL;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("EQUAL", token.start,
                                               token.length, line, position);
                    }
                }

                break;
            }

            case '/': {
                if (nc == '=') {
                    token.start = &state->text[i];
                    token.length = 2;
                    token.type = T_NOT_EQ;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("NOT EQUAL", token.start,
                                               token.length, line, position);
                    }

                    skipNextToken = true;
                } else if (nc == '/') {
                    comment = true;
                } else {
                    token.start = &state->text[i];
                    token.length = 1;
                    token.type = T_DIVIDE;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("DIVIDE", token.start,
                                               token.length, line, position);
                    }
                }

                break;
            }

            case '<': {
                if (nc == '=') {
                    token.start = &state->text[i];
                    token.length = 2;
                    token.type = T_LESS_EQ;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("LESS OR EQUAL", token.start,
                                               token.length, line, position);
                    }

                    skipNextToken = true;
                } else {
                    token.start = &state->text[i];
                    token.length = 1;
                    token.type = T_LESS;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("LESS", token.start,
                                               token.length, line, position);
                    }
                }

                break;
            }

            case '>': {
                if (nc == '=') {
                    token.start = &state->text[i];
                    token.length = 2;
                    token.type = T_GREATER_EQ;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("GREATER OR EQUAL", token.start,
                                               token.length, line, position);
                    }

                    skipNextToken = true;
                } else {
                    token.start = &state->text[i];
                    token.length = 1;
                    token.type = T_GREATER;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("GREATER", token.start,
                                               token.length, line, position);
                    }
                }

                break;
            }

            case '+': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_PLUS;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("PLUS", token.start, token.length,
                                           line, position);
                }

                break;
            }

            case '-': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_MINUS;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("MINUS", token.start, token.length,
                                           line, position);
                }

                break;
            }

            case '*': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_MULTIPLY;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("MULTIPLY", token.start,
                                           token.length, line, position);
                }

                break;
            }

            case '%': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_MODULO;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("MODULO", token.start, token.length,
                                           line, position);
                }

                break;
            }

            default:
                printf("sl: %s:%d:%d: lexical error: unexpected token %c\n", state->filename, line, position, c);
                return false;
                break;
        }
    }

    return true;
}
