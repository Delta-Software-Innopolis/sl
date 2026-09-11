#include "lexer.h"

bool SLResetBuffer(SLTokenArray* tokens, char buffer[128], int* bufWriter, int position, int line, char** start, bool* reading) {
    if (!(*reading)) return true;

    buffer[*bufWriter] = '\0';
    if (!SLAppendKeywordOrID(tokens, buffer, *bufWriter, position, line, *start)) {
        return false;
    }
    *bufWriter = 0;
    *start = NULL;
    *reading = false;

    return true;
}

bool SLScanText(SLCompilerState* state) {

    bool readingID;
    char* start = NULL;
    char buffer[128];
    int bufWriter = 0;

    // TODO: Fix position pls, now it always 1
    int line = 1;
    int position = 1;

    bool skipNextToken = false;
    
    for (int i = 0; i < state->text_size; i++) {
        if (skipNextToken) {
            skipNextToken = false;
            continue;
        }

        char c = state->text[i];
        char nc = state->text[i + 1];

        if (c == ' ' || c == '\t' || c == '\r') {
            if (!SLResetBuffer(&state->tokens, buffer, &bufWriter, position, line, &start, &readingID)) {
                return false;
            }
            
            continue;
        }
        
        if (c == '\n') {
            if (!SLResetBuffer(&state->tokens, buffer, &bufWriter, position, line, &start, &readingID)) {
                return false;
            }

            line++;
            position = 1;
            continue;
        }

        if ((isalpha(c) || c == '_') && !readingID) {
            readingID = true;
            
            start = &state->text[i];
            buffer[bufWriter++] = c;
            continue;
        }

        if ((isalnum(c) || c == '_') && readingID) {
            buffer[bufWriter++] = c;
            continue;
        } else {
            if (!SLResetBuffer(&state->tokens, buffer, &bufWriter, position, line, &start, &readingID)) {
                return false;
            }
        }

        
        // TODO: Add all simple tokens
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
                        SLTokenDebugPrintSlice("ASSIGNMENT", token.start, token.length, line, position);
                    }

                    skipNextToken = true;
                } else {
                    token.start = &state->text[i];
                    token.length = 1;
                    token.type = T_COLON;

                    SLTokenArrayPush(&state->tokens, token);

                    if (DEBUG) {
                        SLTokenDebugPrintSlice("COLON", token.start, token.length, line, position);
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
                    SLTokenDebugPrintSlice("LEFT PARANTHESIS", token.start, token.length, line, position);
                }

                break;
            }

            case ')': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_RPAR;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("RIGHT PARANTHESIS", token.start, token.length, line, position);
                }

                break;
            }

            case ',': {
                token.start = &state->text[i];
                token.length = 1;
                token.type = T_COMMA;

                SLTokenArrayPush(&state->tokens, token);

                if (DEBUG) {
                    SLTokenDebugPrintSlice("COMMA", token.start, token.length, line, position);
                }

                break;
            }
            
            default:
                break;
        }
        

        // TODO: Implement skipping comments!!1!1
        
    }

    return true;

}


