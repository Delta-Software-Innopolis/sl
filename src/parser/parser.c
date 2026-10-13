#include "parser.h"

static _Thread_local const char* prefix = "";
static _Thread_local const char* branch = "";

static void SLFreeType(SLType* type);
static void SLFreeDecl(SLDecl* decl);
static void SLFreeStmt(SLStmt* stmt);

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

    SLTokenReader* reader = (SLTokenReader*) malloc(sizeof(SLTokenReader));

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
    if (reader == NULL || (size_t) reader->cursor >= reader->tokens->count) {
        return NULL;
    }

    SLSkipNewlines(reader);
    return &reader->tokens->items[reader->cursor];
}

static SLToken* SLMatchToken(SLTokenReader* reader, SLTokenType type) {
    SLToken* tok = SLPeekToken(reader);
    if (tok == NULL || tok->type != type) return NULL;
    reader->cursor++;
    return tok;
}

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
        // TODO: Error

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
        // TODO: Error and free relation
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
        // TODO: Error
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
        // TODO: Error and free simple

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
        // TODO: Error
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
            // TODO: Error and free factor

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
        // TODO: Error
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
            // TODO: Error and free first summand

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
        // TODO: Error
        return NULL;
    }

    return unary;
}

SLExpr* SLParseUnary(SLTokenReader* reader) {
    SLToken* unaryOp = SLGetNextToken(reader);

    if (unaryOp == NULL) {
        return NULL;
    }

    if (unaryOp->type != SL_TOK_PLUS
        && unaryOp->type != SL_TOK_MINUS
        && unaryOp->type != SL_TOK_NOT) {
        reader->cursor--;
        return SLParsePrimary(reader);
    }

    SLExpr* nextUnary = SLParseUnary(reader);

    if (nextUnary == NULL) {
        // TODO: Error
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
    // TODO: Parse ID Primary

    SLToken* tok = SLGetNextToken(reader);

    if (tok == NULL) {
        // TODO: Error
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
        return SLParseIDPrimary(reader);
    } else if (tok->type == SL_TOK_LPAR) {
        free(ret);
        SLExpr* subexpr = SLParseExpr(reader);

        if (subexpr == NULL) {
            // TODO: Error
            return NULL;
        }

        SLToken* closePar = SLGetNextToken(reader);

        if (closePar == NULL || closePar->type != SL_TOK_RPAR) {
            // TODO: Error unclosed paranthesys
            return NULL;
        }

        return subexpr;
    } else {
        free(ret);
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
        // TODO: Error
        return NULL;
    }

    if (id->type != SL_TOK_IDENTIFIER) {
        // TODO: Error
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
            SLFreeExpr(ret);
            return NULL;
        }

        SLToken* closePar = &reader->tokens->items[reader->cursor - 1];
        ret->span.end = closePar->position;
        ret->span.end.position += (int) closePar->value.length;
        return ret;
    }

    return SLParseModifiableTail(reader, ret);
}

bool SLParseCallTail(SLTokenReader* reader, SLExprArray* list) {
    SLToken* lpar = SLGetNextToken(reader);

    if (lpar == NULL) {
        // TODO: Error
        return false;
    }

    if (lpar->type != SL_TOK_LPAR) {
        // TODO: Error

        reader->cursor--;
        return false;
    }

    SLToken* rpar = SLGetNextToken(reader);

    if (rpar == NULL) {
        // TODO: Error

        return false;
    }

    if (rpar->type == SL_TOK_RPAR) {
        return true;
    }

    reader->cursor--;

    while (true) {
        SLExpr* arg = SLParseExpr(reader);

        if (arg == NULL) {
            return false;
        }

        if (!SLExprArrayPush(list, arg)) {
            // TODO: Error
            SLFreeExpr(arg);
            return false;
        }

        SLToken* comma = SLGetNextToken(reader);

        if (comma == NULL) {
            return false;
        }

        if (comma->type == SL_TOK_RPAR) {
            return true;
        }

        if (comma->type != SL_TOK_COMMA) {
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
                // TODO: Error
                SLFreeExpr(base);
                return NULL;
            }

            if (id->type != SL_TOK_IDENTIFIER) {
                // TODO: Error
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
                // TODO: Error
                SLFreeExpr(base);
                return NULL;
            }

            SLToken* closeBracket = SLGetNextToken(reader);
            if (closeBracket == NULL || closeBracket->type != SL_TOK_RBRACKET) {
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
        // TODO: Error
        return NULL;
    }

    switch (tok->type) {
        case SL_TOK_INTEGER:
        case SL_TOK_REAL:
        case SL_TOK_BOOLEAN:
        case SL_TOK_IDENTIFIER: {
            SLType* ret = calloc(1, sizeof(*ret));
            if (ret == NULL) return NULL;

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
            return SLParseArrayType(reader);
        case SL_TOK_RECORD:
            return SLParseRecordType(reader);
        default:
            // TODO: Error
            return NULL;
    }
}

SLType* SLParseArrayType(SLTokenReader* reader) {
    SLToken* kw = SLMatchToken(reader, SL_TOK_ARRAY);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    if (SLMatchToken(reader, SL_TOK_LBRACKET) == NULL) {
        // TODO: Error
        return NULL;
    }

    SLExpr* size = NULL;
    SLToken* next = SLPeekToken(reader);

    if (next != NULL && next->type != SL_TOK_RBRACKET) {
        size = SLParseExpr(reader);
        if (size == NULL) {
            // TODO: Error
            return NULL;
        }
    }

    if (SLMatchToken(reader, SL_TOK_RBRACKET) == NULL) {
        // TODO: Error
        SLFreeExpr(size);
        return NULL;
    }

    SLType* element = SLParseType(reader);

    if (element == NULL) {
        // TODO: Error
        SLFreeExpr(size);
        return NULL;
    }

    SLType* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
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
    SLToken* kw = SLMatchToken(reader, SL_TOK_RECORD);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    SLType* ret = calloc(1, sizeof(*ret));
    if (ret == NULL) return NULL;

    ret->kind = SL_TYPE_RECORD;
    ret->span.begin = kw->position;

    while (true) {
        SLSkipSemicolons(reader);
        SLToken* next = SLPeekToken(reader);

        if (next == NULL) {
            // TODO: Error unterminated record
            SLFreeType(ret);
            return NULL;
        }

        if (next->type == SL_TOK_END) break;

        SLDecl* field = SLParseVarDecl(reader);

        if (field == NULL) {
            SLFreeType(ret);
            return NULL;
        }

        if (!SLDeclArrayPush(&ret->value.record.fields, field)) {
            SLFreeDecl(field);
            SLFreeType(ret);
            return NULL;
        }
    }

    SLToken* end = SLMatchToken(reader, SL_TOK_END); // guaranteed by the loop
    ret->span.end = SLTokenEnd(end);

    return ret;
}

/* ================= declarations ================= */

SLDecl* SLParseVarDecl(SLTokenReader* reader) {
    SLToken* kw = SLMatchToken(reader, SL_TOK_VAR);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    SLToken* name = SLMatchToken(reader, SL_TOK_IDENTIFIER);

    if (name == NULL) {
        // TODO: Error
        return NULL;
    }

    SLType* type = NULL;
    SLExpr* init = NULL;

    if (SLMatchToken(reader, SL_TOK_COLON) != NULL) {
        type = SLParseType(reader);

        if (type == NULL) {
            // TODO: Error
            return NULL;
        }

        if (SLMatchToken(reader, SL_TOK_EQUAL) != NULL) {
            init = SLParseExpr(reader);

            if (init == NULL) {
                // TODO: Error
                SLFreeType(type);
                return NULL;
            }
        }
    } else if (SLMatchToken(reader, SL_TOK_EQUAL) != NULL) {
        init = SLParseExpr(reader);

        if (init == NULL) {
            // TODO: Error
            return NULL;
        }
    } else {
        // TODO: Error expected ':' or '='
        return NULL;
    }

    SLDecl* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
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
    SLToken* kw = SLMatchToken(reader, SL_TOK_TYPE);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    SLToken* name = SLMatchToken(reader, SL_TOK_IDENTIFIER);

    if (name == NULL) {
        // TODO: Error
        return NULL;
    }

    if (SLMatchToken(reader, SL_TOK_EQUAL) == NULL) {
        // TODO: Error
        return NULL;
    }

    SLType* type = SLParseType(reader);

    if (type == NULL) {
        // TODO: Error
        return NULL;
    }

    SLDecl* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
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

    if (tok == NULL) {
        // TODO: Error
        return NULL;
    }

    if (tok->type == SL_TOK_VAR) return SLParseVarDecl(reader);
    if (tok->type == SL_TOK_TYPE) return SLParseTypeDecl(reader);

    // TODO: Error
    return NULL;
}

bool SLParseParams(SLTokenReader* reader, SLParamArray* params) {
    while (true) {
        SLToken* name = SLMatchToken(reader, SL_TOK_IDENTIFIER);

        if (name == NULL) {
            // TODO: Error
            return false;
        }

        if (SLMatchToken(reader, SL_TOK_COLON) == NULL) {
            // TODO: Error
            return false;
        }

        SLType* type = SLParseType(reader);

        if (type == NULL) {
            // TODO: Error
            return false;
        }

        SLParam param = {
            .name = name,
            .type = type,
            .span = { .begin = name->position, .end = type->span.end },
        };

        if (!SLParamArrayPush(params, param)) {
            SLFreeType(type);
            return false;
        }

        if (SLMatchToken(reader, SL_TOK_COMMA) == NULL) return true;
    }
}

SLDecl* SLParseRoutineDecl(SLTokenReader* reader) {
    SLToken* kw = SLMatchToken(reader, SL_TOK_ROUTINE);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    SLToken* name = SLMatchToken(reader, SL_TOK_IDENTIFIER);

    if (name == NULL) {
        // TODO: Error
        return NULL;
    }

    if (SLMatchToken(reader, SL_TOK_LPAR) == NULL) {
        // TODO: Error
        return NULL;
    }

    SLDecl* decl = calloc(1, sizeof(*decl));
    if (decl == NULL) return NULL;

    decl->kind = SL_DECL_ROUTINE;
    decl->value.routine.name = name;
    decl->value.routine.body_kind = SL_ROUTINE_FORWARD;
    decl->span.begin = kw->position;

    SLToken* next = SLPeekToken(reader);
    SLToken* rpar = NULL;
    SLToken* end = NULL;

    if (next != NULL && next->type != SL_TOK_RPAR) {
        if (!SLParseParams(reader, &decl->value.routine.params)) goto fail;
    }

    rpar = SLMatchToken(reader, SL_TOK_RPAR);

    if (rpar == NULL) {
        // TODO: Error
        goto fail;
    }

    decl->span.end = SLTokenEnd(rpar);

    if (SLMatchToken(reader, SL_TOK_COLON) != NULL) {
        SLType* return_type = SLParseType(reader);

        if (return_type == NULL) {
            // TODO: Error
            goto fail;
        }

        decl->value.routine.return_type = return_type;
        decl->span.end = return_type->span.end;
    }

    if (SLMatchToken(reader, SL_TOK_EQUAL) != NULL) {
        decl->value.routine.body_kind = SL_ROUTINE_BLOCK;

        if (!SLParseBody(reader, &decl->value.routine.body)) goto fail;

        end = SLMatchToken(reader, SL_TOK_END);

        if (end == NULL) {
            // TODO: Error expected 'end'
            goto fail;
        }

        decl->span.end = SLTokenEnd(end);
    } else if (SLMatchToken(reader, SL_TOK_INLINE_FUNC) != NULL) {
        decl->value.routine.body_kind = SL_ROUTINE_EXPR;
        decl->value.routine.expr_body = SLParseExpr(reader);

        if (decl->value.routine.expr_body == NULL) {
            // TODO: Error
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
            if (item.value.decl == NULL) return false;
        } else {
            item.kind = SL_ITEM_STMT;
            item.value.stmt = SLParseStatement(reader);
            if (item.value.stmt == NULL) return false;
        }

        if (!SLBodyPush(body, item)) {
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
        // TODO: Error
        return NULL;
    }

    switch (tok->type) {
        case SL_TOK_IDENTIFIER: return SLParseIDStatement(reader);
        case SL_TOK_WHILE:      return SLParseWhile(reader);
        case SL_TOK_FOR:        return SLParseFor(reader);
        case SL_TOK_IF:         return SLParseIf(reader);
        case SL_TOK_RETURN:     return SLParseReturn(reader);
        default:
            // TODO: Error
            return NULL;
    }
}

// id_statement = id, ( call_tail | modifiable_tail, ":=", expr );
// SLParseIDPrimary already parses "id (call_tail | modifiable_tail)", so we
// reuse it and then decide: a CALL is a call statement, anything else must
// be followed by ":=".
SLStmt* SLParseIDStatement(SLTokenReader* reader) {
    SLExpr* target = SLParseIDPrimary(reader);

    if (target == NULL) {
        // TODO: Error
        return NULL;
    }

    SLStmt* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
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

    if (SLMatchToken(reader, SL_TOK_ASSIGN) == NULL) {
        // TODO: Error expected ':='
        SLFreeStmt(ret);
        return NULL;
    }

    SLExpr* value = SLParseExpr(reader);

    if (value == NULL) {
        // TODO: Error
        SLFreeStmt(ret);
        return NULL;
    }

    ret->value.assign.value = value;
    ret->span.end = value->span.end;

    return ret;
}

SLStmt* SLParseWhile(SLTokenReader* reader) {
    SLToken* kw = SLMatchToken(reader, SL_TOK_WHILE);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    SLStmt* ret = calloc(1, sizeof(*ret));
    if (ret == NULL) return NULL;

    ret->kind = SL_STMT_WHILE;
    ret->span.begin = kw->position;

    SLToken* end = NULL;

    ret->value.while_loop.condition = SLParseExpr(reader);

    if (ret->value.while_loop.condition == NULL) {
        // TODO: Error
        goto fail;
    }

    if (SLMatchToken(reader, SL_TOK_LOOP) == NULL) {
        // TODO: Error expected 'loop'
        goto fail;
    }

    if (!SLParseBody(reader, &ret->value.while_loop.body)) goto fail;

    end = SLMatchToken(reader, SL_TOK_END);

    if (end == NULL) {
        // TODO: Error expected 'end'
        goto fail;
    }

    ret->span.end = SLTokenEnd(end);
    return ret;

fail:
    SLFreeStmt(ret);
    return NULL;
}

SLStmt* SLParseFor(SLTokenReader* reader) {
    SLToken* kw = SLMatchToken(reader, SL_TOK_FOR);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    SLToken* iterator = SLMatchToken(reader, SL_TOK_IDENTIFIER);

    if (iterator == NULL) {
        // TODO: Error
        return NULL;
    }

    if (SLMatchToken(reader, SL_TOK_IN) == NULL) {
        // TODO: Error
        return NULL;
    }

    SLStmt* ret = calloc(1, sizeof(*ret));
    if (ret == NULL) return NULL;

    ret->kind = SL_STMT_FOR;
    ret->value.for_loop.iterator = iterator;
    ret->span.begin = kw->position;

    SLToken* end = NULL;

    // range = expr, [ "..", expr ]
    ret->value.for_loop.from = SLParseExpr(reader);

    if (ret->value.for_loop.from == NULL) {
        // TODO: Error
        goto fail;
    }

    if (SLMatchToken(reader, SL_TOK_RANGE) != NULL) {
        ret->value.for_loop.to = SLParseExpr(reader);

        if (ret->value.for_loop.to == NULL) {
            // TODO: Error
            goto fail;
        }
    }

    if (SLMatchToken(reader, SL_TOK_REVERSE) != NULL) {
        ret->value.for_loop.reverse = true;
    }

    if (SLMatchToken(reader, SL_TOK_LOOP) == NULL) {
        // TODO: Error expected 'loop'
        goto fail;
    }

    if (!SLParseBody(reader, &ret->value.for_loop.body)) goto fail;

    end = SLMatchToken(reader, SL_TOK_END);

    if (end == NULL) {
        // TODO: Error expected 'end'
        goto fail;
    }

    ret->span.end = SLTokenEnd(end);
    return ret;

fail:
    SLFreeStmt(ret);
    return NULL;
}

SLStmt* SLParseIf(SLTokenReader* reader) {
    SLToken* kw = SLMatchToken(reader, SL_TOK_IF);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    SLStmt* ret = calloc(1, sizeof(*ret));
    if (ret == NULL) return NULL;

    ret->kind = SL_STMT_IF;
    ret->span.begin = kw->position;

    SLToken* end = NULL;

    ret->value.if_stmt.condition = SLParseExpr(reader);

    if (ret->value.if_stmt.condition == NULL) {
        // TODO: Error
        goto fail;
    }

    if (SLMatchToken(reader, SL_TOK_THEN) == NULL) {
        // TODO: Error expected 'then'
        goto fail;
    }

    if (!SLParseBody(reader, &ret->value.if_stmt.then_body)) goto fail;

    if (SLMatchToken(reader, SL_TOK_ELSE) != NULL) {
        ret->value.if_stmt.has_else = true;

        if (!SLParseBody(reader, &ret->value.if_stmt.else_body)) goto fail;
    }

    end = SLMatchToken(reader, SL_TOK_END);

    if (end == NULL) {
        // TODO: Error expected 'end'
        goto fail;
    }

    ret->span.end = SLTokenEnd(end);
    return ret;

fail:
    SLFreeStmt(ret);
    return NULL;
}

SLStmt* SLParseReturn(SLTokenReader* reader) {
    SLToken* kw = SLMatchToken(reader, SL_TOK_RETURN);

    if (kw == NULL) {
        // TODO: Error
        return NULL;
    }

    SLExpr* value = NULL;

    if (SLStartsExpr(SLPeekToken(reader))) {
        value = SLParseExpr(reader);

        if (value == NULL) {
            // TODO: Error
            return NULL;
        }
    }

    SLStmt* ret = calloc(1, sizeof(*ret));

    if (ret == NULL) {
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
    if (program == NULL) return NULL;

    while (true) {
        SLSkipSemicolons(reader);
        SLToken* tok = SLPeekToken(reader);

        if (tok == NULL) break;

        SLDecl* decl = tok->type == SL_TOK_ROUTINE
            ? SLParseRoutineDecl(reader)
            : SLParseSimpleDecl(reader);

        if (decl == NULL) {
            for (int k = reader->cursor - 2; k <= reader->cursor; ++k) {
                if (k < 0 || (size_t) k >= reader->tokens->count) continue;
                SLToken* t = &reader->tokens->items[k];
                fprintf(stderr, "token[%d]: type=%d line=%d '%.*s'\n",
                        k, (int) t->type, t->position.line,
                        (int) t->value.length, t->value.start ? t->value.start : "");
            }
            SLFreeProgram(program);
            return NULL;
        }

        if (!SLDeclArrayPush(&program->decls, decl)) {
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
