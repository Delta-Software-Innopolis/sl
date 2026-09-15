#include <llvm-c/Core.h>
#include <stdio.h>

#include "sl.h"

int main(int argc, char** argv) {
    if (argc == 1) {
        fprintf(stderr, "sl: fatal error: no input files\n");
        return 1;
    }

    SLCompilerState* state = SLInitCompilerState(argv[1]);

    if (state == NULL) {
        return 1;
    }

    if (!SLScanText(state)) {
        return 1;
    }

    return 0;
}
