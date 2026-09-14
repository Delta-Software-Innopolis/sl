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

    token.start = start;
    token.length = length;
    token.line = line;
    token.position = position;

    // TODO: Add all keywords

    if (isdigit(buffer[0])) {
        if (strchr(buffer, '.')) {
            token.type = T_REAL_LIT;
            if (DEBUG) {
                SLTokenDebugPrint("REAL NUMBER", buffer, line, position);
            }
        } else {
            token.type = T_INTEGER_LIT;
            if (DEBUG) {
                SLTokenDebugPrint("INTEGER NUMBER", buffer, line, position);
            }
        }
    } else if (strcmp(buffer, "routine") == 0) {
        token.type = T_ROUTINE;

        if (DEBUG) {
            SLTokenDebugPrint("ROUTINE", buffer, line, position);
        }

    } else if (strcmp(buffer, "integer") == 0) {
        token.type = T_INTEGER;

        if (DEBUG) {
            SLTokenDebugPrint("INTEGER", buffer, line, position);
        }
    } else if (strcmp(buffer, "var") == 0) {
        token.type = T_VAR;

        if (DEBUG) {
            SLTokenDebugPrint("VAR", buffer, line, position);
        }
    } else if (strcmp(buffer, "for") == 0) {
        token.type = T_FOR;

        if (DEBUG) {
            SLTokenDebugPrint("FOR", buffer, line, position);
        }
    } else if (strcmp(buffer, "boolean") == 0) {
        token.type = T_BOOLEAN;

        if (DEBUG) {
            SLTokenDebugPrint("BOOLEAN", buffer, line, position);
        }
    } else if (strcmp(buffer, "real") == 0) {
        token.type = T_REAL;

        if (DEBUG) {
            SLTokenDebugPrint("REAL", buffer, line, position);
        }
    } else if (strcmp(buffer, "array") == 0) {
        token.type = T_ARRAY;

        if (DEBUG) {
            SLTokenDebugPrint("ARRAY", buffer, line, position);
        }
    } else if (strcmp(buffer, "record") == 0) {
        token.type = T_RECORD;

        if (DEBUG) {
            SLTokenDebugPrint("RECORD", buffer, line, position);
        }
    } else if (strcmp(buffer, "if") == 0) {
        token.type = T_IF;

        if (DEBUG) {
            SLTokenDebugPrint("IF", buffer, line, position);
        }
    } else if (strcmp(buffer, "else") == 0) {
        token.type = T_ELSE;

        if (DEBUG) {
            SLTokenDebugPrint("ELSE", buffer, line, position);
        }
    } else if (strcmp(buffer, "while") == 0) {
        token.type = T_WHILE;

        if (DEBUG) {
            SLTokenDebugPrint("WHILE", buffer, line, position);
        }
    } else if (strcmp(buffer, "loop") == 0) {
        token.type = T_LOOP;

        if (DEBUG) {
            SLTokenDebugPrint("LOOP", buffer, line, position);
        }
    } else if (strcmp(buffer, "return") == 0) {
        token.type = T_RETURN;

        if (DEBUG) {
            SLTokenDebugPrint("RETURN", buffer, line, position);
        }
    } else if (strcmp(buffer, "end") == 0) {
        token.type = T_END;

        if (DEBUG) {
            SLTokenDebugPrint("END", buffer, line, position);
        }
    } else if (strcmp(buffer, "reverse") == 0) {
        token.type = T_REVERSE;

        if (DEBUG) {
            SLTokenDebugPrint("REVERSE", buffer, line, position);
        }
    } else if (strcmp(buffer, "in") == 0) {
        token.type = T_IN;

        if (DEBUG) {
            SLTokenDebugPrint("IN", buffer, line, position);
        }
    } else if (strcmp(buffer, "not") == 0) {
        token.type = T_NOT;

        if (DEBUG) {
            SLTokenDebugPrint("NOT", buffer, line, position);
        }
    } else if (strcmp(buffer, "or") == 0) {
        token.type = T_OR;

        if (DEBUG) {
            SLTokenDebugPrint("OR", buffer, line, position);
        }
    } else if (strcmp(buffer, "and") == 0) {
        token.type = T_AND;

        if (DEBUG) {
            SLTokenDebugPrint("AND", buffer, line, position);
        }
    } else if (strcmp(buffer, "xor") == 0) {
        token.type = T_XOR;

        if (DEBUG) {
            SLTokenDebugPrint("XOR", buffer, line, position);
        }
    } else if (strcmp(buffer, "true") == 0) {
        token.type = T_TRUE;

        if (DEBUG) {
            SLTokenDebugPrint("TRUE", buffer, line, position);
        }
    } else if (strcmp(buffer, "false") == 0) {
        token.type = T_FALSE;

        if (DEBUG) {
            SLTokenDebugPrint("FALSE", buffer, line, position);
        }
    } else if (strcmp(buffer, "then") == 0) {
        token.type = T_THEN;

        if (DEBUG) {
            SLTokenDebugPrint("THEN", buffer, line, position);
        }
    } else {
        token.type = T_IDENTIFIER;
        token.start = start;
        token.length = length;
        token.line = line;
        token.position = position;

        if (DEBUG) {
            SLTokenDebugPrint("IDENTIFIER", buffer, line, position);
        }
    }

    SLTokenArrayPush(array, token);
    return true;
}
