#include "compiler_state.h"


SLCompilerState* SLInitCompilerState(char* filename) {
    SLCompilerState* state = malloc(sizeof(*state));
    if (state == NULL) {
        return NULL;
    }

    state->input = fopen(filename, "rb");
    if (state->input == NULL) {
        free(state);

        fprintf(stderr, "sl: error: no such file or directory: %s\n", filename);
        return NULL;
    }

    if (fseek(state->input, 0, SEEK_END) != 0) {
        fclose(state->input);
        free(state);
        return NULL;
    }

    long file_size = ftell(state->input);
    if (file_size < 0) {
        fclose(state->input);
        free(state);
        return NULL;
    }

    rewind(state->input);

    state->text = malloc((size_t)file_size + 1);
    if (state->text == NULL) {
        fclose(state->input);
        free(state);
        return NULL;
    }

    size_t bytes_read = fread(
        state->text,
        1,
        (size_t)file_size,
        state->input
    );

    if (bytes_read != (size_t)file_size) {
        free(state->text);
        fclose(state->input);
        free(state);
        return NULL;
    }

    state->text[bytes_read] = '\0';
    state->text_size = file_size;

    rewind(state->input);

    state->tokens = (SLTokenArray){0};

    return state;
}
