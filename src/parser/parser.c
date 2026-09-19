#include "parser.h"

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

void SLExprDebugPrint(SLExpr* expr) {
    static _Thread_local const char* prefix = "";
    static _Thread_local const char* branch = "";

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

SLTokenReader* SLInitTokenReader(SLCompilerState* state) {
    if (state == NULL) return NULL;

    SLTokenReader* reader = (SLTokenReader*) malloc(sizeof(SLTokenReader));

    reader->tokens = &state->tokens;
    reader->cursor = 0;
    reader->isEOF = false;
    
    return reader;
}

SLToken* SLGetNextToken(SLTokenReader* reader) {
    if (reader == NULL) return NULL;

    if (reader->cursor >= reader->tokens->count) {
        reader->isEOF = true;
        return NULL;
    }

    return &reader->tokens->items[reader->cursor++];
}


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
