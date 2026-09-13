#ifndef LEXER_H
#define LEXER_H

#pragma once

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../compiler_state/compiler_state.h"
#include "../token/token.h"

bool SLScanText(SLCompilerState* state);

#endif