#ifndef dictu_include_h
#define dictu_include_h

#include <stdbool.h>
#include <stdint.h>

#define DICTU_MAJOR_VERSION "0"
#define DICTU_MINOR_VERSION "19"
#define DICTU_PATCH_VERSION "0"

#define DICTU_STRING_VERSION "Dictu Version: " DICTU_MAJOR_VERSION "." DICTU_MINOR_VERSION "." DICTU_PATCH_VERSION "\n"

typedef struct _vm DictuVM;

typedef uint64_t Value;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} DictuInterpretResult;

DictuVM *dictuInitVM(bool repl, int argc, char *argv[]);

void dictuFreeVM(DictuVM *vm);

DictuInterpretResult dictuInterpret(DictuVM *vm, char *moduleName, char *source);

/**
 * Binds the `value` in `vm` to a native reference handle, preventing it from being collected by the
 * garbage collector until it is later released with `unbindRef`.
 *
 * Users of the reference API should be very careful that they do not leak memory, as it circumvents
 * the garbage collector entirely by moving the value data out of its purview.
 */
uint32_t bindRef(DictuVM *vm, Value value);

/**
 * Pushes the value stored at `ref` onto the stack of `vm`, pushing `NIL_VAL` if `ref` is not a
 * valid registry handle.
 */
void pushRef(DictuVM *vm, uint32_t ref);

/**
 * Unbinds the value reference `ref`, returning control of the data lifetime back to the garbage
 * collector.
 */
void unbindRef(DictuVM *vm, uint32_t ref);

#endif //dictu_include_h
