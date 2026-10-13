#include "parser.h"

static _Thread_local const char* prefix = "";
static _Thread_local const char* branch = "";

static void SLFreeType(SLType* type);
static void SLFreeDecl(SLDecl* decl);
static void SLFreeStmt(SLStmt* stmt);

static void SLErrorSetLocation(SLTokenReader* r, SLToken* at) {
    SLParseError* e = &r->error;

    if (at != NULL) {
        e->at = at->value.start;
        e->length = at->value.length;
        e->position = at->position;
    } else if (r->tokens->count > 0) {
        SLToken* last = &r->tokens->items[r->tokens->count - 1];
        e->at = last->value.start != NULL
            ? last->value.start + last->value.length : NULL;
        e->length = 1;
        e->position = last->position;
        e->position.position += (int) last->value.length;
    } else {
        e->at = NULL;
        e->length = 1;
        e->position = (SLTokenPosition) {0};
    }
}

void SLErrorRaise(SLTokenReader* r, SLToken* at, const char* file, int line,
                  const char* func, const char* fmt, ...) {
    SLParseError* e = &r->error;
    if (e->has_error) return;

    e->has_error = true;
    e->trace_count = 0;
    e->trace_truncated = false;

    va_list args;
    va_start(args, fmt);
    vsnprintf(e->message, sizeof e->message, fmt, args);
    va_end(args);

    SLErrorSetLocation(r, at);
    SLErrorTrace(r, file, line, func);
}

void SLErrorExpected(SLTokenReader* r, SLToken* found, const char* expected,
                     const char* file, int line, const char* func) {
    if (found == NULL) {
        SLErrorRaise(r, NULL, file, line, func,
                     "expected %s, found end of input", expected);
    } else if (found->type == SL_TOK_NEWLINE) {
        SLErrorRaise(r, found, file, line, func,
                     "expected %s, found end of line", expected);
    } else if (found->value.start == NULL || found->value.length == 0) {
        SLErrorRaise(r, found, file, line, func,
                     "expected %s, found unexpected token", expected);
    } else {
        SLErrorRaise(r, found, file, line, func, "expected %s, found '%.*s'",
                     expected, (int) found->value.length, found->value.start);
    }
}

void SLErrorTrace(SLTokenReader* r, const char* file, int line, const char* func) {
    SLParseError* e = &r->error;

    if (!e->has_error) {

        SLToken* cur = r->cursor < r->tokens->count
            ? &r->tokens->items[r->cursor] : NULL;
        SLErrorRaise(r, cur, file, line, func,
                     "internal parser error: failure without diagnostic");
        return;
    }

    if (e->trace_count < SL_ERROR_MAX_TRACE) {
        e->trace[e->trace_count++] = (SLTraceFrame) {file, func, line};
    } else {
        e->trace_truncated = true;
    }
}

void SLErrorPrint(const SLParseError* e, FILE* out, const char* filename,
                  const char* source, size_t source_len) {
    if (!e->has_error) return;
    if (filename == NULL) filename = "<input>";

    fprintf(out, "error: %s\n", e->message);

    uintptr_t at = (uintptr_t) e->at;
    uintptr_t base = (uintptr_t) source;
    bool located = source != NULL && e->at != NULL
                && at >= base && at <= base + source_len;

    if (located) {
        size_t offset = (size_t) (at - base);

        size_t line_start = offset;
        while (line_start > 0 && source[line_start - 1] != '\n') line_start--;
        size_t line_end = offset;
        while (line_end < source_len && source[line_end] != '\n'
               && source[line_end] != '\r') line_end++;
        size_t line_no = 1;
        for (size_t i = 0; i < line_start; ++i) if (source[i] == '\n') line_no++;

        fprintf(out, "  --> %s:%zu:%zu\n", filename, line_no, offset - line_start + 1);
        fprintf(out, "   | %.*s\n", (int) (line_end - line_start), source + line_start);

        fputs("   | ", out);
        for (size_t i = line_start; i < offset; ++i)
            fputc(source[i] == '\t' ? '\t' : ' ', out);
        size_t carets = e->length > 0 ? e->length : 1;
        if (line_end > offset && offset + carets > line_end) carets = line_end - offset;
        for (size_t i = 0; i < carets; ++i) fputc('^', out);
        fputc('\n', out);
    } else {
        fprintf(out, "  --> %s:%d:%d\n", filename, e->position.line, e->position.position);
    }

    fputs("\nParser traceback (most recent call last):\n", out);
    if (e->trace_truncated) fputs("  ...\n", out);
    for (size_t i = e->trace_count; i > 0; --i) {
        const SLTraceFrame* f = &e->trace[i - 1];
        fprintf(out, "  in %s (%s:%d)%s\n", f->func, f->file, f->line,
                i == 1 ? "   <-- raised here" : "");
    }
}

static void SLFreeExpr(SLExpr* expr) {
    if (expr == NULL) return;

    switch (expr->type) {
        case SL_EXPR_UNARY:
            SLFreeExpr(expr->value.unary.operand);
            break;
        case SL_EXPR_BINARY:
            SLFreeExpr(expr->value.binary.left);
            SLFreeExpr(expr->value.binary.right);
            break;
        case SL_EXPR_CALL:
            for (size_t i = 0; i < expr->value.call.arguments.count; ++i) {
                SLFreeExpr(expr->value.call.arguments.items[i]);
            }
            free(expr->value.call.arguments.items);
            break;
        case SL_EXPR_INDEX:
            SLFreeExpr(expr->value.index.base);
            SLFreeExpr(expr->value.index.index);
            break;
        case SL_EXPR_FIELD:
            SLFreeExpr(expr->value.field.base);
            break;
        default:
            break;
    }

    free(expr);
}

static void SLFreeDeclArray(SLDeclArray* array) {
    for (size_t i = 0; i < array->count; ++i) {
        SLFreeDecl(array->items[i]);
    }
    free(array->items);
    *array = (SLDeclArray){0};
}

static void SLFreeBody(SLBody* body) {
    for (size_t i = 0; i < body->count; ++i) {
        if (body->items[i].kind == SL_ITEM_DECL) {
            SLFreeDecl(body->items[i].value.decl);
        } else {
            SLFreeStmt(body->items[i].value.stmt);
        }
    }
    free(body->items);
    *body = (SLBody){0};
}

static void SLFreeType(SLType* type) {
    if (type == NULL) return;

    switch (type->kind) {
        case SL_TYPE_ARRAY:
            SLFreeExpr(type->value.array.size);
            SLFreeType(type->value.array.element);
            break;
        case SL_TYPE_RECORD:
            SLFreeDeclArray(&type->value.record.fields);
            break;
        default:
            break;
    }

    free(type);
}

static void SLFreeDecl(SLDecl* decl) {
    if (decl == NULL) return;

    switch (decl->kind) {
        case SL_DECL_VAR:
            SLFreeType(decl->value.var_decl.type);
            SLFreeExpr(decl->value.var_decl.init);
            break;
        case SL_DECL_TYPE:
            SLFreeType(decl->value.type_decl.type);
            break;
        case SL_DECL_ROUTINE:
            for (size_t i = 0; i < decl->value.routine.params.count; ++i) {
                SLFreeType(decl->value.routine.params.items[i].type);
            }
            free(decl->value.routine.params.items);
            SLFreeType(decl->value.routine.return_type);
            SLFreeBody(&decl->value.routine.body);
            SLFreeExpr(decl->value.routine.expr_body);
            break;
    }

    free(decl);
}

static void SLFreeStmt(SLStmt* stmt) {
    if (stmt == NULL) return;

    switch (stmt->kind) {
        case SL_STMT_ASSIGN:
            SLFreeExpr(stmt->value.assign.target);
            SLFreeExpr(stmt->value.assign.value);
            break;
        case SL_STMT_CALL:
            SLFreeExpr(stmt->value.call);
            break;
        case SL_STMT_WHILE:
            SLFreeExpr(stmt->value.while_loop.condition);
            SLFreeBody(&stmt->value.while_loop.body);
            break;
        case SL_STMT_FOR:
            SLFreeExpr(stmt->value.for_loop.from);
            SLFreeExpr(stmt->value.for_loop.to);
            SLFreeBody(&stmt->value.for_loop.body);
            break;
        case SL_STMT_IF:
            SLFreeExpr(stmt->value.if_stmt.condition);
            SLFreeBody(&stmt->value.if_stmt.then_body);
            SLFreeBody(&stmt->value.if_stmt.else_body);
            break;
        case SL_STMT_RETURN:
            SLFreeExpr(stmt->value.return_value);
            break;
    }

    free(stmt);
}

void SLFreeProgram(SLProgram* program) {
    if (program == NULL) return;
    SLFreeDeclArray(&program->decls);
    free(program);
}

bool SLExprArrayPush(SLExprArray* list, SLExpr* expr) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 64 : list->capacity * 2;

        SLExpr** new_items =
            realloc(list->items, new_capacity * sizeof(*new_items));

        if (new_items == NULL) {
            return false;
        }

        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count++] = expr;
    return true;
}

SLTokenReader* SLInitTokenReader(SLCompilerState* state) {
    if (state == NULL) return NULL;

    SLTokenReader* reader = (SLTokenReader*) calloc(1, sizeof(SLTokenReader));
    if (reader == NULL) return NULL;

    reader->tokens = &state->tokens;
    reader->cursor = 0;
    reader->isEOF = false;

    return reader;
}

static void SLSkipNewlines(SLTokenReader* reader) {
    while ((size_t) reader->cursor < reader->tokens->count
           && reader->tokens->items[reader->cursor].type == SL_TOK_NEWLINE) {
        reader->cursor++;
    }
}

SLToken* SLGetNextToken(SLTokenReader* reader) {
    if (reader == NULL) return NULL;

    SLSkipNewlines(reader);
    if (reader->cursor >= reader->tokens->count) {
        reader->isEOF = true;
        return NULL;
    }

    return &reader->tokens->items[reader->cursor++];
}

static SLToken* SLPeekToken(SLTokenReader* reader) {
    if (reader == NULL) return NULL;
    SLSkipNewlines(reader);
    if ((size_t) reader->cursor >= reader->tokens->count) return NULL;
    return &reader->tokens->items[reader->cursor];
}

static SLToken* SLMatchToken(SLTokenReader* reader, SLTokenType type) {
    SLToken* tok = SLPeekToken(reader);
    if (tok == NULL || tok->type != type) return NULL;
    reader->cursor++;
    return tok;
}

static SLToken* SLRequireToken(SLTokenReader* reader, SLTokenType type,
                               const char* what, const char* file, int line,
                               const char* func) {
    SLToken* tok = SLMatchToken(reader, type);
    if (tok == NULL) {
        SLErrorExpected(reader, SLPeekToken(reader), what, file, line, func);
    }
    return tok;
}

#define SL_REQUIRE(r, type, what) \
    SLRequireToken((r), (type), (what), __FILE__, __LINE__, __func__)

static void SLSkipSemicolons(SLTokenReader* reader) {
    while (SLMatchToken(reader, SL_TOK_SEMICOLON) != NULL) {}
}

static SLTokenPosition SLTokenEnd(const SLToken* tok) {
    SLTokenPosition end = tok->position;
    end.position += (int) tok->value.length;
    return end;
}

static bool SLStartsExpr(const SLToken* tok) {
    if (tok == NULL) return false;
    switch (tok->type) {
        case SL_TOK_INTEGER_LIT:
        case SL_TOK_REAL_LIT:
        case SL_TOK_TRUE:
        case SL_TOK_FALSE:
        case SL_TOK_IDENTIFIER:
        case SL_TOK_LPAR:
        case SL_TOK_PLUS:
        case SL_TOK_MINUS:
        case SL_TOK_NOT:
            return true;
        default:
            return false;
    }
}

#define SL_DEFINE_PUSH(Name, ArrayType, ItemType)                           \
    static bool Name(ArrayType* list, ItemType item) {                      \
        if (list->count == list->capacity) {                                \
            size_t new_capacity = list->capacity == 0                       \
                ? 8 : list->capacity * 2;                                   \
            ItemType* new_items =                                           \
                realloc(list->items, new_capacity * sizeof(*new_items));    \
            if (new_items == NULL) return false;                            \
            list->items = new_items;                                        \
            list->capacity = new_capacity;                                  \
        }                                                                   \
        list->items[list->count++] = item;                                  \
        return true;                                                        \
    }

SL_DEFINE_PUSH(SLDeclArrayPush, SLDeclArray, SLDecl*)
SL_DEFINE_PUSH(SLParamArrayPush, SLParamArray, SLParam)
SL_DEFINE_PUSH(SLBodyPush, SLBody, SLBodyItem)

// 2 + 2 * 2

SLExpr* SLParseExpr(SLTokenReader* reader) {
    SLExpr* relation = SLParseRelation(reader);

    if (relation == NULL) {
        SL_TRACE(reader);
        return NULL;
    }

    SLToken* logical_op = SLGetNextToken(reader);

    if (logical_op == NULL) {
        return relation;
    }

    if (logical_op->type != SL_TOK_AND
        && logical_op->type != SL_TOK_OR
        && logical_op->type != SL_TOK_XOR) {
            reader->cursor--;
            return relation;
    }

    SLExpr* second_rel = SLParseExpr(reader);

    if (second_rel == NULL) {
        SL_TRACE(reader);
        SLFreeExpr(relation);
        return NULL;
    }

    SLExpr* ret = (SLExpr*) malloc(sizeof(SLExpr));
    ret->type = SL_EXPR_BINARY;
    ret->value.binary.left = relation;
    ret->value.binary.op = logical_op;
    ret->value.binary.right = second_rel;

    ret->span.begin = relation->span.begin;
    ret->span.end = second_rel->span.end;

    return ret;
}


SLExpr* SLParseRelation(SLTokenReader* reader) {
    SLExpr* simple = SLParseSimple(reader);

    if (simple == NULL) {
        SL_TRACE(reader);
        return NULL;
    }

    SLToken* comparator = SLGetNextToken(reader);

    if (comparator == NULL) {
        return simple;
    }

    if (comparator->type != SL_TOK_LESS
        && comparator->type != SL_TOK_LESS_EQ
        && comparator->type != SL_TOK_GREATER
        && comparator->type != SL_TOK_GREATER_EQ
        && comparator->type != SL_TOK_EQUAL
        && comparator->type != SL_TOK_NOT_EQ) {
            reader->cursor--;
            return simple;
    }

    SLExpr* second_simple = SLParseSimple(reader);

    if (second_simple == NULL) {
        SL_TRACE(reader);
        SLFreeExpr(simple);

        return NULL;
    }

    SLExpr* ret = (SLExpr*) malloc(sizeof(SLExpr));
    ret->type = SL_EXPR_BINARY;

    ret->value.binary.left = simple;
    ret->value.binary.op = comparator;
    ret->value.binary.right = second_simple;

    ret->span.begin = simple->span.begin;
    ret->span.end = second_simple->span.end;

    return ret;
}

SLExpr* SLParseSimple(SLTokenReader* reader) {
    SLExpr* factor = SLParseFactor(reader);

    if (factor == NULL) {
        SL_TRACE(reader);
        return NULL;
    }

    SLExpr* ret = NULL;

    while (true) {
        SLToken* operator = SLGetNextToken(reader);

        if (operator == NULL) {
            return ret == NULL ? factor : ret;
        }

        if (operator->type != SL_TOK_PLUS
            && operator->type != SL_TOK_MINUS) {
                reader->cursor--;
                return ret == NULL ? factor : ret;
        }

        SLExpr* second_factor = SLParseFactor(reader);

        if (second_factor == NULL) {
            SL_TRACE(reader);
            SLFreeExpr(ret == NULL ? factor : ret);

            return NULL;
        }

        SLExpr* newLeftPart = (SLExpr*) malloc(sizeof(SLExpr));
        newLeftPart->type = SL_EXPR_BINARY;

        newLeftPart->value.binary.left = ret == NULL ? factor : ret;
        newLeftPart->value.binary.op = operator;
        newLeftPart->value.binary.right = second_factor;

        newLeftPart->span.begin = factor->span.begin;
        newLeftPart->span.end = second_factor->span.end;

        ret = newLeftPart;

    }

}

SLExpr* SLParseFactor(SLTokenReader* reader) {
    SLExpr* summand = SLParseSummand(reader);

    if (summand == NULL) {
        SL_TRACE(reader);
        return NULL;
    }

    SLExpr* ret = NULL;

    while (true) {
        SLToken* operator = SLGetNextToken(reader);

        if (operator == NULL) {
            if (ret == NULL) {
                return summand;
            } else {
                return ret;
            }
        }

        if (operator->type != SL_TOK_MULTIPLY
            && operator->type != SL_TOK_DIVIDE
            && operator->type != SL_TOK_MODULO) {
                reader->cursor--;
                return ret == NULL ? summand : ret;
        }

        SLExpr* second_summand = SLParseSummand(reader);

        if (second_summand == NULL) {
            SL_TRACE(reader);
            SLFreeExpr(ret == NULL ? summand : ret);

            return NULL;
        }

        SLExpr* newLeftPart = (SLExpr*) malloc(sizeof(SLExpr));
        newLeftPart->type = SL_EXPR_BINARY;

        newLeftPart->value.binary.left = ret == NULL ? summand : ret;
        newLeftPart->value.binary.op = operator;
        newLeftPart->value.binary.right = second_summand;

        newLeftPart->span.begin = summand->span.begin;
        newLeftPart->span.end = second_summand->span.end;

        ret = newLeftPart;

    }
}

SLExpr* SLParseSummand(SLTokenReader* reader) {
    SLExpr* unary = SLParseUnary(reader);

    if (unary == NULL) {
        SL_TRACE(reader);
        return NULL;
    }

    return unary;
}

SLExpr* SLParseUnary(SLTokenReader* reader) {
    SLToken* unaryOp = SLGetNextToken(reader);

    if (unaryOp == NULL) {
        SL_EXPECTED(reader, NULL, "expression");
        return NULL;
    }

    if (unaryOp->type != SL_TOK_PLUS
        && unaryOp->type != SL_TOK_MINUS
        && unaryOp->type != SL_TOK_NOT) {
        reader->cursor--;
        SL_RETURN_TRACED(reader, SLParsePrimary(reader));
    }

    SLExpr* nextUnary = SLParseUnary(reader);

    if (nextUnary == NULL) {
        SL_TRACE(reader);
        return NULL;
    }
    SLExpr* ret = (SLExpr*) malloc(sizeof(SLExpr));
    ret->type = SL_EXPR_UNARY;

    ret->value.unary.op = unaryOp;
    ret->value.unary.operand = nextUnary;

    ret->span.begin = unaryOp->position;
    ret->span.end = nextUnary->span.end;

    return ret;
}

SLExpr* SLParsePrimary(SLTokenReader* reader) {

    SLToken* tok = SLGetNextToken(reader);

    if (tok == NULL) {
        SL_EXPECTED(reader, NULL, "expression");
        return NULL;
    }

    SLExpr* ret = (SLExpr*) malloc(sizeof(SLExpr));

    if (tok->type == SL_TOK_INTEGER_LIT) {
        ret->type = SL_EXPR_INTEGER;
        ret->value.number = tok;
    } else if (tok->type == SL_TOK_REAL_LIT) {
        ret->type = SL_EXPR_REAL;
        ret->value.number = tok;
    } else if (tok->type == SL_TOK_TRUE) {
        ret->type = SL_EXPR_BOOLEAN;
        ret->value.boolean = tok;
    } else if (tok->type == SL_TOK_FALSE) {
        ret->type = SL_EXPR_BOOLEAN;
        ret->value.boolean = tok;
    } else if (tok->type == SL_TOK_IDENTIFIER) {
        free(ret);
        reader->cursor--;
        SL_RETURN_TRACED(reader, SLParseIDPrimary(reader));
        return SLParseIDPrimary(reader);
    } else if (tok->type == SL_TOK_LPAR) {
        free(ret);
        SLExpr* subexpr = SLParseExpr(reader);

        if (subexpr == NULL) {
            SL_TRACE(reader);
            return NULL;
        }

        SLToken* closePar = SLGetNextToken(reader);

        if (closePar == NULL || closePar->type != SL_TOK_RPAR) {
            SL_EXPECTED(reader, closePar, "')'");
            SLFreeExpr(subexpr);
            return NULL;
        }

        return subexpr;
    } else {
        free(ret);
        SL_EXPECTED(reader, tok, "expression");
        return NULL;
    }

    ret->span.begin = tok->position;
    ret->span.end = tok->position;
    ret->span.end.position += (int) tok->value.length;

    return ret;
}

SLExpr* SLParseIDPrimary(SLTokenReader* reader) {
    SLToken* id = SLGetNextToken(reader);

    if (id == NULL) {
        SL_EXPECTED(reader, id, "identifier");
        return NULL;
    }

    if (id->type != SL_TOK_IDENTIFIER) {
        SL_EXPECTED(reader, id, "identifier");
        return NULL;
    }

    SLExpr* ret = (SLExpr*) malloc(sizeof(SLExpr));
    if (ret == NULL) return NULL;

    ret->type = SL_EXPR_NAME;
    ret->value.name = id;
    ret->span.begin = id->position;
    ret->span.end = id->position;
    ret->span.end.position += (int) id->value.length;

    SLToken* next = SLGetNextToken(reader);
    if (next != NULL) reader->cursor--;

    if (next != NULL && next->type == SL_TOK_LPAR) {
        ret->type = SL_EXPR_CALL;
        ret->value.call.callee = id;
        ret->value.call.arguments = (SLExprArray){0};

        if (!SLParseCallTail(reader, &ret->value.call.arguments)) {
            SL_TRACE(reader);
            SLFreeExpr(ret);
            return NULL;
        }

        SLToken* closePar = &reader->tokens->items[reader->cursor - 1];
        ret->span.end = closePar->position;
        ret->span.end.position += (int) closePar->value.length;
        return ret;
    }

    SL_RETURN_TRACED(reader, SLParseModifiableTail(reader, ret));
}

bool SLParseCallTail(SLTokenReader* reader, SLExprArray* list) {
    SLToken* lpar = SLGetNextToken(reader);

    if (lpar == NULL) {
        SL_EXPECTED(reader, lpar, "'('");
        return false;
    }

    if (lpar->type != SL_TOK_LPAR) {
        SL_EXPECTED(reader, lpar, "'('");
        reader->cursor--;
        return false;
    }

    SLToken* rpar = SLGetNextToken(reader);

    if (rpar == NULL) {
        SL_EXPECTED(reader, NULL, "argument or ')'");

        return false;
    }

    if (rpar->type == SL_TOK_RPAR) {
        return true;
    }

    reader->cursor--;

    while (true) {
        SLExpr* arg = SLParseExpr(reader);

        if (arg == NULL) {
            SL_TRACE(reader);
            return false;
        }

        if (!SLExprArrayPush(list, arg)) {
            SL_RAISE(reader, NULL, "out of memory while parsing call arguments");
            SLFreeExpr(arg);
            return false;
        }

        SLToken* comma = SLGetNextToken(reader);

        if (comma == NULL) {
            SL_EXPECTED(reader, comma, "',' or ')'");
            return false;
        }

        if (comma->type == SL_TOK_RPAR) {
            return true;
        }

        if (comma->type != SL_TOK_COMMA) {
            SL_EXPECTED(reader, comma, "',' or ')'");
            return false;
        }
    }
}

SLExpr* SLParseModifiableTail(SLTokenReader* reader, SLExpr* base) {
    if (base == NULL) return NULL;

    while (true) {
        SLToken* dotOrBracket = SLGetNextToken(reader);

        if (dotOrBracket == NULL) {
            return base;
        }

        if (dotOrBracket->type == SL_TOK_DOT) {
            SLToken* id = SLGetNextToken(reader);

            if (id == NULL) {
                SL_EXPECTED(reader, id, "field name after '.'");
                SLFreeExpr(base);
                return NULL;
            }

            if (id->type != SL_TOK_IDENTIFIER) {
                SL_EXPECTED(reader, id, "field name after '.'");
                SLFreeExpr(base);
                return NULL;
            }

            SLExpr* ret = (SLExpr*) malloc(sizeof(SLExpr));
            if (ret == NULL) {
                SLFreeExpr(base);
                return NULL;
            }

            ret->type = SL_EXPR_FIELD;
            ret->value.field.base = base;
            ret->value.field.name = id;
            ret->span.begin = base->span.begin;
            ret->span.end = id->position;
            ret->span.end.position += (int) id->value.length;
            base = ret;
        } else if (dotOrBracket->type == SL_TOK_LBRACKET) {
            SLExpr* expr = SLParseExpr(reader);

            if (expr == NULL) {
                SL_TRACE(reader);
                SLFreeExpr(base);
                return NULL;
            }

            SLToken* closeBracket = SLGetNextToken(reader);
            if (closeBracket == NULL || closeBracket->type != SL_TOK_RBRACKET) {
                SL_EXPECTED(reader, closeBracket, "']'");
                SLFreeExpr(expr);
                SLFreeExpr(base);
                return NULL;
            }

            SLExpr* ret = (SLExpr*) malloc(sizeof(SLExpr));
            if (ret == NULL) {
                SLFreeExpr(expr);
                SLFreeExpr(base);
                return NULL;
            }

            ret->type = SL_EXPR_INDEX;
            ret->value.index.base = base;
            ret->value.index.index = expr;
            ret->span.begin = base->span.begin;
            ret->span.end = closeBracket->position;
            ret->span.end.position += (int) closeBracket->value.length;
            base = ret;
        } else {
            reader->cursor--;
            return base;
        }
    }
}

SLToken* SLParseSign(SLTokenReader* reader) {
    SLToken* tok = SLGetNextToken(reader);

    if (tok == NULL) {
        // TODO: Error
        return NULL;
    }

    if (tok->type != SL_TOK_PLUS || tok->type != SL_TOK_MINUS) {
        return NULL;
    }

    return tok;
}

/* ================= types ================= */

SLType* SLParseType(SLTokenReader* reader) {
    SLToken* tok = SLPeekToken(reader);

    if (tok == NULL) {
        SL_EXPECTED(reader, NULL, "type");
        return NULL;
    }

    switch (tok->type) {
        case SL_TOK_INTEGER:
        case SL_TOK_REAL:
        case SL_TOK_BOOLEAN:
        case SL_TOK_IDENTIFIER: {
            SLType* ret = calloc(1, sizeof(*ret));
            if (ret == NULL) {
                SL_RAISE(reader, tok, "out of memory");
                return NULL;
            }

            reader->cursor++;

            if (tok->type == SL_TOK_IDENTIFIER) {
                ret->kind = SL_TYPE_NAMED;
                ret->value.name = tok;
            } else {
                ret->kind = SL_TYPE_PRIMITIVE;
                ret->value.primitive = tok;
            }

            ret->span.begin = tok->position;
            ret->span.end = SLTokenEnd(tok);
            return ret;
        }
        case SL_TOK_ARRAY:
            SL_RETURN_TRACED_T(SLType, reader, SLParseArrayType(reader));
        case SL_TOK_RECORD:
            SL_RETURN_TRACED_T(SLType, reader, SLParseRecordType(reader));
        default:
            SL_EXPECTED(reader, tok, "type");
            return NULL;
    }
}

SLType* SLParseArrayType(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_ARRAY, "'array'");
    if (kw == NULL) return NULL;

    if (SL_REQUIRE(reader, SL_TOK_LBRACKET, "'[' after 'array'") == NULL) {
        return NULL;
    }

    SLExpr* size = NULL;
    SLToken* next = SLPeekToken(reader);

    if (next != NULL && next->type != SL_TOK_RBRACKET) {
        size = SLParseExpr(reader);
        if (size == NULL) {
            SL_TRACE(reader);
            return NULL;
        }
    }

    if (SL_REQUIRE(reader, SL_TOK_RBRACKET, "']' after array size") == NULL) {
        SLFreeExpr(size);
        return NULL;
    }

    SLType* element = SLParseType(reader);

    if (element == NULL) {
        SL_TRACE(reader);
        SLFreeExpr(size);
        return NULL;
    }

    SLType* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
        SL_RAISE(reader, kw, "out of memory");
        SLFreeExpr(size);
        SLFreeType(element);
        return NULL;
    }

    ret->kind = SL_TYPE_ARRAY;
    ret->value.array.size = size;
    ret->value.array.element = element;
    ret->span.begin = kw->position;
    ret->span.end = element->span.end;

    return ret;
}

SLType* SLParseRecordType(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_RECORD, "'record'");
    if (kw == NULL) return NULL;

    SLType* ret = calloc(1, sizeof(*ret));
    if (ret == NULL) {
        SL_RAISE(reader, kw, "out of memory");
        return NULL;
    }

    ret->kind = SL_TYPE_RECORD;
    ret->span.begin = kw->position;

    while (true) {
        SLSkipSemicolons(reader);
        SLToken* next = SLPeekToken(reader);

        if (next != NULL && next->type == SL_TOK_END) break;

        if (next == NULL || next->type != SL_TOK_VAR) {
            SL_EXPECTED(reader, next, "field declaration ('var') or 'end' in record");
            SLFreeType(ret);
            return NULL;
        }

        SLDecl* field = SLParseVarDecl(reader);

        if (field == NULL) {
            SL_TRACE(reader);
            SLFreeType(ret);
            return NULL;
        }

        if (!SLDeclArrayPush(&ret->value.record.fields, field)) {
            SL_RAISE(reader, next, "out of memory");
            SLFreeDecl(field);
            SLFreeType(ret);
            return NULL;
        }
    }

    SLToken* end = SLMatchToken(reader, SL_TOK_END);
    ret->span.end = SLTokenEnd(end);

    return ret;
}

/* ================= declarations ================= */

SLDecl* SLParseVarDecl(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_VAR, "'var'");
    if (kw == NULL) return NULL;

    SLToken* name = SL_REQUIRE(reader, SL_TOK_IDENTIFIER, "variable name after 'var'");
    if (name == NULL) return NULL;

    SLType* type = NULL;
    SLExpr* init = NULL;

    if (SLMatchToken(reader, SL_TOK_COLON) != NULL) {
        type = SLParseType(reader);

        if (type == NULL) {
            SL_TRACE(reader);
            return NULL;
        }

        if (SLMatchToken(reader, SL_TOK_EQUAL) != NULL) {
            init = SLParseExpr(reader);

            if (init == NULL) {
                SL_TRACE(reader);
                SLFreeType(type);
                return NULL;
            }
        }
    } else if (SLMatchToken(reader, SL_TOK_EQUAL) != NULL) {
        init = SLParseExpr(reader);

        if (init == NULL) {
            SL_TRACE(reader);
            return NULL;
        }
    } else {
        SL_EXPECTED(reader, SLPeekToken(reader), "':' or '=' after variable name");
        return NULL;
    }

    SLDecl* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
        SL_RAISE(reader, name, "out of memory");
        SLFreeType(type);
        SLFreeExpr(init);
        return NULL;
    }

    ret->kind = SL_DECL_VAR;
    ret->value.var_decl.name = name;
    ret->value.var_decl.type = type;
    ret->value.var_decl.init = init;
    ret->span.begin = kw->position;
    ret->span.end = init != NULL ? init->span.end : type->span.end;

    return ret;
}

SLDecl* SLParseTypeDecl(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_TYPE, "'type'");
    if (kw == NULL) return NULL;

    SLToken* name = SL_REQUIRE(reader, SL_TOK_IDENTIFIER, "type name after 'type'");
    if (name == NULL) return NULL;

    if (SL_REQUIRE(reader, SL_TOK_EQUAL, "'=' after type name") == NULL) {
        return NULL;
    }

    SLType* type = SLParseType(reader);

    if (type == NULL) {
        SL_TRACE(reader);
        return NULL;
    }

    SLDecl* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
        SL_RAISE(reader, name, "out of memory");
        SLFreeType(type);
        return NULL;
    }

    ret->kind = SL_DECL_TYPE;
    ret->value.type_decl.name = name;
    ret->value.type_decl.type = type;
    ret->span.begin = kw->position;
    ret->span.end = type->span.end;

    return ret;
}

SLDecl* SLParseSimpleDecl(SLTokenReader* reader) {
    SLToken* tok = SLPeekToken(reader);

    if (tok != NULL && tok->type == SL_TOK_VAR) {
        SL_RETURN_TRACED_T(SLDecl, reader, SLParseVarDecl(reader));
    }
    if (tok != NULL && tok->type == SL_TOK_TYPE) {
        SL_RETURN_TRACED_T(SLDecl, reader, SLParseTypeDecl(reader));
    }

    SL_EXPECTED(reader, tok, "'var', 'type' or 'routine'");
    return NULL;
}

bool SLParseParams(SLTokenReader* reader, SLParamArray* params) {
    while (true) {
        SLToken* name = SL_REQUIRE(reader, SL_TOK_IDENTIFIER, "parameter name");
        if (name == NULL) return false;

        if (SL_REQUIRE(reader, SL_TOK_COLON, "':' after parameter name") == NULL) {
            return false;
        }

        SLType* type = SLParseType(reader);

        if (type == NULL) {
            SL_TRACE(reader);
            return false;
        }

        SLParam param = {
            .name = name,
            .type = type,
            .span = { .begin = name->position, .end = type->span.end },
        };

        if (!SLParamArrayPush(params, param)) {
            SL_RAISE(reader, name, "out of memory");
            SLFreeType(type);
            return false;
        }

        if (SLMatchToken(reader, SL_TOK_COMMA) == NULL) return true;
    }
}

SLDecl* SLParseRoutineDecl(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_ROUTINE, "'routine'");
    if (kw == NULL) return NULL;

    SLToken* name = SL_REQUIRE(reader, SL_TOK_IDENTIFIER, "routine name after 'routine'");
    if (name == NULL) return NULL;

    if (SL_REQUIRE(reader, SL_TOK_LPAR, "'(' after routine name") == NULL) {
        return NULL;
    }

    SLDecl* decl = calloc(1, sizeof(*decl));
    if (decl == NULL) {
        SL_RAISE(reader, name, "out of memory");
        return NULL;
    }

    decl->kind = SL_DECL_ROUTINE;
    decl->value.routine.name = name;
    decl->value.routine.body_kind = SL_ROUTINE_FORWARD;
    decl->span.begin = kw->position;

    SLToken* next = SLPeekToken(reader);
    SLToken* rpar = NULL;
    SLToken* end = NULL;

    if (next != NULL && next->type != SL_TOK_RPAR) {
        if (!SLParseParams(reader, &decl->value.routine.params)) {
            SL_TRACE(reader);
            goto fail;
        }
    }

    rpar = SL_REQUIRE(reader, SL_TOK_RPAR, "',' or ')' in parameter list");
    if (rpar == NULL) goto fail;

    decl->span.end = SLTokenEnd(rpar);

    if (SLMatchToken(reader, SL_TOK_COLON) != NULL) {
        SLType* return_type = SLParseType(reader);

        if (return_type == NULL) {
            SL_TRACE(reader);
            goto fail;
        }

        decl->value.routine.return_type = return_type;
        decl->span.end = return_type->span.end;
    }

    if (SLMatchToken(reader, SL_TOK_EQUAL) != NULL) {
        decl->value.routine.body_kind = SL_ROUTINE_BLOCK;

        if (!SLParseBody(reader, &decl->value.routine.body)) {
            SL_TRACE(reader);
            goto fail;
        }

        end = SL_REQUIRE(reader, SL_TOK_END, "'end' to close routine body");
        if (end == NULL) {
            SL_TRACE(reader);
            goto fail;
        }

        decl->span.end = SLTokenEnd(end);
    } else if (SLMatchToken(reader, SL_TOK_INLINE_FUNC) != NULL) {
        decl->value.routine.body_kind = SL_ROUTINE_EXPR;
        decl->value.routine.expr_body = SLParseExpr(reader);

        if (decl->value.routine.expr_body == NULL) {
            SL_TRACE(reader);
            goto fail;
        }

        decl->span.end = decl->value.routine.expr_body->span.end;
    }

    return decl;

fail:
    SLFreeDecl(decl);
    return NULL;
}

/* ================= statements ================= */

bool SLParseBody(SLTokenReader* reader, SLBody* body) {
    while (true) {
        SLSkipSemicolons(reader);
        SLToken* next = SLPeekToken(reader);

        if (next == NULL
            || next->type == SL_TOK_END
            || next->type == SL_TOK_ELSE) {
            return true;
        }

        SLBodyItem item = {0};

        if (next->type == SL_TOK_VAR || next->type == SL_TOK_TYPE) {
            item.kind = SL_ITEM_DECL;
            item.value.decl = SLParseSimpleDecl(reader);
            if (item.value.decl == NULL) {
                SL_TRACE(reader);
                return false;
            }
        } else {
            item.kind = SL_ITEM_STMT;
            item.value.stmt = SLParseStatement(reader);
            if (item.value.stmt == NULL) {
                SL_TRACE(reader);
                return false;
            }
        }

        if (!SLBodyPush(body, item)) {
            SL_RAISE(reader, next, "out of memory");
            if (item.kind == SL_ITEM_DECL) {
                SLFreeDecl(item.value.decl);
            } else {
                SLFreeStmt(item.value.stmt);
            }
            return false;
        }
    }
}

SLStmt* SLParseStatement(SLTokenReader* reader) {
    SLToken* tok = SLPeekToken(reader);

    if (tok == NULL) {
        SL_EXPECTED(reader, NULL, "statement or 'end'");
        return NULL;
    }

    switch (tok->type) {
        case SL_TOK_IDENTIFIER:
            SL_RETURN_TRACED_T(SLStmt, reader, SLParseIDStatement(reader));
        case SL_TOK_WHILE:
            SL_RETURN_TRACED_T(SLStmt, reader, SLParseWhile(reader));
        case SL_TOK_FOR:
            SL_RETURN_TRACED_T(SLStmt, reader, SLParseFor(reader));
        case SL_TOK_IF:
            SL_RETURN_TRACED_T(SLStmt, reader, SLParseIf(reader));
        case SL_TOK_RETURN:
            SL_RETURN_TRACED_T(SLStmt, reader, SLParseReturn(reader));
        default:
            SL_EXPECTED(reader, tok, "statement or 'end'");
            return NULL;
    }
}

SLStmt* SLParseIDStatement(SLTokenReader* reader) {
    SLExpr* target = SLParseIDPrimary(reader);

    if (target == NULL) {
        SL_TRACE(reader);
        return NULL;
    }

    SLStmt* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
        SL_RAISE(reader, SLPeekToken(reader), "out of memory");
        SLFreeExpr(target);
        return NULL;
    }

    ret->span.begin = target->span.begin;

    if (target->type == SL_EXPR_CALL) {
        ret->kind = SL_STMT_CALL;
        ret->value.call = target;
        ret->span.end = target->span.end;
        return ret;
    }

    ret->kind = SL_STMT_ASSIGN;
    ret->value.assign.target = target;

    if (SL_REQUIRE(reader, SL_TOK_ASSIGN, "':=' in assignment") == NULL) {
        SLFreeStmt(ret);
        return NULL;
    }

    SLExpr* value = SLParseExpr(reader);

    if (value == NULL) {
        SL_TRACE(reader);
        SLFreeStmt(ret);
        return NULL;
    }

    ret->value.assign.value = value;
    ret->span.end = value->span.end;

    return ret;
}

SLStmt* SLParseWhile(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_WHILE, "'while'");
    if (kw == NULL) return NULL;

    SLStmt* ret = calloc(1, sizeof(*ret));
    if (ret == NULL) {
        SL_RAISE(reader, kw, "out of memory");
        return NULL;
    }

    ret->kind = SL_STMT_WHILE;
    ret->span.begin = kw->position;

    SLToken* end = NULL;

    ret->value.while_loop.condition = SLParseExpr(reader);

    if (ret->value.while_loop.condition == NULL) {
        SL_TRACE(reader);
        goto fail;
    }

    if (SL_REQUIRE(reader, SL_TOK_LOOP, "'loop' after while condition") == NULL) {
        goto fail;
    }

    if (!SLParseBody(reader, &ret->value.while_loop.body)) {
        SL_TRACE(reader);
        goto fail;
    }

    end = SL_REQUIRE(reader, SL_TOK_END, "'end' to close 'while' loop");
    if (end == NULL) goto fail;

    ret->span.end = SLTokenEnd(end);
    return ret;

fail:
    SLFreeStmt(ret);
    return NULL;
}

SLStmt* SLParseFor(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_FOR, "'for'");
    if (kw == NULL) return NULL;

    SLToken* iterator = SL_REQUIRE(reader, SL_TOK_IDENTIFIER, "loop variable after 'for'");
    if (iterator == NULL) return NULL;

    if (SL_REQUIRE(reader, SL_TOK_IN, "'in' after loop variable") == NULL) {
        return NULL;
    }

    SLStmt* ret = calloc(1, sizeof(*ret));
    if (ret == NULL) {
        SL_RAISE(reader, kw, "out of memory");
        return NULL;
    }

    ret->kind = SL_STMT_FOR;
    ret->value.for_loop.iterator = iterator;
    ret->span.begin = kw->position;

    SLToken* end = NULL;

    // range = expr, [ "..", expr ]
    ret->value.for_loop.from = SLParseExpr(reader);

    if (ret->value.for_loop.from == NULL) {
        SL_TRACE(reader);
        goto fail;
    }

    if (SLMatchToken(reader, SL_TOK_RANGE) != NULL) {
        ret->value.for_loop.to = SLParseExpr(reader);

        if (ret->value.for_loop.to == NULL) {
            SL_TRACE(reader);
            goto fail;
        }
    }

    if (SLMatchToken(reader, SL_TOK_REVERSE) != NULL) {
        ret->value.for_loop.reverse = true;
    }

    if (SL_REQUIRE(reader, SL_TOK_LOOP, "'loop' after for range") == NULL) {
        goto fail;
    }

    if (!SLParseBody(reader, &ret->value.for_loop.body)) {
        SL_TRACE(reader);
        goto fail;
    }

    end = SL_REQUIRE(reader, SL_TOK_END, "'end' to close 'for' loop");
    if (end == NULL) goto fail;

    ret->span.end = SLTokenEnd(end);
    return ret;

fail:
    SLFreeStmt(ret);
    return NULL;
}

SLStmt* SLParseIf(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_IF, "'if'");
    if (kw == NULL) return NULL;

    SLStmt* ret = calloc(1, sizeof(*ret));
    if (ret == NULL) {
        SL_RAISE(reader, kw, "out of memory");
        return NULL;
    }

    ret->kind = SL_STMT_IF;
    ret->span.begin = kw->position;

    SLToken* end = NULL;

    ret->value.if_stmt.condition = SLParseExpr(reader);

    if (ret->value.if_stmt.condition == NULL) {
        SL_TRACE(reader);
        goto fail;
    }

    if (SL_REQUIRE(reader, SL_TOK_THEN, "'then' after if condition") == NULL) {
        goto fail;
    }

    if (!SLParseBody(reader, &ret->value.if_stmt.then_body)) {
        SL_TRACE(reader);
        goto fail;
    }

    if (SLMatchToken(reader, SL_TOK_ELSE) != NULL) {
        ret->value.if_stmt.has_else = true;

        if (!SLParseBody(reader, &ret->value.if_stmt.else_body)) {
            SL_TRACE(reader);
            goto fail;
        }
    }

    end = SL_REQUIRE(reader, SL_TOK_END, "'end' to close 'if'");
    if (end == NULL) goto fail;

    ret->span.end = SLTokenEnd(end);
    return ret;

fail:
    SLFreeStmt(ret);
    return NULL;
}

SLStmt* SLParseReturn(SLTokenReader* reader) {
    SLToken* kw = SL_REQUIRE(reader, SL_TOK_RETURN, "'return'");
    if (kw == NULL) return NULL;

    SLExpr* value = NULL;

    if (SLStartsExpr(SLPeekToken(reader))) {
        value = SLParseExpr(reader);

        if (value == NULL) {
            SL_TRACE(reader);
            return NULL;
        }
    }

    SLStmt* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
        SL_RAISE(reader, kw, "out of memory");
        SLFreeExpr(value);
        return NULL;
    }

    ret->kind = SL_STMT_RETURN;
    ret->value.return_value = value;
    ret->span.begin = kw->position;
    ret->span.end = value != NULL ? value->span.end : SLTokenEnd(kw);

    return ret;
}

/* ================= program ================= */

SLProgram* SLParseProgram(SLTokenReader* reader) {
    SLProgram* program = calloc(1, sizeof(*program));
    if (program == NULL) {
        SL_RAISE(reader, NULL, "out of memory");
        return NULL;
    }

    while (true) {
        SLSkipSemicolons(reader);
        SLToken* tok = SLPeekToken(reader);

        if (tok == NULL) break;

        SLDecl* decl = tok->type == SL_TOK_ROUTINE
            ? SLParseRoutineDecl(reader)
            : SLParseSimpleDecl(reader);

        if (decl == NULL) {
            SL_TRACE(reader);
            SLFreeProgram(program);
            return NULL;
        }

        if (!SLDeclArrayPush(&program->decls, decl)) {
            SL_RAISE(reader, tok, "out of memory");
            SLFreeDecl(decl);
            SLFreeProgram(program);
            return NULL;
        }
    }

    return program;
}

void SLExprDebugPrint(SLExpr* expr) {
    fputs(prefix, stdout);
    fputs(branch, stdout);

    if (expr == NULL) {
        puts("<null>");
        return;
    }

    const char* label;
    SLToken* token = NULL;
    SLSlice value = {0};
    SLExpr* child_pair[2] = {NULL, NULL};
    SLExpr** children = child_pair;
    size_t child_count = 0;

    switch (expr->type) {
        case SL_EXPR_INTEGER:
            label = "INTEGER";
            token = expr->value.number;
            break;
        case SL_EXPR_REAL:
            label = "REAL";
            token = expr->value.number;
            break;
        case SL_EXPR_BOOLEAN:
            label = "BOOLEAN";
            token = expr->value.boolean;
            break;
        case SL_EXPR_NAME:
            label = "NAME";
            token = expr->value.name;
            break;
        case SL_EXPR_UNARY:
            label = "UNARY";
            token = expr->value.unary.op;
            child_pair[0] = expr->value.unary.operand;
            child_count = 1;
            break;
        case SL_EXPR_BINARY:
            label = "BINARY";
            token = expr->value.binary.op;
            child_pair[0] = expr->value.binary.left;
            child_pair[1] = expr->value.binary.right;
            child_count = 2;
            break;
        case SL_EXPR_CALL:
            label = "CALL";
            token = expr->value.call.callee;
            children = expr->value.call.arguments.items;
            child_count = expr->value.call.arguments.count;
            break;
        case SL_EXPR_INDEX:
            label = "INDEX";
            child_pair[0] = expr->value.index.base;
            child_pair[1] = expr->value.index.index;
            child_count = 2;
            break;
        case SL_EXPR_FIELD:
            label = "FIELD";
            token = expr->value.field.name;
            child_pair[0] = expr->value.field.base;
            child_count = 1;
            break;
        default:
            printf("UNKNOWN (%d)\n", (int) expr->type);
            return;
    }

    if (token != NULL) {
        value = token->value;
    }

    fputs(label, stdout);
    if (value.start != NULL && value.length > 0) {
        putchar(' ');
        fwrite(value.start, 1, value.length, stdout);
    }
    putchar('\n');

    if (child_count == 0) {
        return;
    }

    const char* saved_prefix = prefix;
    const char* saved_branch = branch;
    const char* continuation = branch[0] == '\0' ? ""
        : branch[0] == '|' ? "|   " : "    ";
    size_t prefix_length = strlen(prefix);
    size_t continuation_length = strlen(continuation);
    char* child_prefix = malloc(prefix_length + continuation_length + 1);
    if (child_prefix == NULL) {
        fputs("SLExprDebugPrint: out of memory\n", stderr);
        return;
    }
    memcpy(child_prefix, prefix, prefix_length);
    memcpy(child_prefix + prefix_length, continuation, continuation_length + 1);

    prefix = child_prefix;
    for (size_t i = 0; i < child_count; ++i) {
        branch = i == child_count - 1 ? "`-- " : "|-- ";
        SLExprDebugPrint(children != NULL ? children[i] : NULL);
    }
    prefix = saved_prefix;
    branch = saved_branch;
    free(child_prefix);
}

typedef struct {
    const char* saved_prefix;
    const char* saved_branch;
    char* child_prefix;
} SLDbgScope;

static void SLDbgLabel(const char* label, const SLToken* token) {
    fputs(prefix, stdout);
    fputs(branch, stdout);
    fputs(label, stdout);

    if (token != NULL && token->value.start != NULL
        && token->value.length > 0) {
        putchar(' ');
        fwrite(token->value.start, 1, token->value.length, stdout);
    }

    putchar('\n');
}

// Call right after printing a node's own line, before printing its children.
static bool SLDbgOpen(SLDbgScope* scope) {
    const char* continuation = branch[0] == '\0' ? ""
        : branch[0] == '|' ? "|   " : "    ";
    size_t prefix_length = strlen(prefix);
    size_t continuation_length = strlen(continuation);

    scope->child_prefix = malloc(prefix_length + continuation_length + 1);

    if (scope->child_prefix == NULL) {
        fputs("debug print: out of memory\n", stderr);
        return false;
    }

    memcpy(scope->child_prefix, prefix, prefix_length);
    memcpy(scope->child_prefix + prefix_length, continuation,
           continuation_length + 1);

    scope->saved_prefix = prefix;
    scope->saved_branch = branch;
    prefix = scope->child_prefix;

    return true;
}

static void SLDbgClose(SLDbgScope* scope) {
    prefix = scope->saved_prefix;
    branch = scope->saved_branch;
    free(scope->child_prefix);
}

// Select the branch glyph for child number `index` of `count`.
static void SLDbgBranch(size_t index, size_t count) {
    branch = index + 1 == count ? "`-- " : "|-- ";
}

static void SLDbgExprSection(const char* label, SLExpr* expr) {
    SLDbgLabel(label, NULL);

    SLDbgScope scope;
    if (!SLDbgOpen(&scope)) return;

    SLDbgBranch(0, 1);
    SLExprDebugPrint(expr);

    SLDbgClose(&scope);
}

static void SLDbgTypeSection(const char* label, const SLToken* token,
                             SLType* type) {
    SLDbgLabel(label, token);

    SLDbgScope scope;
    if (!SLDbgOpen(&scope)) return;

    SLDbgBranch(0, 1);
    SLTypeDebugPrint(type);

    SLDbgClose(&scope);
}

static void SLDbgBodySection(const char* label, const SLBody* body) {
    SLDbgLabel(label, NULL);

    if (body->count == 0) return;

    SLDbgScope scope;
    if (!SLDbgOpen(&scope)) return;

    for (size_t i = 0; i < body->count; ++i) {
        SLDbgBranch(i, body->count);

        if (body->items[i].kind == SL_ITEM_DECL) {
            SLDeclDebugPrint(body->items[i].value.decl);
        } else {
            SLStmtDebugPrint(body->items[i].value.stmt);
        }
    }

    SLDbgClose(&scope);
}

void SLTypeDebugPrint(SLType* type) {
    if (type == NULL) {
        SLDbgLabel("<null>", NULL);
        return;
    }

    SLDbgScope scope;

    switch (type->kind) {
        case SL_TYPE_PRIMITIVE:
            SLDbgLabel("TYPE_PRIMITIVE", type->value.primitive);
            return;

        case SL_TYPE_NAMED:
            SLDbgLabel("TYPE_NAME", type->value.name);
            return;

        case SL_TYPE_ARRAY: {
            SLExpr* size = type->value.array.size;
            size_t count = size != NULL ? 2 : 1;
            size_t i = 0;

            SLDbgLabel("TYPE_ARRAY", NULL);
            if (!SLDbgOpen(&scope)) return;

            if (size != NULL) {
                SLDbgBranch(i++, count);
                SLDbgExprSection("SIZE", size);
            }

            SLDbgBranch(i, count);
            SLTypeDebugPrint(type->value.array.element);

            SLDbgClose(&scope);
            return;
        }

        case SL_TYPE_RECORD: {
            const SLDeclArray* fields = &type->value.record.fields;

            SLDbgLabel("TYPE_RECORD", NULL);
            if (fields->count == 0) return;
            if (!SLDbgOpen(&scope)) return;

            for (size_t i = 0; i < fields->count; ++i) {
                SLDbgBranch(i, fields->count);
                SLDeclDebugPrint(fields->items[i]);
            }

            SLDbgClose(&scope);
            return;
        }
    }

    printf("UNKNOWN TYPE (%d)\n", (int) type->kind);
}

void SLDeclDebugPrint(SLDecl* decl) {
    if (decl == NULL) {
        SLDbgLabel("<null>", NULL);
        return;
    }

    SLDbgScope scope;

    switch (decl->kind) {
        case SL_DECL_VAR: {
            SLType* type = decl->value.var_decl.type;
            SLExpr* init = decl->value.var_decl.init;
            size_t count = (type != NULL) + (init != NULL);
            size_t i = 0;

            SLDbgLabel("VAR", decl->value.var_decl.name);
            if (count == 0) return;
            if (!SLDbgOpen(&scope)) return;

            if (type != NULL) {
                SLDbgBranch(i++, count);
                SLTypeDebugPrint(type);
            }

            if (init != NULL) {
                SLDbgBranch(i++, count);
                SLExprDebugPrint(init);
            }

            SLDbgClose(&scope);
            return;
        }

        case SL_DECL_TYPE:
            SLDbgLabel("TYPE_DECL", decl->value.type_decl.name);

            if (!SLDbgOpen(&scope)) return;

            SLDbgBranch(0, 1);
            SLTypeDebugPrint(decl->value.type_decl.type);

            SLDbgClose(&scope);
            return;

        case SL_DECL_ROUTINE: {
            const SLParamArray* params = &decl->value.routine.params;
            SLType* return_type = decl->value.routine.return_type;
            SLRoutineBodyKind body_kind = decl->value.routine.body_kind;

            size_t count = params->count
                + (return_type != NULL)
                + (body_kind != SL_ROUTINE_FORWARD);
            size_t i = 0;

            SLDbgLabel(body_kind == SL_ROUTINE_FORWARD
                           ? "ROUTINE_FORWARD" : "ROUTINE",
                       decl->value.routine.name);

            if (count == 0) return;
            if (!SLDbgOpen(&scope)) return;

            for (size_t p = 0; p < params->count; ++p) {
                SLDbgBranch(i++, count);
                SLDbgTypeSection("PARAM", params->items[p].name,
                                 params->items[p].type);
            }

            if (return_type != NULL) {
                SLDbgBranch(i++, count);
                SLDbgTypeSection("RETURNS", NULL, return_type);
            }

            if (body_kind == SL_ROUTINE_BLOCK) {
                SLDbgBranch(i++, count);
                SLDbgBodySection("BODY", &decl->value.routine.body);
            } else if (body_kind == SL_ROUTINE_EXPR) {
                SLDbgBranch(i++, count);
                SLDbgExprSection("EXPR_BODY", decl->value.routine.expr_body);
            }

            SLDbgClose(&scope);
            return;
        }
    }

    printf("UNKNOWN DECL (%d)\n", (int) decl->kind);
}

void SLStmtDebugPrint(SLStmt* stmt) {
    if (stmt == NULL) {
        SLDbgLabel("<null>", NULL);
        return;
    }

    SLDbgScope scope;

    switch (stmt->kind) {
        case SL_STMT_ASSIGN:
            SLDbgLabel("ASSIGN", NULL);
            if (!SLDbgOpen(&scope)) return;

            SLDbgBranch(0, 2);
            SLExprDebugPrint(stmt->value.assign.target);
            SLDbgBranch(1, 2);
            SLExprDebugPrint(stmt->value.assign.value);

            SLDbgClose(&scope);
            return;

        case SL_STMT_CALL:
            SLDbgLabel("CALL_STMT", NULL);
            if (!SLDbgOpen(&scope)) return;

            SLDbgBranch(0, 1);
            SLExprDebugPrint(stmt->value.call);

            SLDbgClose(&scope);
            return;

        case SL_STMT_WHILE:
            SLDbgLabel("WHILE", NULL);
            if (!SLDbgOpen(&scope)) return;

            SLDbgBranch(0, 2);
            SLExprDebugPrint(stmt->value.while_loop.condition);
            SLDbgBranch(1, 2);
            SLDbgBodySection("BODY", &stmt->value.while_loop.body);

            SLDbgClose(&scope);
            return;

        case SL_STMT_FOR: {
            SLExpr* to = stmt->value.for_loop.to;
            size_t count = to != NULL ? 3 : 2;
            size_t i = 0;

            SLDbgLabel(stmt->value.for_loop.reverse ? "FOR_REVERSE" : "FOR",
                       stmt->value.for_loop.iterator);
            if (!SLDbgOpen(&scope)) return;

            SLDbgBranch(i++, count);
            SLDbgExprSection("FROM", stmt->value.for_loop.from);

            if (to != NULL) {
                SLDbgBranch(i++, count);
                SLDbgExprSection("TO", to);
            }

            SLDbgBranch(i, count);
            SLDbgBodySection("BODY", &stmt->value.for_loop.body);

            SLDbgClose(&scope);
            return;
        }

        case SL_STMT_IF: {
            size_t count = stmt->value.if_stmt.has_else ? 3 : 2;

            SLDbgLabel("IF", NULL);
            if (!SLDbgOpen(&scope)) return;

            SLDbgBranch(0, count);
            SLExprDebugPrint(stmt->value.if_stmt.condition);
            SLDbgBranch(1, count);
            SLDbgBodySection("THEN", &stmt->value.if_stmt.then_body);

            if (stmt->value.if_stmt.has_else) {
                SLDbgBranch(2, count);
                SLDbgBodySection("ELSE", &stmt->value.if_stmt.else_body);
            }

            SLDbgClose(&scope);
            return;
        }

        case SL_STMT_RETURN:
            SLDbgLabel("RETURN", NULL);

            if (stmt->value.return_value == NULL) return;
            if (!SLDbgOpen(&scope)) return;

            SLDbgBranch(0, 1);
            SLExprDebugPrint(stmt->value.return_value);

            SLDbgClose(&scope);
            return;
    }

    printf("UNKNOWN STMT (%d)\n", (int) stmt->kind);
}

void SLProgramDebugPrint(SLProgram* program) {
    if (program == NULL) {
        SLDbgLabel("<null>", NULL);
        return;
    }

    SLDbgLabel("PROGRAM", NULL);

    if (program->decls.count == 0) return;

    SLDbgScope scope;
    if (!SLDbgOpen(&scope)) return;

    for (size_t i = 0; i < program->decls.count; ++i) {
        SLDbgBranch(i, program->decls.count);
        SLDeclDebugPrint(program->decls.items[i]);
    }

    SLDbgClose(&scope);
}
