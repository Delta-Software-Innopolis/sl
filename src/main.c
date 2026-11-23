#include <stdio.h>
#include <sys/stat.h>

#include "codegen/codegen.h"
#include "sl.h"

static const char usage[] = "usage: sl [--emit-llvm | -c | --dump-ast] input.sl [-o output]\n";

static char* SLDefaultOutput(const char* input, SLCodegenOutput mode) {
    if (mode == SL_OUTPUT_EXECUTABLE) return strdup("a.out");

    const char* slash = strrchr(input, '/');
    const char* base = slash ? slash + 1 : input;
    const char* dot = strrchr(base, '.');

    size_t stem = dot && dot != base ? (size_t)(dot - base) : strlen(base);

    const char* suffix = mode == SL_OUTPUT_OBJECT ? ".o" : ".ll";

    char* path = malloc(stem + strlen(suffix) + 1);

    if (path) {
        memcpy(path, base, stem);
        strcpy(path + stem, suffix);
    }

    return path;
}

int main(int argc, char** argv) {
    SLCodegenOptions options = {.output = SL_OUTPUT_EXECUTABLE};

    char* input = NULL;
    bool dump_ast = false, mode_selected = false, positional = false;

    for (int i = 1; i < argc; ++i) {
        char* arg = argv[i];

        if (positional || arg[0] != '-') {
            if (input) goto invalid;

            input = arg;
        } else if (!strcmp(arg, "--")) {
            positional = true;
        } else if (!strcmp(arg, "-o")) {
            if (options.output_path || ++i == argc || !*argv[i]) goto invalid;

            options.output_path = argv[i];
        } else if (!strcmp(arg, "--help")) {
            fputs(usage, stdout);

            return 0;
        } else {
            if (mode_selected) goto invalid;

            mode_selected = true;

            if (!strcmp(arg, "--dump-ast"))
                dump_ast = true;
            else if (!strcmp(arg, "--emit-llvm"))
                options.output = SL_OUTPUT_LLVM_IR;
            else if (!strcmp(arg, "-c"))
                options.output = SL_OUTPUT_OBJECT;
            else
                goto invalid;
        }
    }

    if (!input || (dump_ast && options.output_path)) goto invalid;

    SLCompilerState* state = SLInitCompilerState(input);
    if (!state) return 1;

    char* default_path = NULL;
    int exit_code = 1;

    SLTokenReader* reader = NULL;
    SLProgram* program = NULL;

    SLSemanticState sem_state = {
        .compiler_state = state,
    };

    SLOptStats opt_stats = {0};

    if (!SLScanText(state)) {
        goto cleanup;
    }

    reader = SLInitTokenReader(state);

    if (reader == NULL) {
        goto cleanup;
    }

    program = SLParseProgram(reader);

    if (program == NULL) {
        SLErrorPrint(&reader->error, stderr, state->filename, state->text,
                     (size_t)state->text_size);
        goto cleanup;
    }

    if (!SLAnalyzeAndOptimize(&sem_state, program, &opt_stats)) {
        SLSemanticErrorPrint(&sem_state, stderr);
        goto cleanup;
    }

    if (dump_ast) {
        SLProgramDebugPrint(program);
    } else {
        if (!options.output_path) {

            default_path = SLDefaultOutput(input, options.output);

            if (!default_path) {
                fputs("sl: out of memory choosing output path\n", stderr);
                goto cleanup;
            }

            options.output_path = default_path;
        }

        struct stat source, dest;

        if (!fstat(fileno(state->input), &source) &&
            !stat(options.output_path, &dest) && source.st_dev == dest.st_dev &&
            source.st_ino == dest.st_ino) {

            fputs("sl: input and output refer to the same file\n", stderr);

            goto cleanup;
        }

        SLCodegenError error;

        if (!SLGenerateCode(program, &sem_state, &options, &error)) {
            if (error.span.begin.line)
                fprintf(stderr, "%s:%d:%d: %s\n", input, error.span.begin.line,
                        error.span.begin.position, error.message);
            else
                fprintf(stderr, "sl: %s\n", error.message);
            goto cleanup;
        }
    }
    exit_code = 0;

cleanup:
    free(default_path);
    SLFreeSemanticState(&sem_state);
    SLFreeProgram(program);
    
    free(reader);
    free(state->tokens.items);
    free(state->text);
    fclose(state->input);
    free(state);

    return exit_code;

invalid:
    fputs(usage, stderr);
    return 1;
}
