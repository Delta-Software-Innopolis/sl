#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

void print_integer(int64_t value) { printf("%" PRId64 "\n", value); }

void print_real(double value) { printf("%.17g\n", value); }

void print_boolean(bool value) { puts(value ? "true" : "false"); }
