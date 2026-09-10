#ifndef LEXER_H
#define LEXER_H

#pragma once

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "../compiler_state/compiler_state.h"
#include "../token/token.h"

bool SLScanText(SLCompilerState* state);

#endif