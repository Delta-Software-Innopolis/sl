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

    int exit_code = 1;

    SLTokenReader* reader = NULL;
    SLProgram* program = NULL;

    SLSemanticState sem_state = {
        .compiler_state = state,
    };

    if (!SLScanText(state)) {
        goto cleanup;
    }

    reader = SLInitTokenReader(state);

    if (reader == NULL) {
        goto cleanup;
    }

    program = SLParseProgram(reader);
    if (program == NULL) {
        SLErrorPrint(&reader->error, stderr, state->filename,
                     state->text, (size_t) state->text_size);
        goto cleanup;
    }

    if (!SLAnalyzeProgram(&sem_state, program)) {
        SLSemanticErrorPrint(&sem_state, stderr);
        SLFreeSemanticState(&sem_state);
        return 1;
    }

    SLProgramDebugPrint(program);
    exit_code = 0;

cleanup:
    SLFreeSemanticState(&sem_state);
    SLFreeProgram(program);
    free(reader);
    free(state->tokens.items);
    free(state->text);
    fclose(state->input);
    free(state);

    return exit_code;
}
