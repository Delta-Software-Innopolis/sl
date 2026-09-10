#ifndef COMPILER_STATE_H
#define COMPILER_STATE_H

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include "../token/token.h"

typedef struct {
    FILE* input;
    char* text;
    int text_size;

    SLTokenArray tokens;

} SLCompilerState;

SLCompilerState* SLInitCompilerState(char* filename);


#endif