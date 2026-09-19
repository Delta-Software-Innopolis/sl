#include "token.h"

void SLTokenDebugPrint(const char* token_type, char value[128], int line,
                       int position) {
    printf("Token Type: %s, value: %s, line: %d, position: %d\n", token_type,
           value, line, position);
}

void SLTokenDebugPrintSlice(const char* token_type, char* start, int length,
                            int line, int position) {
    if (*start == '\n') {
        printf("Token Type: %s, value: \\n, line: %d, position: %d\n",
               token_type, line, position);

    } else {
        printf("Token Type: %s, value: %.*s, line: %d, position: %d\n",
               token_type, length, start, line, position);
    }
}

bool SLTokenArrayPush(SLTokenArray* array, SLToken token) {
    if (array->count == array->capacity) {
        size_t new_capacity = array->capacity == 0 ? 64 : array->capacity * 2;

        SLToken* new_items =
            realloc(array->items, new_capacity * sizeof(*new_items));

        if (new_items == NULL) {
            return false;
        }

        array->items = new_items;
        array->capacity = new_capacity;
    }

    array->items[array->count++] = token;
    return true;
}

bool SLTokenEquals(const char* start, int length, const char* expected) {
    size_t expected_length = strlen(expected);

    return length == (int)expected_length &&
           memcmp(start, expected, expected_length) == 0;
}

bool SLAppendKeywordOrID(SLTokenArray* array, char buffer[128], int length,
                         int position, int line, char* start) {
    SLToken token;

    token.value.start = start;
    token.value.length = length;
    token.position.line = line;
    token.position.position = position;

    if (isdigit(buffer[0])) {
        if (strchr(buffer, '.')) {
            token.type = SL_TOK_REAL_LIT;
            if (DEBUG) {
                SLTokenDebugPrint("REAL NUMBER", buffer, line, position);
            }
        } else {
            token.type = SL_TOK_INTEGER_LIT;
            if (DEBUG) {
                SLTokenDebugPrint("INTEGER NUMBER", buffer, line, position);
            }
        }
    } else if (strcmp(buffer, "routine") == 0) {
        token.type = SL_TOK_ROUTINE;

        if (DEBUG) {
            SLTokenDebugPrint("ROUTINE", buffer, line, position);
        }

    } else if (strcmp(buffer, "integer") == 0) {
        token.type = SL_TOK_INTEGER;

        if (DEBUG) {
            SLTokenDebugPrint("INTEGER", buffer, line, position);
        }
    } else if (strcmp(buffer, "var") == 0) {
        token.type = SL_TOK_VAR;

        if (DEBUG) {
            SLTokenDebugPrint("VAR", buffer, line, position);
        }
    } else if (strcmp(buffer, "for") == 0) {
        token.type = SL_TOK_FOR;

        if (DEBUG) {
            SLTokenDebugPrint("FOR", buffer, line, position);
        }
    } else if (strcmp(buffer, "boolean") == 0) {
        token.type = SL_TOK_BOOLEAN;

        if (DEBUG) {
            SLTokenDebugPrint("BOOLEAN", buffer, line, position);
        }
    } else if (strcmp(buffer, "real") == 0) {
        token.type = SL_TOK_REAL;

        if (DEBUG) {
            SLTokenDebugPrint("REAL", buffer, line, position);
        }
    } else if (strcmp(buffer, "array") == 0) {
        token.type = SL_TOK_ARRAY;

        if (DEBUG) {
            SLTokenDebugPrint("ARRAY", buffer, line, position);
        }
    } else if (strcmp(buffer, "record") == 0) {
        token.type = SL_TOK_RECORD;

        if (DEBUG) {
            SLTokenDebugPrint("RECORD", buffer, line, position);
        }
    } else if (strcmp(buffer, "if") == 0) {
        token.type = SL_TOK_IF;

        if (DEBUG) {
            SLTokenDebugPrint("IF", buffer, line, position);
        }
    } else if (strcmp(buffer, "else") == 0) {
        token.type = SL_TOK_ELSE;

        if (DEBUG) {
            SLTokenDebugPrint("ELSE", buffer, line, position);
        }
    } else if (strcmp(buffer, "while") == 0) {
        token.type = SL_TOK_WHILE;

        if (DEBUG) {
            SLTokenDebugPrint("WHILE", buffer, line, position);
        }
    } else if (strcmp(buffer, "loop") == 0) {
        token.type = SL_TOK_LOOP;

        if (DEBUG) {
            SLTokenDebugPrint("LOOP", buffer, line, position);
        }
    } else if (strcmp(buffer, "return") == 0) {
        token.type = SL_TOK_RETURN;

        if (DEBUG) {
            SLTokenDebugPrint("RETURN", buffer, line, position);
        }
    } else if (strcmp(buffer, "end") == 0) {
        token.type = SL_TOK_END;

        if (DEBUG) {
            SLTokenDebugPrint("END", buffer, line, position);
        }
    } else if (strcmp(buffer, "reverse") == 0) {
        token.type = SL_TOK_REVERSE;

        if (DEBUG) {
            SLTokenDebugPrint("REVERSE", buffer, line, position);
        }
    } else if (strcmp(buffer, "in") == 0) {
        token.type = SL_TOK_IN;

        if (DEBUG) {
            SLTokenDebugPrint("IN", buffer, line, position);
        }
    } else if (strcmp(buffer, "not") == 0) {
        token.type = SL_TOK_NOT;

        if (DEBUG) {
            SLTokenDebugPrint("NOT", buffer, line, position);
        }
    } else if (strcmp(buffer, "or") == 0) {
        token.type = SL_TOK_OR;

        if (DEBUG) {
            SLTokenDebugPrint("OR", buffer, line, position);
        }
    } else if (strcmp(buffer, "and") == 0) {
        token.type = SL_TOK_AND;

        if (DEBUG) {
            SLTokenDebugPrint("AND", buffer, line, position);
        }
    } else if (strcmp(buffer, "xor") == 0) {
        token.type = SL_TOK_XOR;

        if (DEBUG) {
            SLTokenDebugPrint("XOR", buffer, line, position);
        }
    } else if (strcmp(buffer, "true") == 0) {
        token.type = SL_TOK_TRUE;

        if (DEBUG) {
            SLTokenDebugPrint("TRUE", buffer, line, position);
        }
    } else if (strcmp(buffer, "false") == 0) {
        token.type = SL_TOK_FALSE;

        if (DEBUG) {
            SLTokenDebugPrint("FALSE", buffer, line, position);
        }
    } else if (strcmp(buffer, "then") == 0) {
        token.type = SL_TOK_THEN;

        if (DEBUG) {
            SLTokenDebugPrint("THEN", buffer, line, position);
        }
    } else if (strcmp(buffer, "type") == 0) {
        token.type = SL_TOK_TYPE;

        if (DEBUG) {
            SLTokenDebugPrint("TYPE", buffer, line, position);
        }
    } else {
        token.type = SL_TOK_IDENTIFIER;
        token.value.start = start;
        token.value.length = length;
        token.position.line = line;
        token.position.position = position;

        if (DEBUG) {
            SLTokenDebugPrint("IDENTIFIER", buffer, line, position);
        }
    }

    SLTokenArrayPush(array, token);
    return true;
}
