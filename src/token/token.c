#include "token.h"


bool SLTokenArrayPush(SLTokenArray* array, SLToken token) {
    if (array->count == array->capacity) {
        size_t new_capacity = array->capacity == 0 ? 64 : array->capacity * 2;

        SLToken* new_items = realloc(array->items, new_capacity * sizeof(*new_items));

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

    return length == (int) expected_length && memcmp(start, expected, expected_length) == 0;
}

bool SLAppendKeywordOrID(SLTokenArray* array, char buffer[100], int length, int position, int line, char* start) {
    SLToken token;

    token.start = start;
    token.length = length;
    token.line = line;
    token.position = position;

    // TODO: Add all keywords

    if (strcmp(buffer, "routine") == 0) {
        token.type = T_ROUTINE;

        if (DEBUG) {
            printf("Token Type: ROUTINE, value: %s, line: %d, position: %d\n", buffer, line, position);
        }
    
    } else if (strcmp(buffer, "integer") == 0) {
        token.type = T_INTEGER;

        if (DEBUG) {
            printf("Token Type: INTEGER, value: %s, line: %d, position: %d\n", buffer, line, position);
        }
    } else if (strcmp(buffer, "var") == 0) {
        token.type = T_VAR;

        if (DEBUG) {
            printf("Token Type: VAR, value: %s, line: %d, position: %d\n", buffer, line, position);
        }
    } else if (strcmp(buffer, "for") == 0) {
        token.type = T_FOR;

        if (DEBUG) {
            printf("Token Type: FOR, value: %s, line: %d, position: %d\n", buffer, line, position);
        }
    } else {
        token.type = T_IDENTIFIER;
        token.start = start;
        token.length = length;
        token.line = line;
        token.position = position;

        if (DEBUG) {
            printf("Token Type: IDENTIFIER, value: %s, line: %d, position: %d\n", buffer, line, position);
        }
    }

    SLTokenArrayPush(array, token);
    return true;
}

