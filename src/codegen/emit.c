#include <errno.h>
#include <llvm-c/TargetMachine.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "codegen_internal.h"

extern char** environ;

static bool SLSystemError(SLCodegenContext* ctx, const char* action,
                          const char* path, int err) {
    char message[256];
    snprintf(message, sizeof(message), "%s '%s': %s", action, path,
             strerror(err));
    return SLCGFail(ctx, (SLSpan){0}, message);
}

static bool SLWriteObject(SLCodegenContext* ctx, char* path) {
    if (LLVMInitializeNativeTarget() || LLVMInitializeNativeAsmPrinter())
        return SLCGFail(ctx, (SLSpan){0}, "LLVM native target unavailable");

    char* triple = LLVMGetDefaultTargetTriple();
    char* message = NULL;

    LLVMTargetRef target;
    LLVMTargetMachineRef machine = NULL;

    if (LLVMGetTargetFromTriple(triple, &target, &message)) {
        SLCGFail(ctx, (SLSpan){0}, message);
    } else {
        machine = LLVMCreateTargetMachine(target, triple, "generic", "",
                                          LLVMCodeGenLevelDefault, LLVMRelocPIC,
                                          LLVMCodeModelDefault);

        if (!machine)
            SLCGFail(ctx, (SLSpan){0},
                     "cannot create native LLVM target machine");
    }

    if (machine) {
        LLVMTargetDataRef layout = LLVMCreateTargetDataLayout(machine);
        LLVMSetTarget(ctx->module, triple);
        LLVMSetModuleDataLayout(ctx->module, layout);

        if (LLVMTargetMachineEmitToFile(machine, ctx->module, path,
                                        LLVMObjectFile, &message))
            SLCGFail(ctx, (SLSpan){0}, message);

        LLVMDisposeTargetData(layout);
        LLVMDisposeTargetMachine(machine);
    }

    LLVMDisposeMessage(message);
    LLVMDisposeMessage(triple);

    return !ctx->error.has_error;
}

static bool SLLink(SLCodegenContext* ctx, char* object, char* output) {
    char* argv[] = {SL_CLANG_PATH,
#ifdef SL_SYSROOT
                    "-isysroot",   SL_SYSROOT,
#endif
                    "-o",          output,          "--",
                    object,        SL_RUNTIME_PATH, NULL};
    pid_t child;

    int err = posix_spawn(&child, SL_CLANG_PATH, NULL, NULL, argv, environ);

    if (err)
        return SLSystemError(ctx, "cannot launch linker driver", SL_CLANG_PATH,
                             err);
    int status;

    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return SLSystemError(ctx, "cannot wait for", SL_CLANG_PATH, errno);
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;

    char message[128];

    if (WIFEXITED(status))
        snprintf(message, sizeof(message), "clang link failed (exit status %d)",
                 WEXITSTATUS(status));
    else
        snprintf(message, sizeof(message),
                 "clang link interrupted by signal %d", WTERMSIG(status));

    return SLCGFail(ctx, (SLSpan){0}, message);
}

bool SLCGEmit(SLCodegenContext* ctx, const SLCodegenOptions* options) {
    const char* dest = options->output_path;
    const char* slash = strrchr(dest, '/');

    size_t prefix = slash ? (size_t)(slash - dest + 1) : 0;
    const char suffix[] = ".sl-XXXXXX";
    size_t dir_size = prefix + sizeof(suffix);

    char* dir = malloc(dir_size);
    char* object = malloc(dir_size + sizeof("/module.o"));
    char* output = malloc(dir_size + sizeof("/result"));

    if (!dir || !object || !output) {
        SLCGFail(ctx, (SLSpan){0}, "out of memory preparing output");
        goto cleanup;
    }

    memcpy(dir, dest, prefix);
    memcpy(dir + prefix, suffix, sizeof(suffix));

    if (!mkdtemp(dir)) {
        SLSystemError(ctx, "cannot prepare output", dest, errno);
        goto cleanup;
    }

    snprintf(object, dir_size + sizeof("/module.o"), "%s/module.o", dir);
    snprintf(output, dir_size + sizeof("/result"), "%s/result", dir);

    switch (options->output) {
        case SL_OUTPUT_LLVM_IR: {

            char* error = NULL;

            if (LLVMPrintModuleToFile(ctx->module, output, &error))
                SLCGFail(ctx, (SLSpan){0}, error);

            LLVMDisposeMessage(error);

            break;
        }

        case SL_OUTPUT_OBJECT:
            SLWriteObject(ctx, output);
            break;

        case SL_OUTPUT_EXECUTABLE:
            if (SLWriteObject(ctx, object)) SLLink(ctx, object, output);
            break;

        default:
            SLCGFail(ctx, (SLSpan){0}, "unknown output mode");
    }

    if (!ctx->error.has_error && rename(output, dest) != 0)
        SLSystemError(ctx, "cannot publish output", dest, errno);

    unlink(object);
    unlink(output);
    rmdir(dir);

cleanup:
    free(dir);
    free(object);
    free(output);

    return !ctx->error.has_error;
}
