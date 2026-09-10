#include <stdio.h>
#include <llvm-c/Core.h>

#include "sl.h"

int main(int argc, char** argv) {

    if (argc == 1) {
        fprintf(stderr, "sl: fatal error: no input files\n");
        return 0;
    }

    SLCompilerState* state = SLInitCompilerState(argv[1]);

    if (state == NULL) {
        return 0;
    }

    if (!SLScanText(state)) {
        return 0;
    }

    return 0;
}
