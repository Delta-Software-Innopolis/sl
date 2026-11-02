#include "symbol_table.h"

SLSymbol* SLSymbolTableFind(const SLSymbolTable* table, SLSlice name) {
    for (size_t i = 0; i < table->count; ++i) {
        SLSymbol* symbol = table->items[i];
        if (symbol->name.length == name.length
            && memcmp(symbol->name.start, name.start, name.length) == 0) {
            return symbol;
        }
    }

    return NULL;
}

bool SLSymbolTablePush(SLSymbolTable* table, SLSymbol* symbol) {
    if (table->count == table->capacity) {
        size_t new_capacity = table->capacity == 0 ? 64 : table->capacity * 2;

        SLSymbol** new_items =
            realloc(table->items, new_capacity * sizeof(*new_items));

        if (new_items == NULL) {
            return false;
        }

        table->items = new_items;
        table->capacity = new_capacity;
    }

    table->items[table->count++] = symbol;
    return true;
}
