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
    
    for (int i = 0; i < state->text_size; i++) {
        char c = state->text[i];
        
        SLToken token;

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

        
        // TODO: Processing of simple tokens (colon, assignment, ...)
        switch (c) {
            case ':':
                break;
            
            default:
                break;
        }

        // TODO: Implement skipping comments!!1!1
        
    }

    return true;

}


