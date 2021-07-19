#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"
#include "util.h"
#include "error_lib/error.h"
#include "datatypes/number.h"
#include "datatypes/bool.h"
#include "datatypes/nil.h"
#include "datatypes/strings.h"
#include "datatypes/lists/lists.h"
#include "datatypes/dicts/dicts.h"
#include "datatypes/sets.h"
#include "datatypes/files.h"
#include "datatypes/class.h"
#include "datatypes/instance.h"
#include "datatypes/result/result.h"
#include "natives.h"
#include "../optionals/optionals.h"

static void resetStack(DictuVM *vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
    vm->compiler = NULL;
}

void runtimeError(DictuVM *vm, const char *format, ...) {
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];

        ObjFunction *function = frame->closure->function;

        // -1 because the IP is sitting on the next instruction to be
        // executed.
        size_t instruction = frame->ip - function->chunk.code - 1;

        if (function->name == NULL) {
            log_error("File '%s', {bold}line %d{reset}", function->module->name->chars, function->chunk.lines[instruction]);
            i = -1;
        } else {
            log_error("Function '%s' in '%s', {bold}line %d{reset}", function->name->chars, function->module->name->chars, function->chunk.lines[instruction]);
        }

        log_pad("");

        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        fputs("\n\n", stderr);
        va_end(args);
    }

    resetStack(vm);
}

DictuVM *dictuInitVM(bool repl, int argc, char *argv[]) {
    DictuVM *vm = malloc(sizeof(*vm));

    if (vm == NULL) {
        printf("Unable to allocate memory\n");
        exit(71);
    }

    memset(vm, '\0', sizeof(DictuVM));

    resetStack(vm);
    vm->objects = NULL;
    vm->repl = repl;
    vm->frameCapacity = 4;
    vm->frames = NULL;
    vm->initString = NULL;
    vm->replVar = NULL;
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->grayStack = NULL;
    vm->lastModule = NULL;
    initTable(&vm->modules);
    initTable(&vm->globals);
    initTable(&vm->constants);
    initTable(&vm->strings);

    initTable(&vm->numberMethods);
    initTable(&vm->boolMethods);
    initTable(&vm->nilMethods);
    initTable(&vm->stringMethods);
    initTable(&vm->listMethods);
    initTable(&vm->dictMethods);
    initTable(&vm->setMethods);
    initTable(&vm->fileMethods);
    initTable(&vm->classMethods);
    initTable(&vm->instanceMethods);
    initTable(&vm->resultMethods);

    vm->frames = ALLOCATE(vm, CallFrame, vm->frameCapacity);
    vm->initString = copyString(vm, "init", 4);

    // Native functions
    defineAllNatives(vm);

    // Native methods
    declareNumberMethods(vm);
    declareBoolMethods(vm);
    declareNilMethods(vm);
    declareStringMethods(vm);
    declareListMethods(vm);
    declareDictMethods(vm);
    declareSetMethods(vm);
    declareFileMethods(vm);
    declareClassMethods(vm);
    declareInstanceMethods(vm);
    declareResultMethods(vm);

    /**
     * Native classes which are not required to be
     * imported. For imported modules see optionals.c
     */
    createSystemModule(vm, argc, argv);
    createCModule(vm);

    if (vm->repl) {
        vm->replVar = copyString(vm, "_", 1);
    }

    return vm;
}

void dictuFreeVM(DictuVM *vm) {
    if (vm->repl) {
        freeTable(vm, &vm->constants);
    }

    freeTable(vm, &vm->modules);
    freeTable(vm, &vm->globals);
    freeTable(vm, &vm->constants);
    freeTable(vm, &vm->strings);
    freeTable(vm, &vm->numberMethods);
    freeTable(vm, &vm->boolMethods);
    freeTable(vm, &vm->nilMethods);
    freeTable(vm, &vm->stringMethods);
    freeTable(vm, &vm->listMethods);
    freeTable(vm, &vm->dictMethods);
    freeTable(vm, &vm->setMethods);
    freeTable(vm, &vm->fileMethods);
    freeTable(vm, &vm->classMethods);
    freeTable(vm, &vm->instanceMethods);
    freeTable(vm, &vm->resultMethods);
    FREE_ARRAY(vm, CallFrame, vm->frames, vm->frameCapacity);
    vm->initString = NULL;
    vm->replVar = NULL;
    freeObjects(vm);

#if defined(DEBUG_TRACE_MEM) || defined(DEBUG_FINAL_MEM)
#ifdef __MINGW32__
    printf("Total memory usage: %lu\n", (unsigned long)vm->bytesAllocated);
#else
    printf("Total memory usage: %zu\n", vm->bytesAllocated);
#endif
#endif

    free(vm);
}

void push(DictuVM *vm, Value value) {
    *vm->stackTop = value;
    vm->stackTop++;
}

Value pop(DictuVM *vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

Value peek(DictuVM *vm, int distance) {
    return vm->stackTop[-1 - distance];
}

static bool call(DictuVM *vm, ObjClosure *closure, int argCount) {
    if (argCount < closure->function->arity || argCount > closure->function->arity + closure->function->arityOptional) {
        runtimeError(vm, "Function '%s' expected %d argument(s) but got %d.",
                     closure->function->name->chars,
                     closure->function->arity + closure->function->arityOptional,
                     argCount
        );

        return false;
    }
    if (vm->frameCount == vm->frameCapacity) {
        int oldCapacity = vm->frameCapacity;
        vm->frameCapacity = GROW_CAPACITY(vm->frameCapacity);
        vm->frames = GROW_ARRAY(vm, vm->frames, CallFrame,
                                   oldCapacity, vm->frameCapacity);
    }

    CallFrame *frame = &vm->frames[vm->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;

    frame->slots = vm->stackTop - argCount - 1;

    return true;
}

static bool callValue(DictuVM *vm, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod *bound = AS_BOUND_METHOD(callee);

                // Replace the bound method with the receiver so it's in the
                // right slot when the method is called.
                vm->stackTop[-argCount - 1] = bound->receiver;
                return call(vm, bound->method, argCount);
            }

            case OBJ_CLASS: {
                // If it's not a default class, e.g a trait, it is not callable
                if (!(IS_DEFAULT_CLASS(callee))) {
                    break;
                }

                ObjClass *klass = AS_CLASS(callee);

                // Create the instance.
                vm->stackTop[-argCount - 1] = OBJ_VAL(newInstance(vm, klass));

                // Call the initializer, if there is one.
                Value initializer;
                if (tableGet(&klass->publicMethods, vm->initString, &initializer)) {
                    return call(vm, AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError(vm, "Expected 0 arguments but got %d.", argCount);
                    return false;
                }

                return true;
            }

            case OBJ_CLOSURE: {
                vm->stackTop[-argCount - 1] = callee;
                return call(vm, AS_CLOSURE(callee), argCount);
            }

            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(vm, argCount, vm->stackTop - argCount);

                if (IS_EMPTY(result))
                    return false;

                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }

            default:
                // Do nothing.
                break;
        }
    }

    int valLength = 0;
    char *val = valueTypeToString(vm, callee, &valLength);
    runtimeError(vm, "'%s' is not callable", val);
    FREE_ARRAY(vm, char, val, valLength + 1);
    return false;
}

static bool callNativeMethod(DictuVM *vm, Value method, int argCount) {
    NativeFn native = AS_NATIVE(method);

    Value result = native(vm, argCount, vm->stackTop - argCount - 1);

    if (IS_EMPTY(result))
        return false;

    vm->stackTop -= argCount + 1;
    push(vm, result);
    return true;
}

static bool invokeFromClass(DictuVM *vm, ObjClass *klass, ObjString *name,
                            int argCount) {
    // Look for the method.
    Value method;
    if (!tableGet(&klass->publicMethods, name, &method)) {
        if (tableGet(&klass->privateMethods, name, &method)) {
            runtimeError(vm, "Cannot access private property '%s' from superclass '%s'.", name->chars, klass->name->chars);
            return false;
        }

        runtimeError(vm, "Undefined property '%s'.", name->chars);
        return false;
    }

    return call(vm, AS_CLOSURE(method), argCount);
}

static bool invokeInternal(DictuVM *vm, ObjString *name, int argCount) {
    Value receiver = peek(vm, argCount);

    if (IS_INSTANCE(receiver)) {
        ObjInstance *instance = AS_INSTANCE(receiver);

        Value value;
        // Look for the method.
        if (tableGet(&instance->klass->privateMethods, name, &value)) {
            return call(vm, AS_CLOSURE(value), argCount);
        }

        if (tableGet(&instance->klass->publicMethods, name, &value)) {
            return call(vm, AS_CLOSURE(value), argCount);
        }

        // Check for instance methods.
        if (tableGet(&vm->instanceMethods, name, &value)) {
            return callNativeMethod(vm, value, argCount);
        }

        // Look for a field which may shadow a method.
        if (tableGet(&instance->publicFields, name, &value)) {
            vm->stackTop[-argCount - 1] = value;
            return callValue(vm, value, argCount);
        }
    } else if (IS_CLASS(receiver)) {
        ObjClass *instance = AS_CLASS(receiver);
        Value method;
        if (tableGet(&instance->privateMethods, name, &method)) {
            if (AS_CLOSURE(method)->function->type != TYPE_STATIC) {
                if (tableGet(&vm->classMethods, name, &method)) {
                    return callNativeMethod(vm, method, argCount);
                }

                runtimeError(vm, "'%s' is not static. Only static methods can be invoked directly from a class.",
                             name->chars);
                return false;
            }

            return callValue(vm, method, argCount);
        }

        if (tableGet(&instance->publicMethods, name, &method)) {
            if (AS_CLOSURE(method)->function->type != TYPE_STATIC) {
                if (tableGet(&vm->classMethods, name, &method)) {
                    return callNativeMethod(vm, method, argCount);
                }

                runtimeError(vm, "'%s' is not static. Only static methods can be invoked directly from a class.",
                             name->chars);
                return false;
            }

            return callValue(vm, method, argCount);
        }

        if (tableGet(&vm->classMethods, name, &method)) {
            return callNativeMethod(vm, method, argCount);
        }
    }

    runtimeError(vm, "Undefined property '%s'.", name->chars);
    return false;
}

static bool invoke(DictuVM *vm, ObjString *name, int argCount) {
    Value receiver = peek(vm, argCount);

    if (!IS_OBJ(receiver)) {
        if (IS_NUMBER(receiver)) {
            Value value;
            if (tableGet(&vm->numberMethods, name, &value)) {
                return callNativeMethod(vm, value, argCount);
            }

            runtimeError(vm, "Number has no method %s().", name->chars);
            return false;
        } else if (IS_BOOL(receiver)) {
            Value value;
            if (tableGet(&vm->boolMethods, name, &value)) {
                return callNativeMethod(vm, value, argCount);
            }

            runtimeError(vm, "Bool has no method %s().", name->chars);
            return false;
        } else if (IS_NIL(receiver)) {
            Value value;
            if (tableGet(&vm->nilMethods, name, &value)) {
                return callNativeMethod(vm, value, argCount);
            }

            runtimeError(vm, "Nil has no method %s().", name->chars);
            return false;
        }
    } else {
        switch (getObjType(receiver)) {
            case OBJ_MODULE: {
                ObjModule *module = AS_MODULE(receiver);

                Value value;
                if (!tableGet(&module->values, name, &value)) {
                    runtimeError(vm, "Undefined property '%s'.", name->chars);
                    return false;
                }
                return callValue(vm, value, argCount);
            }

            case OBJ_CLASS: {
                ObjClass *instance = AS_CLASS(receiver);
                Value method;
                if (tableGet(&instance->publicMethods, name, &method)) {
                    if (AS_CLOSURE(method)->function->type != TYPE_STATIC) {
                        if (tableGet(&vm->classMethods, name, &method)) {
                            return callNativeMethod(vm, method, argCount);
                        }

                        runtimeError(vm, "'%s' is not static. Only static methods can be invoked directly from a class.",
                                     name->chars);
                        return false;
                    }

                    return callValue(vm, method, argCount);
                }

                if (tableGet(&vm->classMethods, name, &method)) {
                    return callNativeMethod(vm, method, argCount);
                }

                runtimeError(vm, "Undefined property '%s'.", name->chars);
                return false;
            }

            case OBJ_INSTANCE: {
                ObjInstance *instance = AS_INSTANCE(receiver);

                Value value;
                // Look for the method.
                if (tableGet(&instance->klass->publicMethods, name, &value)) {
                    return call(vm, AS_CLOSURE(value), argCount);
                }

                // Check for instance methods.
                if (tableGet(&vm->instanceMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                // Look for a field which may shadow a method.
                if (tableGet(&instance->publicFields, name, &value)) {
                    vm->stackTop[-argCount - 1] = value;
                    return callValue(vm, value, argCount);
                }

                if (tableGet(&instance->klass->privateMethods, name, &value)) {
                    runtimeError(vm, "Cannot access private property '%s' on '%s' instance.", name->chars, instance->klass->name->chars);
                    return false;
                }

                runtimeError(vm, "Undefined property '%s'.", name->chars);
                return false;
            }

            case OBJ_STRING: {
                Value value;
                if (tableGet(&vm->stringMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "String has no method %s().", name->chars);
                return false;
            }

            case OBJ_LIST: {
                Value value;
                if (tableGet(&vm->listMethods, name, &value)) {
                    if (IS_NATIVE(value)) {
                        return callNativeMethod(vm, value, argCount);
                    }

                    push(vm, peek(vm, 0));

                    for (int i = 2; i <= argCount + 1; i++) {
                        vm->stackTop[-i] = peek(vm, i);
                    }

                    return call(vm, AS_CLOSURE(value), argCount + 1);
                }

                runtimeError(vm, "List has no method %s().", name->chars);
                return false;
            }

            case OBJ_DICT: {
                Value value;
                if (tableGet(&vm->dictMethods, name, &value)) {
                    if (IS_NATIVE(value)) {
                        return callNativeMethod(vm, value, argCount);
                    }

                    push(vm, peek(vm, 0));

                    for (int i = 2; i <= argCount + 1; i++) {
                        vm->stackTop[-i] = peek(vm, i);
                    }

                    return call(vm, AS_CLOSURE(value), argCount + 1);
                }

                runtimeError(vm, "Dict has no method %s().", name->chars);
                return false;
            }

            case OBJ_SET: {
                Value value;
                if (tableGet(&vm->setMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "Set has no method %s().", name->chars);
                return false;
            }

            case OBJ_FILE: {
                Value value;
                if (tableGet(&vm->fileMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "File has no method %s().", name->chars);
                return false;
            }

            case OBJ_RESULT: {
                Value value;
                if (tableGet(&vm->resultMethods, name, &value)) {
                    if (IS_NATIVE(value)) {
                        return callNativeMethod(vm, value, argCount);
                    }

                    push(vm, peek(vm, 0));

                    for (int i = 2; i <= argCount + 1; i++) {
                        vm->stackTop[-i] = peek(vm, i);
                    }

                    return call(vm, AS_CLOSURE(value), argCount + 1);
                }

                runtimeError(vm, "Result has no method %s().", name->chars);
                return false;
            }

            case OBJ_ABSTRACT: {
                ObjAbstract *abstract = AS_ABSTRACT(receiver);

                Value value;
                if (tableGet(&abstract->values, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "Object has no method %s().", name->chars);
                return false;
            }

            case OBJ_ENUM: {
                ObjEnum *enumObj = AS_ENUM(receiver);

                Value value;
                if (tableGet(&enumObj->values, name, &value)) {
                    return callValue(vm, value, argCount);
                }

                runtimeError(vm, "'%s' enum has no property '%s'.", enumObj->name->chars, name->chars);
                return false;
            }

            default:
                break;
        }
    }

    runtimeError(vm, "Only instances have methods.");
    return false;
}

static bool bindMethod(DictuVM *vm, ObjClass *klass, ObjString *name) {
    Value method;
    if (!tableGet(&klass->publicMethods, name, &method)) {
        return false;
    }

    ObjBoundMethod *bound = newBoundMethod(vm, peek(vm, 0), AS_CLOSURE(method));
    pop(vm); // Instance.
    push(vm, OBJ_VAL(bound));
    return true;
}

// Captures the local variable [local] into an [Upvalue]. If that local
// is already in an upvalue, the existing one is used. (This is
// important to ensure that multiple closures closing over the same
// variable actually see the same variable.) Otherwise, it creates a
// new open upvalue and adds it to the DictuVM's list of upvalues.
static ObjUpvalue *captureUpvalue(DictuVM *vm, Value *local) {
    // If there are no open upvalues at all, we must need a new one.
    if (vm->openUpvalues == NULL) {
        vm->openUpvalues = newUpvalue(vm, local);
        return vm->openUpvalues;
    }

    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm->openUpvalues;

    // Walk towards the bottom of the stack until we find a previously
    // existing upvalue or reach where it should be.
    while (upvalue != NULL && upvalue->value > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    // If we found it, reuse it.
    if (upvalue != NULL && upvalue->value == local) return upvalue;

    // We walked past the local on the stack, so there must not be an
    // upvalue for it already. Make a new one and link it in in the right
    // place to keep the list sorted.
    ObjUpvalue *createdUpvalue = newUpvalue(vm, local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        // The new one is the first one in the list.
        vm->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(DictuVM *vm, Value *last) {
    while (vm->openUpvalues != NULL &&
           vm->openUpvalues->value >= last) {
        ObjUpvalue *upvalue = vm->openUpvalues;

        // Move the value into the upvalue itself and point the upvalue to
        // it.
        upvalue->closed = *upvalue->value;
        upvalue->value = &upvalue->closed;

        // Pop it off the open upvalue list.
        vm->openUpvalues = upvalue->next;
    }
}

static void defineMethod(DictuVM *vm, ObjString *name) {
    Value method = peek(vm, 0);
    ObjClass *klass = AS_CLASS(peek(vm, 1));
    ObjFunction *function = AS_CLOSURE(method)->function;

    if (function->accessLevel == ACCESS_PRIVATE) {
        tableSet(vm, &klass->privateMethods, name, method);
    } else {
        if (function->type == TYPE_ABSTRACT) {
            tableSet(vm, &klass->abstractMethods, name, method);
        } else {
            tableSet(vm, &klass->publicMethods, name, method);
        }
    }

    pop(vm);
}

static void createClass(DictuVM *vm, ObjString *name, ObjClass *superclass, ClassType type) {
    ObjClass *klass = newClass(vm, name, superclass, type);
    push(vm, OBJ_VAL(klass));

    // Inherit methods.
    if (superclass != NULL) {
        tableAddAll(vm, &superclass->publicMethods, &klass->publicMethods);
        tableAddAll(vm, &superclass->abstractMethods, &klass->abstractMethods);
    }
}

bool isFalsey(Value value) {
    return IS_NIL(value) ||
           (IS_BOOL(value) && !AS_BOOL(value)) ||
           (IS_NUMBER(value) && AS_NUMBER(value) == 0) ||
           (IS_STRING(value) && AS_CSTRING(value)[0] == '\0') ||
           (IS_LIST(value) && AS_LIST(value)->values.count == 0) ||
           (IS_DICT(value) && AS_DICT(value)->count == 0) ||
           (IS_SET(value) && AS_SET(value)->count == 0);
}

static void concatenate(DictuVM *vm) {
    ObjString *b = AS_STRING(peek(vm, 0));
    ObjString *a = AS_STRING(peek(vm, 1));

    int length = a->length + b->length;
    char *chars = ALLOCATE(vm, char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString *result = takeString(vm, chars, length);

    pop(vm);
    pop(vm);

    push(vm, OBJ_VAL(result));
}

static void setReplVar(DictuVM *vm, Value value) {
    tableSet(vm, &vm->globals, vm->replVar, value);
}

static DictuInterpretResult run(DictuVM *vm) {

    CallFrame *frame = &vm->frames[vm->frameCount - 1];
    register uint8_t* ip = frame->ip;

    #define READ_BYTE() (*ip++)
    #define READ_SHORT() \
        (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))

    #define READ_CONSTANT() \
                (frame->closure->function->chunk.constants.values[READ_BYTE()])

    #define READ_STRING() AS_STRING(READ_CONSTANT())

    #define UNSUPPORTED_OPERAND_TYPE_ERROR(op)                                                      \
        int firstValLength = 0;                                                                     \
        int secondValLength = 0;                                                                    \
        char *firstVal = valueTypeToString(vm, peek(vm, 1), &firstValLength);                       \
        char *secondVal = valueTypeToString(vm, peek(vm, 0), &secondValLength);                     \
                                                                                                    \
        STORE_FRAME;                                                                                \
        runtimeError(vm, "Unsupported operand types for "#op": '%s', '%s'", firstVal, secondVal);   \
        FREE_ARRAY(vm, char, firstVal, firstValLength + 1);                                         \
        FREE_ARRAY(vm, char, secondVal, secondValLength + 1);                                       \
        return INTERPRET_RUNTIME_ERROR;                                                             \

    #define BINARY_OP(valueType, op, type)                                                                \
        do {                                                                                              \
          if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {                                       \
              UNSUPPORTED_OPERAND_TYPE_ERROR(op)                                                          \
          }                                                                                               \
                                                                                                          \
          type b = AS_NUMBER(pop(vm));                                                                    \
          type a = AS_NUMBER(peek(vm, 0));                                                                \
          vm->stackTop[-1] = valueType(a op b);                                                           \
        } while (false)

    #define BINARY_OP_FUNCTION(valueType, op, func, type)                                                 \
        do {                                                                                              \
          if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {                                       \
              UNSUPPORTED_OPERAND_TYPE_ERROR(op)                                                          \
          }                                                                                               \
                                                                                                          \
          type b = AS_NUMBER(pop(vm));                                                                    \
          type a = AS_NUMBER(peek(vm, 0));                                                                \
          vm->stackTop[-1] = valueType(func(a, b));                                                       \
        } while (false)

    #define STORE_FRAME frame->ip = ip

    #define RUNTIME_ERROR(...)                                              \
        do {                                                                \
            STORE_FRAME;                                                    \
            runtimeError(vm, __VA_ARGS__);                                  \
            return INTERPRET_RUNTIME_ERROR;                                 \
        } while (0)

    #define RUNTIME_ERROR_TYPE(error, distance)                                    \
        do {                                                                       \
            STORE_FRAME;                                                           \
            int valLength = 0;                                                     \
            char *val = valueTypeToString(vm, peek(vm, distance), &valLength);     \
            runtimeError(vm, error, val);                                          \
            FREE_ARRAY(vm, char, val, valLength + 1);                              \
            return INTERPRET_RUNTIME_ERROR;                                        \
        } while (0)

    #ifdef COMPUTED_GOTO

    static void* dispatchTable[] = {
        #define OPCODE(name) &&op_##name,
        #include "opcodes.h"
        #undef OPCODE
    };

    #define INTERPRET_LOOP    DISPATCH();
    #define CASE_CODE(name)   op_##name

    #ifdef DEBUG_TRACE_EXECUTION
        #define DISPATCH()                                                                        \
            do                                                                                    \
            {                                                                                     \
                printf("          ");                                                             \
                for (Value *stackValue = vm->stack; stackValue < vm->stackTop; stackValue++) {    \
                    printf("[ ");                                                                 \
                    printValue(*stackValue);                                                      \
                    printf(" ]");                                                                 \
                }                                                                                 \
                printf("\n");                                                                     \
                disassembleInstruction(&frame->closure->function->chunk,                          \
                        (int) (ip - frame->closure->function->chunk.code));                \
                goto *dispatchTable[instruction = READ_BYTE()];                                   \
            }                                                                                     \
            while (false)
    #else
        #define DISPATCH()                                            \
            do                                                        \
            {                                                         \
                goto *dispatchTable[instruction = READ_BYTE()];       \
            }                                                         \
            while (false)
    #endif

    #else

    #define INTERPRET_LOOP                                        \
            loop:                                                 \
                switch (instruction = READ_BYTE())

    #define DISPATCH() goto loop

    #define CASE_CODE(name) case OP_##name

    #endif

    uint8_t instruction;
    INTERPRET_LOOP
    {
        CASE_CODE(CONSTANT): {
            Value constant = READ_CONSTANT();
            push(vm, constant);
            DISPATCH();
        }

        CASE_CODE(NIL):
            push(vm, NIL_VAL);
            DISPATCH();

        CASE_CODE(EMPTY):
            push(vm, EMPTY_VAL);
            DISPATCH();

        CASE_CODE(TRUE):
            push(vm, BOOL_VAL(true));
            DISPATCH();

        CASE_CODE(FALSE):
            push(vm, BOOL_VAL(false));
            DISPATCH();

        CASE_CODE(POP_REPL): {
            Value v = pop(vm);
            if (!IS_NIL(v)) {
                setReplVar(vm, v);
                printValue(v);
                printf("\n");
            }
            DISPATCH();
        }

        CASE_CODE(POP): {
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(GET_LOCAL): {
            uint8_t slot = READ_BYTE();
            push(vm, frame->slots[slot]);
            DISPATCH();
        }

        CASE_CODE(SET_LOCAL): {
            uint8_t slot = READ_BYTE();
            frame->slots[slot] = peek(vm, 0);
            DISPATCH();
        }

        CASE_CODE(GET_GLOBAL): {
            ObjString *name = READ_STRING();
            Value value;
            if (!tableGet(&vm->globals, name, &value)) {
                RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
            }
            push(vm, value);
            DISPATCH();
        }

        CASE_CODE(GET_MODULE): {
            ObjString *name = READ_STRING();
            Value value;
            if (!tableGet(&frame->closure->function->module->values, name, &value)) {
                RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
            }
            push(vm, value);
            DISPATCH();
        }

        CASE_CODE(DEFINE_MODULE): {
            ObjString *name = READ_STRING();
            tableSet(vm, &frame->closure->function->module->values, name, peek(vm, 0));
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(SET_MODULE): {
            ObjString *name = READ_STRING();
            if (tableSet(vm, &frame->closure->function->module->values, name, peek(vm, 0))) {
                tableDelete(vm, &frame->closure->function->module->values, name);
                RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
            }
            DISPATCH();
        }

        CASE_CODE(DEFINE_OPTIONAL): {
            int arity = READ_BYTE();
            int arityOptional = READ_BYTE();
            int argCount = vm->stackTop - frame->slots - arityOptional - 1;

            // Temp array while we shuffle the stack.
            // Can not have more than 255 args to a function, so
            // we can define this with a constant limit
            Value values[255];
            int index;

            for (index = 0; index < arityOptional + argCount; index++) {
                values[index] = pop(vm);
            }

            --index;

            for (int i = 0; i < argCount; i++) {
                push(vm, values[index - i]);
            }

            // Calculate how many "default" values are required
            int remaining = arity + arityOptional - argCount;

            // Push any "default" values back onto the stack
            for (int i = remaining; i > 0; i--) {
                push(vm, values[i - 1]);
            }

            DISPATCH();
        }

        CASE_CODE(GET_UPVALUE): {
            uint8_t slot = READ_BYTE();
            push(vm, *frame->closure->upvalues[slot]->value);
            DISPATCH();
        }

        CASE_CODE(SET_UPVALUE): {
            uint8_t slot = READ_BYTE();
            *frame->closure->upvalues[slot]->value = peek(vm, 0);
            DISPATCH();
        }

        CASE_CODE(GET_PROPERTY): {
            Value receiver = peek(vm, 0);

            if (!IS_OBJ(receiver)) {
                RUNTIME_ERROR_TYPE("'%s' type has no properties", 0);
            }

            switch (getObjType(receiver)) {
                case OBJ_INSTANCE: {
                    ObjInstance *instance = AS_INSTANCE(receiver);
                    ObjString *name = READ_STRING();
                    Value value;
                    if (tableGet(&instance->publicFields, name, &value)) {
                        pop(vm); // Instance.
                        push(vm, value);
                        DISPATCH();
                    }

                    if (bindMethod(vm, instance->klass, name)) {
                        DISPATCH();
                    }

                    // Check class for properties
                    ObjClass *klass = instance->klass;

                    while (klass != NULL) {
                        if (tableGet(&klass->publicProperties, name, &value)) {
                            pop(vm); // Instance.
                            push(vm, value);
                            DISPATCH();
                        }

                        klass = klass->superclass;
                    }

                    if (tableGet(&instance->privateFields, name, &value)) {
                        RUNTIME_ERROR("Cannot access private property '%s' on '%s' instance.", name->chars, instance->klass->name->chars);
                    }

                    RUNTIME_ERROR("'%s' instance has no property: '%s'.", instance->klass->name->chars, name->chars);
                }

                case OBJ_MODULE: {
                    ObjModule *module = AS_MODULE(receiver);
                    ObjString *name = READ_STRING();
                    Value value;
                    if (tableGet(&module->values, name, &value)) {
                        pop(vm); // Module.
                        push(vm, value);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("'%s' module has no property: '%s'.", module->name->chars, name->chars);
                }

                case OBJ_CLASS: {
                    ObjClass *klass = AS_CLASS(receiver);
                    // Used to keep a reference to the class for the runtime error below
                    ObjClass *klassStore = klass;
                    ObjString *name = READ_STRING();

                    Value value;
                    while (klass != NULL) {
                        if (tableGet(&klass->publicProperties, name, &value)) {
                            pop(vm); // Class.
                            push(vm, value);
                            DISPATCH();
                        }

                        klass = klass->superclass;
                    }

                    RUNTIME_ERROR("'%s' class has no property: '%s'.", klassStore->name->chars, name->chars);
                }

                case OBJ_ENUM: {
                    ObjEnum *enumObj = AS_ENUM(receiver);
                    ObjString *name = READ_STRING();
                    Value value;

                    if (tableGet(&enumObj->values, name, &value)) {
                        pop(vm); // Enum.
                        push(vm, value);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("'%s' enum has no property: '%s'.", enumObj->name->chars, name->chars);
                }

                default: {
                    RUNTIME_ERROR_TYPE("'%s' type has no properties", 0);
                }
            }
        }

        CASE_CODE(GET_PRIVATE_PROPERTY): {
            if (IS_INSTANCE(peek(vm, 0))) {
                ObjInstance *instance = AS_INSTANCE(peek(vm, 0));
                ObjString *name = READ_STRING();
                Value value;
                if (tableGet(&instance->privateFields, name, &value)) {
                    pop(vm); // Instance.
                    push(vm, value);
                    DISPATCH();
                }

                if (tableGet(&instance->publicFields, name, &value)) {
                    pop(vm); // Instance.
                    push(vm, value);
                    DISPATCH();
                }

                if (bindMethod(vm, instance->klass, name)) {
                    DISPATCH();
                }

                // Check class for properties
                ObjClass *klass = instance->klass;

                while (klass != NULL) {
                    if (tableGet(&klass->publicProperties, name, &value)) {
                        pop(vm); // Instance.
                        push(vm, value);
                        DISPATCH();
                    }

                    klass = klass->superclass;
                }

                RUNTIME_ERROR("'%s' instance has no property: '%s'.", instance->klass->name->chars, name->chars);
            } else if (IS_CLASS(peek(vm, 0))) {
                ObjClass *klass = AS_CLASS(peek(vm, 0));
                // Used to keep a reference to the class for the runtime error below
                ObjClass *klassStore = klass;
                ObjString *name = READ_STRING();

                Value value;
                while (klass != NULL) {
                    if (tableGet(&klass->publicProperties, name, &value)) {
                        pop(vm); // Class.
                        push(vm, value);
                        DISPATCH();
                    }

                    klass = klass->superclass;
                }

                RUNTIME_ERROR("'%s' class has no property: '%s'.", klassStore->name->chars, name->chars);
            }

            RUNTIME_ERROR_TYPE("'%s' type has no properties", 0);
        }

        CASE_CODE(GET_PROPERTY_NO_POP): {
            if (!IS_INSTANCE(peek(vm, 0))) {
                RUNTIME_ERROR("Only instances have properties.");
            }

            ObjInstance *instance = AS_INSTANCE(peek(vm, 0));
            ObjString *name = READ_STRING();
            Value value;
            if (tableGet(&instance->publicFields, name, &value)) {
                push(vm, value);
                DISPATCH();
            }

            if (bindMethod(vm, instance->klass, name)) {
                DISPATCH();
            }

            // Check class for properties
            ObjClass *klass = instance->klass;

            while (klass != NULL) {
                if (tableGet(&klass->publicProperties, name, &value)) {
                    push(vm, value);
                    DISPATCH();
                }

                klass = klass->superclass;
            }

            if (tableGet(&instance->privateFields, name, &value)) {
                RUNTIME_ERROR("Cannot access private property '%s' on '%s' instance.", name->chars, instance->klass->name->chars);
            }

            RUNTIME_ERROR("'%s' instance has no property: '%s'.", instance->klass->name->chars, name->chars);
        }

        CASE_CODE(GET_PRIVATE_PROPERTY_NO_POP): {
            if (!IS_INSTANCE(peek(vm, 0))) {
                RUNTIME_ERROR("Only instances have properties.");
            }

            ObjInstance *instance = AS_INSTANCE(peek(vm, 0));
            ObjString *name = READ_STRING();
            Value value;
            if (tableGet(&instance->privateFields, name, &value)) {
                push(vm, value);
                DISPATCH();
            }

            if (tableGet(&instance->publicFields, name, &value)) {
                push(vm, value);
                DISPATCH();
            }

            if (bindMethod(vm, instance->klass, name)) {
                DISPATCH();
            }

            // Check class for properties
            ObjClass *klass = instance->klass;

            while (klass != NULL) {
                if (tableGet(&klass->publicProperties, name, &value)) {
                    push(vm, value);
                    DISPATCH();
                }

                klass = klass->superclass;
            }

            RUNTIME_ERROR("'%s' instance has no property: '%s'.", instance->klass->name->chars, name->chars);
        }

        CASE_CODE(SET_PROPERTY): {
            if (IS_INSTANCE(peek(vm, 1))) {
                ObjInstance *instance = AS_INSTANCE(peek(vm, 1));
                tableSet(vm, &instance->publicFields, READ_STRING(), peek(vm, 0));
                pop(vm);
                pop(vm);
                push(vm, NIL_VAL);
                DISPATCH();
            } else if (IS_CLASS(peek(vm, 1))) {
                ObjClass *klass = AS_CLASS(peek(vm, 1));
                tableSet(vm, &klass->publicProperties, READ_STRING(), peek(vm, 0));
                pop(vm);
                pop(vm);
                push(vm, NIL_VAL);
                DISPATCH();
            }

            RUNTIME_ERROR_TYPE("Can not set property on type '%s'", 1);
        }

        CASE_CODE(SET_PRIVATE_PROPERTY): {
            if (IS_INSTANCE(peek(vm, 1))) {
                ObjInstance *instance = AS_INSTANCE(peek(vm, 1));
                tableSet(vm, &instance->privateFields, READ_STRING(), peek(vm, 0));
                pop(vm);
                pop(vm);
                push(vm, NIL_VAL);
                DISPATCH();
            }

            DISPATCH();
        }

        CASE_CODE(SET_CLASS_VAR): {
            // No type check required as this opcode is only ever emitted when parsing a class
            ObjClass *klass = AS_CLASS(peek(vm, 1));
            tableSet(vm, &klass->publicProperties, READ_STRING(), peek(vm, 0));
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(SET_INIT_PROPERTIES): {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
            int argCount = function->arity + function->arityOptional;
            ObjInstance *instance = AS_INSTANCE(peek(vm, function->arity + function->arityOptional));

            for (int i = 0; i < function->propertyCount; ++i) {
                ObjString *propertyName = AS_STRING(function->chunk.constants.values[function->propertyNames[i]]);
                tableSet(vm, &instance->publicFields, propertyName, peek(vm, argCount - function->propertyIndexes[i] - 1));
            }

            DISPATCH();
        }

        CASE_CODE(SET_PRIVATE_INIT_PROPERTIES): {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
            int argCount = function->arity + function->arityOptional;
            ObjInstance *instance = AS_INSTANCE(peek(vm, function->arity + function->arityOptional));

            for (int i = 0; i < function->privatePropertyCount; ++i) {
                ObjString *propertyName = AS_STRING(function->chunk.constants.values[function->privatePropertyNames[i]]);
                tableSet(vm, &instance->privateFields, propertyName, peek(vm, argCount - function->privatePropertyIndexes[i] - 1));
            }

            DISPATCH();
        }

        CASE_CODE(GET_SUPER): {
            ObjString *name = READ_STRING();
            ObjClass *superclass = AS_CLASS(pop(vm));

            if (!bindMethod(vm, superclass, name)) {
                RUNTIME_ERROR("Undefined property '%s'.", name->chars);
            }
            DISPATCH();
        }

        CASE_CODE(EQUAL): {
            Value b = pop(vm);
            Value a = pop(vm);
            push(vm, BOOL_VAL(valuesEqual(a, b)));
            DISPATCH();
        }

        CASE_CODE(GREATER):
            BINARY_OP(BOOL_VAL, >, double);
            DISPATCH();

        CASE_CODE(LESS):
            BINARY_OP(BOOL_VAL, <, double);
            DISPATCH();

        CASE_CODE(ADD): {
            if (IS_STRING(peek(vm, 0)) && IS_STRING(peek(vm, 1))) {
                concatenate(vm);
            } else if (IS_NUMBER(peek(vm, 0)) && IS_NUMBER(peek(vm, 1))) {
                double b = AS_NUMBER(pop(vm));
                double a = AS_NUMBER(pop(vm));
                push(vm, NUMBER_VAL(a + b));
            } else if (IS_LIST(peek(vm, 0)) && IS_LIST(peek(vm, 1))) {
                ObjList *listOne = AS_LIST(peek(vm, 1));
                ObjList *listTwo = AS_LIST(peek(vm, 0));

                ObjList *finalList = newList(vm);
                push(vm, OBJ_VAL(finalList));

                for (int i = 0; i < listOne->values.count; ++i) {
                    writeValueArray(vm, &finalList->values, listOne->values.values[i]);
                }

                for (int i = 0; i < listTwo->values.count; ++i) {
                    writeValueArray(vm, &finalList->values, listTwo->values.values[i]);
                }

                pop(vm);

                pop(vm);
                pop(vm);

                push(vm, OBJ_VAL(finalList));
            } else {
                UNSUPPORTED_OPERAND_TYPE_ERROR(+);
            }
            DISPATCH();
        }

        CASE_CODE(SUBTRACT): {
            BINARY_OP(NUMBER_VAL, -, double);
            DISPATCH();
        }

        CASE_CODE(MULTIPLY):
            BINARY_OP(NUMBER_VAL, *, double);
            DISPATCH();

        CASE_CODE(DIVIDE):
            BINARY_OP(NUMBER_VAL, /, double);
            DISPATCH();

        CASE_CODE(POW): {
            BINARY_OP_FUNCTION(NUMBER_VAL, **, powf, double);
            DISPATCH();
        }

        CASE_CODE(MOD): {
            BINARY_OP_FUNCTION(NUMBER_VAL, **, fmod, double);
            DISPATCH();
        }

        CASE_CODE(BITWISE_AND):
            BINARY_OP(NUMBER_VAL, &, int);
            DISPATCH();

        CASE_CODE(BITWISE_XOR):
            BINARY_OP(NUMBER_VAL, ^, int);
            DISPATCH();

        CASE_CODE(BITWISE_OR):
            BINARY_OP(NUMBER_VAL, |, int);
            DISPATCH();

        CASE_CODE(NOT):
            push(vm, BOOL_VAL(isFalsey(pop(vm))));
            DISPATCH();

        CASE_CODE(NEGATE):
            if (!IS_NUMBER(peek(vm, 0))) {
                RUNTIME_ERROR_TYPE("Unsupported operand type for unary -: '%s'", 0);
            }

            push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
            DISPATCH();

        CASE_CODE(JUMP): {
            uint16_t offset = READ_SHORT();
            ip += offset;
            DISPATCH();
        }

        CASE_CODE(JUMP_IF_FALSE): {
            uint16_t offset = READ_SHORT();
            if (isFalsey(peek(vm, 0))) ip += offset;
            DISPATCH();
        }

        CASE_CODE(JUMP_IF_NIL): {
            uint16_t offset = READ_SHORT();
            if (IS_NIL(peek(vm, 0))) ip += offset;
            DISPATCH();
        }

        CASE_CODE(LOOP): {
            uint16_t offset = READ_SHORT();
            ip -= offset;
            DISPATCH();
        }

        CASE_CODE(BREAK): {
            DISPATCH();
        }

        CASE_CODE(IMPORT): {
            ObjString *fileName = READ_STRING();
            Value moduleVal;

            // If we have imported this file already, skip.
            if (tableGet(&vm->modules, fileName, &moduleVal)) {
                vm->lastModule = AS_MODULE(moduleVal);
                push(vm, NIL_VAL);
                DISPATCH();
            }

            char path[PATH_MAX];
            if (!resolvePath(frame->closure->function->module->path->chars, fileName->chars, path)) {
                RUNTIME_ERROR("Could not open file \"%s\".", fileName->chars);
            }

            char *source = readFile(vm, path);

            if (source == NULL) {
                RUNTIME_ERROR("Could not open file \"%s\".", fileName->chars);
            }

            ObjString *pathObj = copyString(vm, path, strlen(path));
            push(vm, OBJ_VAL(pathObj));
            ObjModule *module = newModule(vm, pathObj);
            module->path = dirname(vm, path, strlen(path));
            vm->lastModule = module;
            pop(vm);

            push(vm, OBJ_VAL(module));
            ObjFunction *function = compile(vm, module, source);
            pop(vm);

            FREE_ARRAY(vm, char, source, strlen(source) + 1);

            if (function == NULL) return INTERPRET_COMPILE_ERROR;
            push(vm, OBJ_VAL(function));
            ObjClosure *closure = newClosure(vm, function);
            pop(vm);
            push(vm, OBJ_VAL(closure));

            frame->ip = ip;
            call(vm, closure, 0);
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;

            DISPATCH();
        }

        CASE_CODE(IMPORT_BUILTIN): {
            int index = READ_BYTE();
            ObjString *fileName = READ_STRING();
            Value moduleVal;

            // If we have imported this module already, skip.
            if (tableGet(&vm->modules, fileName, &moduleVal)) {
                push(vm, moduleVal);
                DISPATCH();
            }

            ObjModule *module = importBuiltinModule(vm, index);

            push(vm, OBJ_VAL(module));
            DISPATCH();
        }

        CASE_CODE(IMPORT_BUILTIN_VARIABLE): {
            int index = READ_BYTE();
            ObjString *fileName = READ_STRING();
            int varCount = READ_BYTE();

            Value moduleVal;
            ObjModule *module;

            if (tableGet(&vm->modules, fileName, &moduleVal)) {
                module = AS_MODULE(moduleVal);
            } else {
                module = importBuiltinModule(vm, index);
            }

            for (int i = 0; i < varCount; i++) {
                Value moduleVariable;
                ObjString *variable = READ_STRING();

                if (!tableGet(&module->values, variable, &moduleVariable)) {
                    RUNTIME_ERROR("%s can't be found in module %s", variable->chars, module->name->chars);
                }

                push(vm, moduleVariable);
            }

            DISPATCH();
        }

        CASE_CODE(IMPORT_VARIABLE): {
            push(vm, OBJ_VAL(vm->lastModule));
            DISPATCH();
        }

        CASE_CODE(IMPORT_FROM): {
            int varCount = READ_BYTE();

            for (int i = 0; i < varCount; i++) {
                Value moduleVariable;
                ObjString *variable = READ_STRING();

                if (!tableGet(&vm->lastModule->values, variable, &moduleVariable)) {
                    RUNTIME_ERROR("%s can't be found in module %s", variable->chars, vm->lastModule->name->chars);
                }

                push(vm, moduleVariable);
            }

            DISPATCH();
        }

        CASE_CODE(IMPORT_END): {
            vm->lastModule = frame->closure->function->module;
            DISPATCH();
        }

        CASE_CODE(NEW_LIST): {
            int count = READ_BYTE();
            ObjList *list = newList(vm);
            push(vm, OBJ_VAL(list));

            for (int i = count; i > 0; i--) {
                writeValueArray(vm, &list->values, peek(vm, i));
            }

            vm->stackTop -= count + 1;
            push(vm, OBJ_VAL(list));
            DISPATCH();
        }

        CASE_CODE(UNPACK_LIST): {
            int varCount = READ_BYTE();

            if (!IS_LIST(peek(vm, 0))) {
                RUNTIME_ERROR("Attempting to unpack a value which is not a list.");
            }

            ObjList *list = AS_LIST(pop(vm));

            if (varCount != list->values.count) {
                if (varCount < list->values.count) {
                    RUNTIME_ERROR("Too many values to unpack");
                } else {
                    RUNTIME_ERROR("Not enough values to unpack");
                }
            }

            for (int i = 0; i < list->values.count; ++i) {
                push(vm, list->values.values[i]);
            }

            DISPATCH();
        }

        CASE_CODE(NEW_DICT): {
            int count = READ_BYTE();
            ObjDict *dict = newDict(vm);
            push(vm, OBJ_VAL(dict));

            for (int i = count * 2; i > 0; i -= 2) {
                if (!isValidKey(peek(vm, i))) {
                    RUNTIME_ERROR("Dictionary key must be an immutable type.");
                }

                dictSet(vm, dict, peek(vm, i), peek(vm, i - 1));
            }

            vm->stackTop -= count * 2 + 1;
            push(vm, OBJ_VAL(dict));

            DISPATCH();
        }

        CASE_CODE(SUBSCRIPT): {
            Value indexValue = peek(vm, 0);
            Value subscriptValue = peek(vm, 1);

            if (!IS_OBJ(subscriptValue)) {
                RUNTIME_ERROR_TYPE("'%s' is not subscriptable", 1);
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        RUNTIME_ERROR("List index must be a number.");
                    }

                    ObjList *list = AS_LIST(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    // Allow negative indexes
                    if (index < 0)
                        index = list->values.count + index;

                    if (index >= 0 && index < list->values.count) {
                        pop(vm);
                        pop(vm);
                        push(vm, list->values.values[index]);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("List index out of bounds.");
                }

                case OBJ_STRING: {
                    ObjString *string = AS_STRING(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    // Allow negative indexes
                    if (index < 0)
                        index = string->length + index;

                    if (index >= 0 && index < string->length) {
                        pop(vm);
                        pop(vm);
                        push(vm, OBJ_VAL(copyString(vm, &string->chars[index], 1)));
                        DISPATCH();
                    }

                    RUNTIME_ERROR("String index out of bounds.");
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        RUNTIME_ERROR("Dictionary key must be an immutable type.");
                    }

                    Value v;
                    pop(vm);
                    pop(vm);
                    if (dictGet(dict, indexValue, &v)) {
                        push(vm, v);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("Key %s does not exist within dictionary.", valueToString(indexValue));
                }

                default: {
                    RUNTIME_ERROR_TYPE("'%s' is not subscriptable", 1);
                }
            }
        }

        CASE_CODE(SUBSCRIPT_ASSIGN): {
            Value assignValue = peek(vm, 0);
            Value indexValue = peek(vm, 1);
            Value subscriptValue = peek(vm, 2);

            if (!IS_OBJ(subscriptValue)) {
                RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        RUNTIME_ERROR("List index must be a number.");
                    }

                    ObjList *list = AS_LIST(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    if (index < 0)
                        index = list->values.count + index;

                    if (index >= 0 && index < list->values.count) {
                        list->values.values[index] = assignValue;
                        pop(vm);
                        pop(vm);
                        pop(vm);
                        push(vm, NIL_VAL);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("List index out of bounds.");
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        RUNTIME_ERROR("Dictionary key must be an immutable type.");
                    }

                    dictSet(vm, dict, indexValue, assignValue);
                    pop(vm);
                    pop(vm);
                    pop(vm);
                    push(vm, NIL_VAL);
                    DISPATCH();
                }

                default: {
                    RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
                }
            }
        }

        CASE_CODE(SUBSCRIPT_PUSH): {
            Value value = peek(vm, 0);
            Value indexValue = peek(vm, 1);
            Value subscriptValue = peek(vm, 2);

            if (!IS_OBJ(subscriptValue)) {
                RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        RUNTIME_ERROR("List index must be a number.");
                    }

                    ObjList *list = AS_LIST(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    // Allow negative indexes
                    if (index < 0)
                        index = list->values.count + index;

                    if (index >= 0 && index < list->values.count) {
                        vm->stackTop[-1] = list->values.values[index];
                        push(vm, value);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("List index out of bounds.");
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        RUNTIME_ERROR("Dictionary key must be an immutable type.");
                    }

                    Value dictValue;
                    if (!dictGet(dict, indexValue, &dictValue)) {
                        RUNTIME_ERROR("Key %s does not exist within dictionary.", valueToString(indexValue));
                    }

                    vm->stackTop[-1] = dictValue;
                    push(vm, value);

                    DISPATCH();
                }

                default: {
                    RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
                }
            }
            DISPATCH();
        }

        CASE_CODE(SLICE): {
            Value sliceEndIndex = peek(vm, 0);
            Value sliceStartIndex = peek(vm, 1);
            Value objectValue = peek(vm, 2);

            if (!IS_OBJ(objectValue)) {
                RUNTIME_ERROR("Can only slice on lists and strings.");
            }

            if ((!IS_NUMBER(sliceStartIndex) && !IS_EMPTY(sliceStartIndex)) || (!IS_NUMBER(sliceEndIndex) && !IS_EMPTY(sliceEndIndex))) {
                RUNTIME_ERROR("Slice index must be a number.");
            }

            int indexStart;
            int indexEnd;
            Value returnVal;

            if (IS_EMPTY(sliceStartIndex)) {
                indexStart = 0;
            } else {
                indexStart = AS_NUMBER(sliceStartIndex);

                if (indexStart < 0) {
                    indexStart = 0;
                }
            }

            switch (getObjType(objectValue)) {
                case OBJ_LIST: {
                    ObjList *createdList = newList(vm);
                    push(vm, OBJ_VAL(createdList));
                    ObjList *list = AS_LIST(objectValue);

                    if (IS_EMPTY(sliceEndIndex)) {
                        indexEnd = list->values.count;
                    } else {
                        indexEnd = AS_NUMBER(sliceEndIndex);

                        if (indexEnd > list->values.count) {
                            indexEnd = list->values.count;
                        } else if (indexEnd < 0) {
                            indexEnd = list->values.count + indexEnd;
                        }
                    }

                    for (int i = indexStart; i < indexEnd; i++) {
                        writeValueArray(vm, &createdList->values, list->values.values[i]);
                    }

                    pop(vm);
                    returnVal = OBJ_VAL(createdList);

                    break;
                }

                case OBJ_STRING: {
                    ObjString *string = AS_STRING(objectValue);

                    if (IS_EMPTY(sliceEndIndex)) {
                        indexEnd = string->length;
                    } else {
                        indexEnd = AS_NUMBER(sliceEndIndex);

                        if (indexEnd > string->length) {
                            indexEnd = string->length;
                        }  else if (indexEnd < 0) {
                            indexEnd = string->length + indexEnd;
                        }
                    }

                    // Ensure the start index is below the end index
                    if (indexStart > indexEnd) {
                        returnVal = OBJ_VAL(copyString(vm, "", 0));
                    } else {
                        returnVal = OBJ_VAL(copyString(vm, string->chars + indexStart, indexEnd - indexStart));
                    }
                    break;
                }

                default: {
                    RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
                }
            }

            pop(vm);
            pop(vm);
            pop(vm);

            push(vm, returnVal);
            DISPATCH();
        }

        CASE_CODE(CALL): {
            int argCount = READ_BYTE();
            frame->ip = ip;
            if (!callValue(vm, peek(vm, argCount), argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(INVOKE): {
            int argCount = READ_BYTE();
            ObjString *method = READ_STRING();
            frame->ip = ip;
            if (!invoke(vm, method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(INVOKE_INTERNAL): {
            int argCount = READ_BYTE();
            ObjString *method = READ_STRING();
            frame->ip = ip;
            if (!invokeInternal(vm, method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(SUPER): {
            int argCount = READ_BYTE();
            ObjString *method = READ_STRING();
            frame->ip = ip;
            ObjClass *superclass = AS_CLASS(pop(vm));
            if (!invokeFromClass(vm, superclass, method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(CLOSURE): {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());

            // Create the closure and push it on the stack before creating
            // upvalues so that it doesn't get collected.
            ObjClosure *closure = newClosure(vm, function);
            push(vm, OBJ_VAL(closure));

            // Capture upvalues.
            for (int i = 0; i < closure->upvalueCount; i++) {
                uint8_t isLocal = READ_BYTE();
                uint8_t index = READ_BYTE();
                if (isLocal) {
                    // Make an new upvalue to close over the parent's local
                    // variable.
                    closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
                } else {
                    // Use the same upvalue as the current call frame.
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }

            DISPATCH();
        }

        CASE_CODE(CLOSE_UPVALUE): {
            closeUpvalues(vm, vm->stackTop - 1);
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(RETURN): {
            Value result = pop(vm);

            // Close any upvalues still in scope.
            closeUpvalues(vm, frame->slots);

            vm->frameCount--;

            if (vm->frameCount == 0) {
                pop(vm);
                return INTERPRET_OK;
            }

            vm->stackTop = frame->slots;
            push(vm, result);

            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(CLASS): {
            ClassType type = READ_BYTE();
            createClass(vm, READ_STRING(), NULL, type);
            DISPATCH();
        }

        CASE_CODE(SUBCLASS): {
            ClassType type = READ_BYTE();

            Value superclass = peek(vm, 0);
            if (!IS_CLASS(superclass)) {
                RUNTIME_ERROR("Superclass must be a class.");
            }

            if (IS_TRAIT(superclass)) {
                RUNTIME_ERROR("Superclass can not be a trait.");
            }

            createClass(vm, READ_STRING(), AS_CLASS(superclass), type);
            DISPATCH();
        }

        CASE_CODE(END_CLASS): {
            ObjClass *klass = AS_CLASS(peek(vm, 0));

            // If super class is abstract, ensure we have defined all abstract methods
            for (int i = 0; i < klass->abstractMethods.capacityMask + 1; i++) {
                if (klass->abstractMethods.entries[i].key == NULL) {
                    continue;
                }

                Value _;
                if (!tableGet(&klass->publicMethods, klass->abstractMethods.entries[i].key, &_)) {
                    RUNTIME_ERROR("Class %s does not implement abstract method %s", klass->name->chars, klass->abstractMethods.entries[i].key->chars);
                }
            }
            DISPATCH();
        }

        CASE_CODE(METHOD):
            defineMethod(vm, READ_STRING());
            DISPATCH();

        CASE_CODE(ENUM): {
            ObjEnum *enumObj = newEnum(vm, READ_STRING());
            push(vm, OBJ_VAL(enumObj));
            DISPATCH();
        }

        CASE_CODE(SET_ENUM_VALUE): {
            Value value = peek(vm, 0);
            ObjEnum *enumObj = AS_ENUM(peek(vm, 1));

            tableSet(vm, &enumObj->values, READ_STRING(), value);
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(USE): {
            Value trait = peek(vm, 0);
            if (!IS_TRAIT(trait)) {
                RUNTIME_ERROR("Can only 'use' with a trait");
            }

            ObjClass *klass = AS_CLASS(peek(vm, 1));

            tableAddAll(vm, &AS_CLASS(trait)->publicMethods, &klass->publicMethods);
            pop(vm); // pop the trait

            DISPATCH();
        }

        CASE_CODE(OPEN_FILE): {
            Value openType = peek(vm, 0);
            Value fileName = peek(vm, 1);

            if (!IS_STRING(openType)) {
                RUNTIME_ERROR("File open type must be a string");
            }

            if (!IS_STRING(fileName)) {
                RUNTIME_ERROR("Filename must be a string");
            }

            ObjString *openTypeString = AS_STRING(openType);
            ObjString *fileNameString = AS_STRING(fileName);

            ObjFile *file = newFile(vm);
            file->file = fopen(fileNameString->chars, openTypeString->chars);
            file->path = fileNameString->chars;
            file->openType = openTypeString->chars;

            if (file->file == NULL) {
                RUNTIME_ERROR("Unable to open file '%s'", file->path);
            }

            pop(vm);
            pop(vm);
            push(vm, OBJ_VAL(file));
            DISPATCH();
        }

        CASE_CODE(CLOSE_FILE): {
            uint8_t slot = READ_BYTE();
            Value file = frame->slots[slot];
            ObjFile *fileObject = AS_FILE(file);
            fclose(fileObject->file);

            DISPATCH();
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
#undef BINARY_OP_FUNCTION
#undef STORE_FRAME
#undef RUNTIME_ERROR

    return INTERPRET_RUNTIME_ERROR;
}

DictuInterpretResult dictuInterpret(DictuVM *vm, char *moduleName, char *source) {
    ObjString *name = copyString(vm, moduleName, strlen(moduleName));
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    pop(vm);

    push(vm, OBJ_VAL(module));
    module->path = getDirectory(vm, moduleName);
    pop(vm);

    ObjFunction *function = compile(vm, module, source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    push(vm, OBJ_VAL(function));
    ObjClosure *closure = newClosure(vm, function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    callValue(vm, OBJ_VAL(closure), 0);
    DictuInterpretResult result = run(vm);

    return result;
}

uint32_t bindRef(DictuVM *vm, Value value) {
    if (IS_OBJ(value)) {
        Obj *obj = AS_OBJ(value);
        Obj *objPrev = obj->prev;
        Obj *objNext = obj->next;

        // Remove object from the object list.
        if (objPrev) {
            vm->objects = objPrev;
            objPrev->next = objNext;
            obj->next = NULL;
            obj->prev = NULL;

            if (objNext) {
                objNext->prev = objPrev;
            }
        } else {
            vm->objects = objNext;
            obj->next = NULL;

            if (objNext) {
                objNext->prev = NULL;
            }
        }
    }

    return registryInsert(vm, &vm->nativeRegistry, value);
}

void pushRef(DictuVM *vm, uint32_t ref) {
    push(vm, registryLookup(&vm->nativeRegistry, ref));
}

void unbindRef(DictuVM *vm, uint32_t ref) {
    Value value = registryRemove(&vm->nativeRegistry, ref);

    if (IS_OBJ(value)) {
        Obj *obj = AS_OBJ(value);
        obj->next = vm->objects;
        vm->objects = obj;
    }
}
