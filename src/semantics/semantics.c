#include "semantics.h"

bool SLAnalyzeProgram(SLSemanticState* state, const SLProgram* program) {
    return SLAnalyzeGlobal(state, program) && SLAnalyzeGlobalTypes(state) && SLAnalyzeGlobalInits(state) && SLAnalyzeRoutines(state);
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

void SLSemanticErrorPrint(const SLSemanticState* state, FILE* out) {
    const SLSemanticError* e = &state->error;
    if (!e->has_error) return;

    const SLCompilerState* cs = state->compiler_state;
    const char* filename = cs != NULL && cs->filename != NULL ? cs->filename : "<input>";
    int line = e->span.begin.line;
    int column = e->span.begin.position;

    fprintf(out, "error: %s\n", e->message);
    if (line < 1) return;
    fprintf(out, "  --> %s:%d:%d\n", filename, line, column);
    if (cs == NULL || cs->text == NULL) return;

    const char* start = cs->text;
    const char* end = cs->text + cs->text_size;
    for (int l = 1; l < line && start < end; ++start) {
        if (*start == '\n') ++l;
    }
    const char* stop = start;
    while (stop < end && *stop != '\n' && *stop != '\r') ++stop;

    fprintf(out, "   | %.*s\n   | ", (int) (stop - start), start);
    for (int i = 1; i < column; ++i) fputc(' ', out);
    int width = e->span.end.line == line ? e->span.end.position - column : 1;
    for (int i = 0; i < (width > 0 ? width : 1); ++i) fputc('^', out);
    fputc('\n', out);
}

static SLScope* SLScopeNew(SLSemanticState* state, SLScope* parent) {
    SLScope* scope = calloc(1, sizeof(*scope));
    if (scope == NULL) {
        SLSemanticFail(state, (SLSpan){0}, "out of memory");
        return NULL;
    }
    scope->parent = parent;
    scope->alloc_next = state->all_scopes;
    state->all_scopes = scope;
    return scope;
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
    if (state->globals_collected) return true;

    if (program == NULL) {
        return SLSemanticFail(state, (SLSpan){0}, "no program to analyze");
    }

    if (state->global_scope == NULL) {
        state->global_scope = SLScopeNew(state, NULL);
        if (state->global_scope == NULL) {
            return SLSemanticFail(state, (SLSpan){0}, "out of memory");
        }
        if (!SLDeclareBuiltins(state)) return false;
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
    state->globals_collected = true;
    return true;
}

static SLSemType* SLPrimitiveType(SLSemanticState* state, SLSemTypeKind kind) {
    state->primitive_types[kind].kind = kind;
    return &state->primitive_types[kind];
}

static bool SLTypesEqual(const SLSemType* a, const SLSemType* b) {
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    return a->kind == b->kind;
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

static const struct {
    const char* name;
    SLSemTypeKind param;
} SL_BUILTIN_PRINTS[] = {
    {"print_integer", SL_SEM_INTEGER},
    {"print_boolean", SL_SEM_BOOLEAN},
    {"print_real", SL_SEM_REAL}
};

static bool SLDeclareBuiltins(SLSemanticState* state) {
    SLSymbolTable* table = &state->global_scope->symbols;
    size_t count = sizeof(SL_BUILTIN_PRINTS) / sizeof(SL_BUILTIN_PRINTS[0]);

    for (size_t i = 0; i < count; ++i) {
        SLSymbol* symbol = calloc(1, sizeof(*symbol));
        SLRoutineSignature* sig = calloc(1, sizeof(*sig));
        SLSemType** params = calloc(1, sizeof(*params));

        if (symbol == NULL || sig == NULL || params == NULL) {
            free(symbol);
            free(sig);
            free(params);
            return SLSemanticFail(state, (SLSpan){0}, "out of memory");
        }

        params[0] = SLPrimitiveType(state, SL_BUILTIN_PRINTS[i].param);
        sig->parameter_types = params;
        sig->parameter_count = 1;
        sig->return_type = SLPrimitiveType(state, SL_SEM_VOID);

        symbol->name.start = (char*) SL_BUILTIN_PRINTS[i].name;
        symbol->name.length = strlen(SL_BUILTIN_PRINTS[i].name);
        symbol->kind = SL_SYMBOL_ROUTINE;
        symbol->signature = sig;

        if (!SLSymbolTablePush(table, symbol)) {
            SLFreeSignature(sig);
            free(symbol);
            return SLSemanticFail(state, (SLSpan){0}, "out of memory");
        }
    }
    return true;
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
        if (!SLTypesEqual(declaration->parameter_types[i],
                          definition->parameter_types[i])) {
            return SLSemanticFail(state,
                symbol->definition->value.routine.params.items[i].span,
                "type mismatch for parameter %zu of routine '%.*s'",
                i + 1, (int) symbol->name.length, symbol->name.start);
        }
    }

    if (!SLTypesEqual(declaration->return_type, definition->return_type)) {
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
        if (decl == NULL) continue;

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
            if (symbol->definition == NULL) {
                SLSemanticFail(state, decl->span,
                               "routine '%.*s' is declared but never defined",
                               (int) symbol->name.length, symbol->name.start);
                goto fail;
            }
        }
    }

    state->current_scope = saved_scope;
    return true;

fail:
    state->current_scope = saved_scope;
    return false;
}

typedef enum {
    SL_OP_UNKNOWN,
    SL_OP_ADD, SL_OP_SUB, SL_OP_MUL, SL_OP_DIV, SL_OP_MOD,
    SL_OP_LT, SL_OP_LE, SL_OP_GT, SL_OP_GE, SL_OP_EQ, SL_OP_NE,
    SL_OP_AND, SL_OP_OR, SL_OP_XOR,
    SL_OP_NEG, SL_OP_POS, SL_OP_NOT
} SLOp;

static SLOp SLBinaryOpKind(const SLToken* op) {
    switch (op->type) {
        case SL_TOK_PLUS:       return SL_OP_ADD;
        case SL_TOK_MINUS:      return SL_OP_SUB;
        case SL_TOK_MULTIPLY:   return SL_OP_MUL;
        case SL_TOK_DIVIDE:     return SL_OP_DIV;
        case SL_TOK_MODULO:     return SL_OP_MOD;
        case SL_TOK_LESS:       return SL_OP_LT;
        case SL_TOK_LESS_EQ:    return SL_OP_LE;
        case SL_TOK_GREATER:    return SL_OP_GT;
        case SL_TOK_GREATER_EQ: return SL_OP_GE;
        case SL_TOK_EQUAL:      return SL_OP_EQ;
        case SL_TOK_NOT_EQ:     return SL_OP_NE;
        case SL_TOK_AND:        return SL_OP_AND;
        case SL_TOK_OR:         return SL_OP_OR;
        case SL_TOK_XOR:        return SL_OP_XOR;
        default:                return SL_OP_UNKNOWN;
    }
}

static SLOp SLUnaryOpKind(const SLToken* op) {
    switch (op->type) {
        case SL_TOK_MINUS: return SL_OP_NEG;
        case SL_TOK_PLUS:  return SL_OP_POS;
        case SL_TOK_NOT:   return SL_OP_NOT;
        default:           return SL_OP_UNKNOWN;
    }
}

static const char* SLTypeName(const SLSemType* t) {
    switch (t->kind) {
        case SL_SEM_INTEGER: return "integer";
        case SL_SEM_REAL:    return "real";
        case SL_SEM_BOOLEAN: return "boolean";
        case SL_SEM_VOID:    return "void";
        default:             return "<unknown>";
    }
}

static bool SLIsNumeric(const SLSemType* t) {
    return t->kind == SL_SEM_INTEGER || t->kind == SL_SEM_REAL;
}

static bool SLConvertible(const SLSemType* to, const SLSemType* from) {
    if (SLTypesEqual(to, from)) return true;
    switch (to->kind) {
        case SL_SEM_REAL:    return from->kind == SL_SEM_INTEGER;
        case SL_SEM_INTEGER: return from->kind == SL_SEM_BOOLEAN;   // true -> 1, false -> 0
        case SL_SEM_BOOLEAN: return from->kind == SL_SEM_INTEGER;   // только 0 и 1
        default:             return false;
    }
}

static bool SLAssignable(const SLSemType* to, const SLSemType* from) {
    return SLConvertible(to, from);
}

static SLSemType* SLCheckValue(SLSemanticState* state, SLExpr* expr) {
    SLSemType* type = SLCheckExpr(state, expr);
    if (type == NULL) return NULL;
    if (type->kind == SL_SEM_VOID) {
        SLSemanticFail(state, expr->span, "expression has no value");
        return NULL;
    }
    return type;
}

static SLSemType* SLCheckBinary(SLSemanticState* state, SLExpr* expr,
                                SLSemType* lt, SLSemType* rt) {
    bool numeric = SLIsNumeric(lt) && SLIsNumeric(rt);

    switch (SLBinaryOpKind(expr->value.binary.op)) {
        case SL_OP_ADD: case SL_OP_SUB: case SL_OP_MUL: case SL_OP_DIV:
            if (numeric) {
                return SLPrimitiveType(state,
                    (lt->kind == SL_SEM_REAL || rt->kind == SL_SEM_REAL)
                        ? SL_SEM_REAL : SL_SEM_INTEGER);
            }
            break;
        case SL_OP_MOD:
            if (lt->kind == SL_SEM_INTEGER && rt->kind == SL_SEM_INTEGER) {
                return lt;
            }
            break;
        case SL_OP_LT: case SL_OP_LE: case SL_OP_GT: case SL_OP_GE:
            if (numeric) return SLPrimitiveType(state, SL_SEM_BOOLEAN);
            break;
        case SL_OP_EQ: case SL_OP_NE:
            if (numeric || (lt->kind == SL_SEM_BOOLEAN
                            && rt->kind == SL_SEM_BOOLEAN)) {
                return SLPrimitiveType(state, SL_SEM_BOOLEAN);
            }
            break;
        case SL_OP_AND: case SL_OP_OR: case SL_OP_XOR:
            if (lt->kind == SL_SEM_BOOLEAN && rt->kind == SL_SEM_BOOLEAN) {
                return lt;
            }
            break;
        default:
            SLSemanticFail(state, expr->span, "unknown binary operator");
            return NULL;
    }

    SLSemanticFail(state, expr->span, "invalid operand types: '%s' and '%s'",
                   SLTypeName(lt), SLTypeName(rt));
    return NULL;
}

static SLSemType* SLCheckCall(SLSemanticState* state, SLExpr* expr) {
    SLToken* callee = expr->value.call.callee;
    SLSlice name = callee->value;
    SLSymbol* symbol = SLScopeLookup(state->current_scope, name);

    if (symbol == NULL) {
        SLSemanticFail(state, expr->span, "unknown routine '%.*s'",
                       (int) name.length, name.start);
        return NULL;
    }
    if (symbol->kind != SL_SYMBOL_ROUTINE || symbol->signature == NULL) {
        SLSemanticFail(state, expr->span, "'%.*s' is not a routine",
                       (int) name.length, name.start);
        return NULL;
    }

    const SLRoutineSignature* sig = symbol->signature;
    SLExprArray* args = &expr->value.call.arguments;

    if (args->count != sig->parameter_count) {
        SLSemanticFail(state, expr->span,
                       "routine '%.*s' expects %zu argument(s), got %zu",
                       (int) name.length, name.start,
                       sig->parameter_count, args->count);
        return NULL;
    }

    for (size_t i = 0; i < args->count; ++i) {
        SLSemType* type = SLCheckValue(state, args->items[i]);
        if (type == NULL) return NULL;
        if (!SLAssignable(sig->parameter_types[i], type)) {
            SLSemanticFail(state, args->items[i]->span,
                           "argument %zu of '%.*s': expected '%s', got '%s'",
                           i + 1, (int) name.length, name.start,
                           SLTypeName(sig->parameter_types[i]), SLTypeName(type));
            return NULL;
        }
    }

    expr->symbol = symbol;
    return sig->return_type;
}

static SLSemType* SLCheckExprInner(SLSemanticState* state, SLExpr* expr) {
    switch (expr->type) {
        case SL_EXPR_INTEGER: return SLPrimitiveType(state, SL_SEM_INTEGER);
        case SL_EXPR_REAL:    return SLPrimitiveType(state, SL_SEM_REAL);
        case SL_EXPR_BOOLEAN: return SLPrimitiveType(state, SL_SEM_BOOLEAN);

        case SL_EXPR_NAME: {
            SLSlice name = expr->value.name->value;
            SLSymbol* symbol = SLScopeLookup(state->current_scope, name);
            if (symbol == NULL) {
                SLSemanticFail(state, expr->span, "unknown name '%.*s'",
                               (int) name.length, name.start);
                return NULL;
            }
            if (symbol->kind == SL_SYMBOL_TYPE
                || symbol->kind == SL_SYMBOL_ROUTINE) {
                SLSemanticFail(state, expr->span, "'%.*s' is not a variable",
                               (int) name.length, name.start);
                return NULL;
            }
            if (symbol->type == NULL) {
                SLSemanticFail(state, expr->span,
                               "type of '%.*s' is not known here",
                               (int) name.length, name.start);
                return NULL;
            }
            expr->symbol = symbol;
            return symbol->type;
        }

        case SL_EXPR_UNARY: {
            SLSemType* type = SLCheckValue(state, expr->value.unary.operand);
            if (type == NULL) return NULL;
            switch (SLUnaryOpKind(expr->value.unary.op)) {
                case SL_OP_NEG: case SL_OP_POS:
                    if (SLIsNumeric(type)) return type;
                    break;
                case SL_OP_NOT:
                    if (type->kind == SL_SEM_BOOLEAN) return type;
                    break;
                default:
                    SLSemanticFail(state, expr->span, "unknown unary operator");
                    return NULL;
            }
            SLSemanticFail(state, expr->span, "invalid operand type '%s'",
                           SLTypeName(type));
            return NULL;
        }

        case SL_EXPR_BINARY: {
            SLSemType* lt = SLCheckValue(state, expr->value.binary.left);
            if (lt == NULL) return NULL;
            SLSemType* rt = SLCheckValue(state, expr->value.binary.right);
            if (rt == NULL) return NULL;
            return SLCheckBinary(state, expr, lt, rt);
        }

        case SL_EXPR_CALL:
            return SLCheckCall(state, expr);

        case SL_EXPR_INDEX:
        case SL_EXPR_FIELD:
            SLSemanticFail(state, expr->span,
                           "indexing and field access are not implemented yet");
            return NULL;
    }
    SLSemanticFail(state, expr->span, "invalid expression");
    return NULL;
}

SLSemType* SLCheckExpr(SLSemanticState* state, SLExpr* expr) {
    if (state == NULL || state->error.has_error) return NULL;
    SLSemType* type = SLCheckExprInner(state, expr);
    expr->sem_type = type;
    return type;
}

SLScope* SLScopePush(SLSemanticState* state) {
    SLScope* scope = SLScopeNew(state, state->current_scope);
    if (scope != NULL) state->current_scope = scope;
    return scope;
}

void SLScopePop(SLSemanticState* state) {
    state->current_scope = state->current_scope->parent;
}

SLSymbol* SLScopeDeclare(SLSemanticState* state, SLSlice name, SLSymbolKind kind,
                         SLSemType* type, SLSpan span, bool is_mutable) {
    if (SLScopeLookupLocal(state->current_scope, name) != NULL) {
        SLSemanticFail(state, span, "duplicate declaration of '%.*s'",
                       (int) name.length, name.start);
        return NULL;
    }
    SLSymbol* symbol = calloc(1, sizeof(*symbol));
    if (symbol == NULL) {
        SLSemanticFail(state, span, "out of memory");
        return NULL;
    }
    symbol->name = name;
    symbol->kind = kind;
    symbol->type = type;
    symbol->span = span;
    symbol->is_mutable = is_mutable;
    if (!SLSymbolTablePush(&state->current_scope->symbols, symbol)) {
        free(symbol);
        SLSemanticFail(state, span, "out of memory");
        return NULL;
    }
    return symbol;
}

static bool SLCheckBody(SLSemanticState* state, SLBody* body);

static bool SLCheckScopedBody(SLSemanticState* state, SLBody* body) {
    if (SLScopePush(state) == NULL) return false;
    bool ok = SLCheckBody(state, body);
    SLScopePop(state);
    return ok;
}

static bool SLCheckCondition(SLSemanticState* state, SLExpr* cond) {
    SLSemType* type = SLCheckValue(state, cond);
    if (type == NULL) return false;
    if (!SLConvertible(SLPrimitiveType(state, SL_SEM_BOOLEAN), type)) {
        return SLSemanticFail(state, cond->span,
                              "condition must be boolean, got '%s'",
                              SLTypeName(type));
    }
    return true;
}

bool SLCheckStmt(SLSemanticState* state, SLStmt* stmt) {
    if (state == NULL || state->error.has_error) return false;

    switch (stmt->kind) {
        case SL_STMT_ASSIGN: {
            SLExpr* target = stmt->value.assign.target;
            SLSemType* tt = SLCheckValue(state, target);
            if (tt == NULL) return false;
            if (target->type == SL_EXPR_NAME && !target->symbol->is_mutable) {
            const char* what = target->symbol->kind == SL_SYMBOL_ITERATOR ? "loop variable " : "";
            return SLSemanticFail(state, target->span, "cannot assign to %s'%.*s'", what,
                          (int) target->symbol->name.length,
                          target->symbol->name.start);
            }
            SLSemType* vt = SLCheckValue(state, stmt->value.assign.value);
            if (vt == NULL) return false;
            if (!SLAssignable(tt, vt)) {
                return SLSemanticFail(state, stmt->span,
                                      "cannot assign '%s' to '%s'",
                                      SLTypeName(vt), SLTypeName(tt));
            }
            return true;
        }

        case SL_STMT_CALL:
            return SLCheckExpr(state, stmt->value.call) != NULL;

        case SL_STMT_WHILE:
            return SLCheckCondition(state, stmt->value.while_loop.condition)
                && SLCheckScopedBody(state, &stmt->value.while_loop.body);

        case SL_STMT_IF:
            if (!SLCheckCondition(state, stmt->value.if_stmt.condition)) {
                return false;
            }
            if (!SLCheckScopedBody(state, &stmt->value.if_stmt.then_body)) {
                return false;
            }
            return !stmt->value.if_stmt.has_else
                || SLCheckScopedBody(state, &stmt->value.if_stmt.else_body);

        case SL_STMT_FOR: {
            SLExpr* from = stmt->value.for_loop.from;
            SLExpr* to = stmt->value.for_loop.to;
            if (to == NULL) {
                return SLSemanticFail(state, stmt->span,
                                      "iteration over arrays is not implemented yet");
            }
            SLSemType* ft = SLCheckValue(state, from);
            SLSemType* tt = ft != NULL ? SLCheckValue(state, to) : NULL;
            if (ft == NULL || tt == NULL) return false;
            if (ft->kind != SL_SEM_INTEGER || tt->kind != SL_SEM_INTEGER) {
                return SLSemanticFail(state, stmt->span,
                                      "for range bounds must be integer");
            }
            if (SLScopePush(state) == NULL) return false;
            bool ok = SLScopeDeclare(state, stmt->value.for_loop.iterator->value,
                                     SL_SYMBOL_ITERATOR, ft, stmt->span, false)
                      != NULL
                   && SLCheckBody(state, &stmt->value.for_loop.body);
            SLScopePop(state);
            return ok;
        }

        case SL_STMT_RETURN: {
            SLExpr* value = stmt->value.return_value;
            SLSemType* expected = state->current_return_type;
            if (value == NULL) {
                if (expected->kind != SL_SEM_VOID) {
                    return SLSemanticFail(state, stmt->span,
                                          "routine must return a value of type '%s'",
                                          SLTypeName(expected));
                }
                return true;
            }
            if (expected->kind == SL_SEM_VOID) {
                return SLSemanticFail(state, stmt->span,
                                      "routine has no return type");
            }
            SLSemType* type = SLCheckValue(state, value);
            if (type == NULL) return false;
            if (!SLAssignable(expected, type)) {
                return SLSemanticFail(state, value->span,
                                      "cannot return '%s' from routine returning '%s'",
                                      SLTypeName(type), SLTypeName(expected));
            }
            return true;
        }
    }
    return SLSemanticFail(state, stmt->span, "invalid statement");
}

static SLSemType* SLTypeOfVarDecl(SLSemanticState* state, const SLDecl* decl,
                                  SLSemType* annotated) {
    SLToken* name = decl->value.var_decl.name;
    SLExpr* init = decl->value.var_decl.init;
    SLSemType* type = annotated;

    if (init != NULL) {
        SLSemType* init_type = SLCheckValue(state, init);
        if (init_type == NULL) return NULL;

        if (type == NULL) {
            type = init_type;
        } else if (!SLAssignable(type, init_type)) {
            SLSemanticFail(state, decl->span,
                "cannot initialize '%.*s' of type '%s' with '%s'",
                (int) name->value.length, name->value.start,
                SLTypeName(type), SLTypeName(init_type));
            return NULL;
        }
    }

    if (type == NULL) {
        SLSemanticFail(state, decl->span,
            "variable '%.*s' needs a type or an initializer",
            (int) name->value.length, name->value.start);
        return NULL;
    }
    return type;
}

static bool SLCheckLocalDecl(SLSemanticState* state, SLDecl* decl) {
    switch (decl->kind) {
        case SL_DECL_VAR: {
            SLToken* name = decl->value.var_decl.name;
            SLSemType* annotated = NULL;

            if (decl->value.var_decl.type != NULL) {
                annotated = SLResolveType(state, decl->value.var_decl.type);
                if (annotated == NULL) return false;
            }

            SLSemType* type = SLTypeOfVarDecl(state, decl, annotated);
            if (type == NULL) return false;

            SLSymbol* symbol = SLScopeDeclare(state, name->value, SL_SYMBOL_VAR,
                                              type, decl->span, true);
            if (symbol == NULL) return false;
            symbol->declaration = decl;
            return true;
        }

        case SL_DECL_TYPE: {
            SLToken* name = decl->value.type_decl.name;
            SLSymbol* symbol = SLScopeDeclare(state, name->value, SL_SYMBOL_TYPE,
                                              NULL, decl->span, false);
            if (symbol == NULL) return false;
            symbol->declaration = decl;
            SLType reference = {
                .kind = SL_TYPE_NAMED,
                .span = decl->span,
                .value.name = name,
            };
            return SLResolveType(state, &reference) != NULL;
        }

        case SL_DECL_ROUTINE:
            return SLSemanticFail(state, decl->span,
                                  "nested routines are not supported");
    }
    return SLSemanticFail(state, decl->span, "invalid declaration");
}

bool SLAnalyzeGlobalInits(SLSemanticState* state) {
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
        if (symbol->kind != SL_SYMBOL_VAR || symbol->declaration == NULL) continue;

        SLSemType* type = SLTypeOfVarDecl(state, symbol->declaration, symbol->type);
        if (type == NULL) {
            state->current_scope = saved_scope;
            return false;
        }
        symbol->type = type;
    }

    state->current_scope = saved_scope;
    return true;
}

static bool SLCheckBody(SLSemanticState* state, SLBody* body) {
    for (size_t i = 0; i < body->count; ++i) {
        SLBodyItem* item = &body->items[i];
        bool ok = item->kind == SL_ITEM_DECL
            ? SLCheckLocalDecl(state, item->value.decl)
            : SLCheckStmt(state, item->value.stmt);
        if (!ok) return false;
    }
    return true;
}

static bool SLAnalyzeRoutine(SLSemanticState* state, SLSymbol* symbol) {
    SLDecl* decl = symbol->definition;
    const SLRoutineSignature* sig = symbol->signature;
    SLParamArray* params = &decl->value.routine.params;

    if (SLScopePush(state) == NULL) return false;
    state->current_routine = decl;
    state->current_return_type = sig->return_type;

    bool ok = true;
    for (size_t i = 0; ok && i < params->count; ++i) {
        ok = SLScopeDeclare(state, params->items[i].name->value, SL_SYMBOL_PARAM,
                            sig->parameter_types[i], params->items[i].span, true)
             != NULL;
    }

    if (ok) {
        if (decl->value.routine.body_kind == SL_ROUTINE_BLOCK) {
            ok = SLCheckBody(state, &decl->value.routine.body);
        } else {
            SLExpr* body = decl->value.routine.expr_body;
            SLSemType* type = SLCheckExpr(state, body);
            ok = type != NULL;
            if (ok && !SLAssignable(sig->return_type, type)) {
                ok = SLSemanticFail(state, body->span,
                        "routine body has type '%s', expected '%s'",
                        SLTypeName(type), SLTypeName(sig->return_type));
            }
        }
    }

    state->current_routine = NULL;
    state->current_return_type = NULL;
    SLScopePop(state);
    return ok;
}

bool SLAnalyzeRoutines(SLSemanticState* state) {
    if (state == NULL || state->error.has_error) return false;
    state->current_scope = state->global_scope;

    SLSymbolTable* table = &state->global_scope->symbols;
    for (size_t i = 0; i < table->count; ++i) {
        SLSymbol* symbol = table->items[i];
        if (symbol->kind != SL_SYMBOL_ROUTINE || symbol->definition == NULL) {
            continue;
        }
        if (!SLAnalyzeRoutine(state, symbol)) return false;
    }
    return true;
}

void SLFreeSemanticState(SLSemanticState* state) {
    if (state == NULL) return;

    for (SLScope* scope = state->all_scopes; scope != NULL;) {
        SLScope* next = scope->alloc_next;
        for (size_t i = 0; i < scope->symbols.count; ++i) {
            SLFreeSignature(scope->symbols.items[i]->signature);
            free(scope->symbols.items[i]);
        }
        free(scope->symbols.items);
        free(scope);
        scope = next;
    }

    *state = (SLSemanticState){0};
}
