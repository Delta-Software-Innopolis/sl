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

    SLTokenReader* reader = SLInitTokenReader(state);

    if (reader == NULL) {
        return 1;
    }

    SLProgram* prog = SLParseProgram(reader);

    SLProgram* program = SLParseProgram(reader);
    if (program == NULL) {
        SLErrorPrint(&reader->error, stderr, state->filename,
                     state->text, (size_t) state->text_size);
        return 1;
    }

    SLProgramDebugPrint(prog);


    free(state);

    return 0;
}
