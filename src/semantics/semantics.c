#include "semantics.h"

bool SLAnalyzeProgram(SLSemanticState* state, const SLProgram* program) {
    return SLAnalyzeGlobal(state, program) && SLAnalyzeGlobalTypes(state);
}

static bool SLSemanticFail(SLSemanticState* state, SLSpan span,
                           const char* fmt, ...) {
    if (!state->error.has_error) {
        state->error.has_error = true;
        state->error.span = span;

        va_list args;
        va_start(args, fmt);
        vsnprintf(state->error.message, sizeof(state->error.message), fmt, args);
        va_end(args);
    }

    return false;
}

SLSymbol* SLScopeLookupLocal(const SLScope* scope, SLSlice name) {
    return scope != NULL ? SLSymbolTableFind(&scope->symbols, name) : NULL;
}

SLSymbol* SLScopeLookup(const SLScope* scope, SLSlice name) {
    for (; scope != NULL; scope = scope->parent) {
        SLSymbol* symbol = SLScopeLookupLocal(scope, name);
        if (symbol != NULL) return symbol;
    }

    return NULL;
}

bool SLAnalyzeGlobal(SLSemanticState* state, const SLProgram* program) {
    if (state == NULL) return false;
    if (state->error.has_error) return false;

    if (program == NULL) {
        return SLSemanticFail(state, (SLSpan){0}, "no program to analyze");
    }

    if (state->global_scope == NULL) {
        state->global_scope = calloc(1, sizeof(*state->global_scope));
        if (state->global_scope == NULL) {
            return SLSemanticFail(state, (SLSpan){0}, "out of memory");
        }
    }

    state->current_scope = state->global_scope;

    SLSymbolTable* table = &state->global_scope->symbols;

    for (size_t i = 0; i < program->decls.count; ++i) {
        SLDecl* decl = program->decls.items[i];
        SLToken* name;
        SLSymbolKind kind;

        switch (decl->kind) {
            case SL_DECL_VAR:
                name = decl->value.var_decl.name;
                kind = SL_SYMBOL_VAR;
                break;
            case SL_DECL_TYPE:
                name = decl->value.type_decl.name;
                kind = SL_SYMBOL_TYPE;
                break;
            case SL_DECL_ROUTINE:
                name = decl->value.routine.name;
                kind = SL_SYMBOL_ROUTINE;
                break;
            default:
                return SLSemanticFail(state, decl->span,
                                      "invalid global declaration");
        }

        SLSymbol* existing = SLSymbolTableFind(table, name->value);
        if (existing != NULL) {
            
            if (kind == SL_SYMBOL_ROUTINE
                && existing->kind == SL_SYMBOL_ROUTINE
                && existing->declaration != NULL
                && existing->declaration->value.routine.body_kind
                    == SL_ROUTINE_FORWARD
                && existing->definition == NULL
                && decl->value.routine.body_kind != SL_ROUTINE_FORWARD) {
                existing->definition = decl;
                continue;
            }

            return SLSemanticFail(state, decl->span,
                                  "duplicate declaration of '%.*s'",
                                  (int) name->value.length, name->value.start);
        }

        SLSymbol* symbol = calloc(1, sizeof(*symbol));
        if (symbol == NULL) {
            return SLSemanticFail(state, decl->span, "out of memory");
        }

        symbol->name = name->value;
        symbol->kind = kind;
        symbol->span = decl->span;
        symbol->is_mutable = kind == SL_SYMBOL_VAR;
        symbol->declaration = decl;
        
        if (kind == SL_SYMBOL_ROUTINE
            && decl->value.routine.body_kind != SL_ROUTINE_FORWARD) {
            symbol->definition = decl;
        }

        if (!SLSymbolTablePush(table, symbol)) {
            free(symbol);
            return SLSemanticFail(state, decl->span, "out of memory");
        }
    }

    return true;
}

static SLSemType* SLPrimitiveType(SLSemanticState* state, SLSemTypeKind kind) {
    state->primitive_types[kind].kind = kind;
    return &state->primitive_types[kind];
}

SLSemType* SLResolveType(SLSemanticState* state, const SLType* type) {
    if (state == NULL || state->error.has_error) return NULL;

    if (type == NULL) {
        SLSemanticFail(state, (SLSpan){0}, "missing type annotation");
        return NULL;
    }

    switch (type->kind) {
        case SL_TYPE_PRIMITIVE:
            switch (type->value.primitive->type) {
                case SL_TOK_INTEGER:
                    return SLPrimitiveType(state, SL_SEM_INTEGER);
                case SL_TOK_REAL:
                    return SLPrimitiveType(state, SL_SEM_REAL);
                case SL_TOK_BOOLEAN:
                    return SLPrimitiveType(state, SL_SEM_BOOLEAN);
                default:
                    SLSemanticFail(state, type->span, "invalid primitive type");
                    return NULL;
            }

        case SL_TYPE_NAMED: {
            SLSlice name = type->value.name->value;
            SLScope* owner = state->current_scope;
            SLSymbol* symbol = NULL;
            for (; owner != NULL; owner = owner->parent) {
                symbol = SLScopeLookupLocal(owner, name);
                if (symbol != NULL) break;
            }

            if (symbol == NULL) {
                SLSemanticFail(state, type->span, "unknown type '%.*s'",
                               (int) name.length, name.start);
                return NULL;
            }
            if (symbol->kind != SL_SYMBOL_TYPE) {
                SLSemanticFail(state, type->span, "'%.*s' is not a type",
                               (int) name.length, name.start);
                return NULL;
            }
            if (symbol->resolution_state == SL_TYPE_RESOLVING) {
                SLSemanticFail(state, type->span, "cyclic type alias '%.*s'",
                               (int) name.length, name.start);
                return NULL;
            }
            if (symbol->type != NULL) return symbol->type;
            if (symbol->declaration == NULL) {
                SLSemanticFail(state, type->span, "type '%.*s' has no declaration",
                               (int) name.length, name.start);
                return NULL;
            }

            // Resolve the alias where it was declared, not where it is used.
            SLScope* saved_scope = state->current_scope;
            state->current_scope = owner;

            symbol->resolution_state = SL_TYPE_RESOLVING;

            symbol->type = SLResolveType(
                state, symbol->declaration->value.type_decl.type);

            symbol->resolution_state = symbol->type != NULL
                ? SL_TYPE_RESOLVED : SL_TYPE_UNRESOLVED;
            state->current_scope = saved_scope;
            
            return symbol->type;
        }

        case SL_TYPE_ARRAY:
            SLSemanticFail(state, type->span,
                           "array type resolution is not implemented yet");
            return NULL;
        case SL_TYPE_RECORD:
            SLSemanticFail(state, type->span,
                           "record type resolution is not implemented yet");
            return NULL;
        default:
            SLSemanticFail(state, type->span, "invalid type");
            return NULL;
    }
}

static void SLFreeSignature(SLRoutineSignature* signature) {
    if (signature == NULL) return;
    free(signature->parameter_types);
    free(signature);
}

static SLRoutineSignature* SLResolveSignature(SLSemanticState* state,
                                             const SLDecl* decl) {
    SLRoutineSignature* signature = calloc(1, sizeof(*signature));
    if (signature == NULL) {
        SLSemanticFail(state, decl->span, "out of memory");
        return NULL;
    }

    const SLParamArray* params = &decl->value.routine.params;
    signature->parameter_count = params->count;
    if (params->count != 0) {
        signature->parameter_types = calloc(
            params->count, sizeof(*signature->parameter_types));
        if (signature->parameter_types == NULL) {
            SLSemanticFail(state, decl->span, "out of memory");
            goto fail;
        }
    }

    for (size_t i = 0; i < params->count; ++i) {
        SLSlice name = params->items[i].name->value;
        for (size_t j = 0; j < i; ++j) {
            SLSlice previous = params->items[j].name->value;
            if (name.length == previous.length
                && memcmp(name.start, previous.start, name.length) == 0) {
                SLSemanticFail(state, params->items[i].span,
                               "duplicate parameter '%.*s'",
                               (int) name.length, name.start);
                goto fail;
            }
        }

        signature->parameter_types[i] = SLResolveType(state, params->items[i].type);
        if (signature->parameter_types[i] == NULL) goto fail;
    }

    signature->return_type = decl->value.routine.return_type != NULL
        ? SLResolveType(state, decl->value.routine.return_type)
        : SLPrimitiveType(state, SL_SEM_VOID);
    if (signature->return_type == NULL) goto fail;

    return signature;

fail:
    SLFreeSignature(signature);
    return NULL;
}

static bool SLCheckSignature(SLSemanticState* state, const SLSymbol* symbol,
                             const SLRoutineSignature* definition) {
    const SLRoutineSignature* declaration = symbol->signature;
    SLSpan span = symbol->definition->span;

    if (declaration->parameter_count != definition->parameter_count) {
        return SLSemanticFail(state, span,
                              "parameter count mismatch for routine '%.*s'",
                              (int) symbol->name.length, symbol->name.start);
    }

    for (size_t i = 0; i < declaration->parameter_count; ++i) {
        if (declaration->parameter_types[i] != definition->parameter_types[i]) {
            return SLSemanticFail(state,
                symbol->definition->value.routine.params.items[i].span,
                "type mismatch for parameter %zu of routine '%.*s'",
                i + 1, (int) symbol->name.length, symbol->name.start);
        }
    }

    if (declaration->return_type != definition->return_type) {
        return SLSemanticFail(state, span,
                              "return type mismatch for routine '%.*s'",
                              (int) symbol->name.length, symbol->name.start);
    }
    return true;
}

bool SLAnalyzeGlobalTypes(SLSemanticState* state) {
    if (state == NULL || state->error.has_error) return false;
    if (state->global_scope == NULL) {
        return SLSemanticFail(state, (SLSpan){0},
                              "global declarations have not been analyzed");
    }

    SLScope* saved_scope = state->current_scope;
    state->current_scope = state->global_scope;
    SLSymbolTable* table = &state->global_scope->symbols;

    for (size_t i = 0; i < table->count; ++i) {
        SLSymbol* symbol = table->items[i];
        SLDecl* decl = symbol->declaration;
        if (decl == NULL) continue;  // Predefined symbols have no AST declaration.

        if (symbol->kind == SL_SYMBOL_TYPE) {
            SLType reference = {
                .kind = SL_TYPE_NAMED,
                .span = decl->span,
                .value.name = decl->value.type_decl.name,
            };
            if (SLResolveType(state, &reference) == NULL) goto fail;
        } else if (symbol->kind == SL_SYMBOL_VAR) {
            if (decl->value.var_decl.type != NULL) {
                symbol->type = SLResolveType(state, decl->value.var_decl.type);
                if (symbol->type == NULL) goto fail;
            }
        } else if (symbol->kind == SL_SYMBOL_ROUTINE) {
            if (symbol->signature == NULL) {
                symbol->signature = SLResolveSignature(state, decl);
                if (symbol->signature == NULL) goto fail;
            }
            if (symbol->definition != NULL && symbol->definition != decl) {
                SLRoutineSignature* definition = SLResolveSignature(
                    state, symbol->definition);
                if (definition == NULL) goto fail;
                bool matches = SLCheckSignature(state, symbol, definition);
                SLFreeSignature(definition);
                if (!matches) goto fail;
            }
        }
    }

    state->current_scope = saved_scope;
    return true;

fail:
    state->current_scope = saved_scope;
    return false;
}

void SLFreeSemanticState(SLSemanticState* state) {
    if (state == NULL) return;

    if (state->global_scope != NULL) {
        SLSymbolTable* table = &state->global_scope->symbols;
        for (size_t i = 0; i < table->count; ++i) {
            SLFreeSignature(table->items[i]->signature);
            free(table->items[i]);
        }
        
        free(table->items);
        free(state->global_scope);
    }

    *state = (SLSemanticState){0};
}
