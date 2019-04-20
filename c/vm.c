#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"
#include "util.h"
#include "collections.h"
#include "strings.h"
#include "files.h"
#include "natives.h"

VM vm; // [one]

void defineAllNatives();

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.currentFrameCount = 0;
    vm.openUpvalues = NULL;
}

void runtimeError(const char *format, ...) {
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];

        ObjFunction *function = frame->closure->function;

        // -1 because the IP is sitting on the next instruction to be
        // executed.
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);

        if (function->name == NULL) {
            fprintf(stderr, "%s: ", vm.currentScriptName);
            i = -1;
        } else {
            fprintf(stderr, "%s(): ", function->name->chars);
        }

        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        fputs("\n", stderr);
        va_end(args);
    }

    resetStack();
}

void initVM(bool repl, const char *scriptName) {
    resetStack();
    vm.objects = NULL;
    vm.listObjects = NULL;
    vm.repl = repl;
    vm.scriptName = scriptName;
    vm.currentScriptName = scriptName;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    initTable(&vm.globals);
    initTable(&vm.strings);
    vm.initString = copyString("init", 4);
    vm.replVar = copyString("_", 1);
    defineAllNatives();
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    vm.initString = NULL;
    vm.replVar = NULL;
    freeObjects();
    freeLists();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
    vm.stackCount++;
}

Value pop() {
    vm.stackTop--;
    vm.stackCount--;
    return *vm.stackTop;
}

Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure *closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Function '%s' expected %d arguments but got %d.",
                     closure->function->name->chars,
                     closure->function->arity,
                     argCount
        );

        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;

    // +1 to include either the called function or the receiver.
    frame->slots = vm.stackTop - (argCount + 1);
    return true;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod *bound = AS_BOUND_METHOD(callee);

                // Replace the bound method with the receiver so it's in the
                // right slot when the method is called.
                vm.stackTop[-argCount - 1] = bound->receiver;
                return call(bound->method, argCount);
            }

            case OBJ_CLASS: {
                ObjClass *klass = AS_CLASS(callee);

                // Create the instance.
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));

                // Call the initializer, if there is one.
                Value initializer;
                if (tableGet(&klass->methods, vm.initString, &initializer)) {
                    return call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }

                return true;
            }

            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);

            case OBJ_NATIVE_VOID: {
                NativeFnVoid native = AS_NATIVE_VOID(callee);
                if (!native(argCount, vm.stackTop - argCount))
                    return false;

                vm.stackTop -= argCount + 1;
                vm.stackCount -= argCount + 1;
                push(NIL_VAL);
                return true;
            }

            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);

                if (IS_NIL(result))
                    return false;

                vm.stackTop -= argCount + 1;
                vm.stackCount -= argCount + 1;
                push(result);
                return true;
            }

            default:
                // Do nothing.
                break;
        }
    }

    runtimeError("Can only call functions and classes.");
    return false;
}

static bool invokeFromClass(ObjClass *klass, ObjString *name,
                            int argCount) {
    // Look for the method.
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString *name, int argCount) {
    Value receiver = peek(argCount);

    if (IS_CLASS(receiver)) {
        ObjClass *instance = AS_CLASS(receiver);
        Value method;
        if (!tableGet(&instance->methods, name, &method)) {
            runtimeError("Undefined property '%s'.", name->chars);
            return false;
        }

        if (!AS_CLOSURE(method)->function->staticMethod) {
            runtimeError("'%s' is not static. Only static methods can be invoked directly from a class.", name->chars);
            return false;
        }

        return callValue(method, argCount);
    } else if (IS_LIST(receiver)) {
        return listMethods(name->chars, argCount + 1);
    } else if (IS_DICT(receiver)) {
        return dictMethods(name->chars, argCount + 1);
    } else if (IS_STRING(receiver)) {
        return stringMethods(name->chars, argCount + 1);
    } else if (IS_FILE(receiver)) {
        return fileMethods(name->chars, argCount + 1);
    }

    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances have methods.");
        return false;
    }

    ObjInstance *instance = AS_INSTANCE(receiver);

    // First look for a field which may shadow a method.
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

static bool bindMethod(ObjClass *klass, ObjString *name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop(); // Instance.
    push(OBJ_VAL(bound));
    return true;
}

// Captures the local variable [local] into an [Upvalue]. If that local
// is already in an upvalue, the existing one is used. (This is
// important to ensure that multiple closures closing over the same
// variable actually see the same variable.) Otherwise, it creates a
// new open upvalue and adds it to the VM's list of upvalues.
static ObjUpvalue *captureUpvalue(Value *local) {
    // If there are no open upvalues at all, we must need a new one.
    if (vm.openUpvalues == NULL) {
        vm.openUpvalues = newUpvalue(local);
        return vm.openUpvalues;
    }

    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm.openUpvalues;

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
    ObjUpvalue *createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        // The new one is the first one in the list.
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value *last) {
    while (vm.openUpvalues != NULL &&
           vm.openUpvalues->value >= last) {
        ObjUpvalue *upvalue = vm.openUpvalues;

        // Move the value into the upvalue itself and point the upvalue to
        // it.
        upvalue->closed = *upvalue->value;
        upvalue->value = &upvalue->closed;

        // Pop it off the open upvalue list.
        vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod(ObjString *name) {
    Value method = peek(0);
    ObjClass *klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
}

static void createClass(ObjString *name, ObjClass *superclass) {
    ObjClass *klass = newClass(name, superclass);
    push(OBJ_VAL(klass));

    // Inherit methods.
    if (superclass != NULL) {
        tableAddAll(&superclass->methods, &klass->methods);
    }
}

bool isFalsey(Value value) {
    return IS_NIL(value) ||
           (IS_BOOL(value) && !AS_BOOL(value)) ||
           (IS_NUMBER(value) && AS_NUMBER(value) == 0) ||
           (IS_STRING(value) && AS_CSTRING(value)[0] == '\0');
}

static void concatenate() {
    ObjString *b = AS_STRING(peek(0));
    ObjString *a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString *result = takeString(chars, length);

    pop();
    pop();

    push(OBJ_VAL(result));
}

static void setReplVar(Value value) {
    tableSet(&vm.globals, vm.replVar, value);
}

static InterpretResult run() {

    CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
        (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
        (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

    for (;;) {

#ifdef DEBUG_TRACE_EXECUTION

        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
          printf("[ ");
          printValue(*slot);
          printf(" ]");
        }
        printf("\n");

        disassembleInstruction(&frame->closure->function->chunk,
            (int)(frame->ip - frame->closure->function->chunk.code));
#endif
        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }

            case OP_NIL:
                push(NIL_VAL);
                break;

            case OP_TRUE:
                push(BOOL_VAL(true));
                break;

            case OP_FALSE:
                push(BOOL_VAL(false));
                break;

            case OP_POP_REPL: {
                if (vm.repl) {
                    Value v = pop();
                    if (!IS_NIL(v)) {
                        setReplVar(v);
                        printValue(v);
                        printf("\n");
                    }
                } else {
                    pop();
                }
                break;
            }

            case OP_POP: {
                if (IS_FILE(peek(0))) {
                    ObjFile *file = AS_FILE(peek(0));
                    fclose(file->file);
                    collectGarbage();
                }

                pop();
                break;
            }

            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }

            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }

            case OP_GET_GLOBAL: {
                ObjString *name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }

            case OP_DEFINE_GLOBAL: {
                ObjString *name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }

            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->value);
                break;
            }

            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->value = peek(0);
                break;
            }

            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(0))) {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance *instance = AS_INSTANCE(peek(0));
                ObjString *name = READ_STRING();
                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop(); // Instance.
                    push(value);
                    break;
                }

                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                break;
            }

            case OP_GET_PROPERTY_NO_POP: {
                if (!IS_INSTANCE(peek(0))) {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance *instance = AS_INSTANCE(peek(0));
                ObjString *name = READ_STRING();
                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    push(value);
                    break;
                }

                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                break;
            }

            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(1))) {
                    runtimeError("Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance *instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));
                pop();
                pop();
                push(NIL_VAL);
                break;
            }

            case OP_GET_SUPER: {
                ObjString *name = READ_STRING();
                ObjClass *superclass = AS_CLASS(pop());
                if (!bindMethod(superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }

            case OP_GREATER:
                BINARY_OP(BOOL_VAL, >);
                break;

            case OP_LESS:
                BINARY_OP(BOOL_VAL, <);
                break;

            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else if (IS_LIST(peek(0)) && IS_LIST(peek(1))) {
                    Value listToAddValue = pop();
                    Value listValue = pop();

                    ObjList *list = AS_LIST(listValue);
                    ObjList *listToAdd = AS_LIST(listToAddValue);

                    for (int i = 0; i < listToAdd->values.count; ++i) {
                        writeValueArray(&list->values, listToAdd->values.values[i]);
                    }

                    push(NIL_VAL);
                } else {
                    runtimeError("Unsupported operand types.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_SUBTRACT:
                BINARY_OP(NUMBER_VAL, -);
                break;

            case OP_INCREMENT: {
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                push(NUMBER_VAL(AS_NUMBER(pop()) + 1));
                break;
            }

            case OP_DECREMENT: {
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                push(NUMBER_VAL(AS_NUMBER(pop()) - 1));
                break;
            }

            case OP_MULTIPLY:
                BINARY_OP(NUMBER_VAL, *);
                break;

            case OP_DIVIDE:
                BINARY_OP(NUMBER_VAL, /);
                break;

            case OP_MOD: {
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());

                push(NUMBER_VAL(fmod(a, b)));
                break;
            }

            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;

            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;

            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }

            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }

            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }

            case OP_BREAK: {

                break;
            }

            case OP_IMPORT: {
                ObjString *fileName = AS_STRING(pop());
                char *s = readFile(fileName->chars);
                vm.currentScriptName = fileName->chars;

                ObjFunction *function = compile(s);
                if (function == NULL) return INTERPRET_COMPILE_ERROR;
                push(OBJ_VAL(function));
                ObjClosure *closure = newClosure(function);
                pop();

                vm.currentFrameCount = vm.frameCount;

                frame = &vm.frames[vm.frameCount++];
                frame->ip = closure->function->chunk.code;
                frame->closure = closure;
                frame->slots = vm.stackTop - 1;
                free(s);
                break;
            }

            case OP_NEW_LIST: {
                ObjList *list = initList();
                push(OBJ_VAL(list));
                break;
            }

            case OP_ADD_LIST: {
                Value addValue = pop();
                Value listValue = pop();

                ObjList *list = AS_LIST(listValue);
                writeValueArray(&list->values, addValue);

                push(OBJ_VAL(list));
                break;
            }

            case OP_NEW_DICT: {
                ObjDict *dict = initDict();
                push(OBJ_VAL(dict));
                break;
            }

            case OP_ADD_DICT: {
                Value value = pop();
                Value key = pop();
                Value dictValue = pop();

                if (!IS_STRING(key)) {
                    runtimeError("Dictionary key must be a string.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjDict *dict = AS_DICT(dictValue);
                char *keyString = AS_CSTRING(key);

                insertDict(dict, keyString, value);

                push(OBJ_VAL(dict));
                break;
            }

            case OP_SUBSCRIPT: {
                Value indexValue = pop();
                Value listValue = pop();

                if (!IS_NUMBER(indexValue)) {
                    runtimeError("Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjList *list = AS_LIST(listValue);
                int index = AS_NUMBER(indexValue);

                if (index < 0)
                    index = list->values.count + index;

                if (index >= 0 && index < list->values.count) {
                    push(list->values.values[index]);
                    break;
                }

                runtimeError("Array index out of bounds.");
                return INTERPRET_RUNTIME_ERROR;
            }

            case OP_SUBSCRIPT_ASSIGN: {
                Value assignValue = pop();
                Value indexValue = pop();
                Value listValue = pop();

                if (!IS_NUMBER(indexValue)) {
                    runtimeError("Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjList *list = AS_LIST(listValue);
                int index = AS_NUMBER(indexValue);

                if (index < 0)
                    index = list->values.count + index;

                if (index >= 0 && index < list->values.count) {
                    list->values.values[index] = assignValue;
                    push(NIL_VAL);
                    break;
                }

                push(NIL_VAL);

                runtimeError("Array index out of bounds.");
                return INTERPRET_RUNTIME_ERROR;
            }

            case OP_SUBSCRIPT_DICT: {
                Value indexValue = pop();
                Value dictValue = pop();

                if (!IS_STRING(indexValue)) {
                    runtimeError("Dictionary key must be a string.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjDict *dict = AS_DICT(dictValue);
                char *key = AS_CSTRING(indexValue);

                push(searchDict(dict, key));

                break;
            }

            case OP_SUBSCRIPT_DICT_ASSIGN: {
                Value value = pop();
                Value key = pop();
                Value dictValue = pop();

                if (!IS_STRING(key)) {
                    runtimeError("Dictionary key must be a string.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjDict *dict = AS_DICT(dictValue);
                char *keyString = AS_CSTRING(key);

                insertDict(dict, keyString, value);

                push(NIL_VAL);
                break;
            }

            case OP_CALL_0:
            case OP_CALL_1:
            case OP_CALL_2:
            case OP_CALL_3:
            case OP_CALL_4:
            case OP_CALL_5:
            case OP_CALL_6:
            case OP_CALL_7:
            case OP_CALL_8:
            case OP_CALL_9:
            case OP_CALL_10:
            case OP_CALL_11:
            case OP_CALL_12:
            case OP_CALL_13:
            case OP_CALL_14:
            case OP_CALL_15:
            case OP_CALL_16:
            case OP_CALL_17:
            case OP_CALL_18:
            case OP_CALL_19:
            case OP_CALL_20:
            case OP_CALL_21:
            case OP_CALL_22:
            case OP_CALL_23:
            case OP_CALL_24:
            case OP_CALL_25:
            case OP_CALL_26:
            case OP_CALL_27:
            case OP_CALL_28:
            case OP_CALL_29:
            case OP_CALL_30:
            case OP_CALL_31: {
                int argCount = instruction - OP_CALL_0;
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            case OP_INVOKE_0:
            case OP_INVOKE_1:
            case OP_INVOKE_2:
            case OP_INVOKE_3:
            case OP_INVOKE_4:
            case OP_INVOKE_5:
            case OP_INVOKE_6:
            case OP_INVOKE_7:
            case OP_INVOKE_8:
            case OP_INVOKE_9:
            case OP_INVOKE_10:
            case OP_INVOKE_11:
            case OP_INVOKE_12:
            case OP_INVOKE_13:
            case OP_INVOKE_14:
            case OP_INVOKE_15:
            case OP_INVOKE_16:
            case OP_INVOKE_17:
            case OP_INVOKE_18:
            case OP_INVOKE_19:
            case OP_INVOKE_20:
            case OP_INVOKE_21:
            case OP_INVOKE_22:
            case OP_INVOKE_23:
            case OP_INVOKE_24:
            case OP_INVOKE_25:
            case OP_INVOKE_26:
            case OP_INVOKE_27:
            case OP_INVOKE_28:
            case OP_INVOKE_29:
            case OP_INVOKE_30:
            case OP_INVOKE_31: {
                ObjString *method = READ_STRING();
                int argCount = instruction - OP_INVOKE_0;
                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            case OP_SUPER_0:
            case OP_SUPER_1:
            case OP_SUPER_2:
            case OP_SUPER_3:
            case OP_SUPER_4:
            case OP_SUPER_5:
            case OP_SUPER_6:
            case OP_SUPER_7:
            case OP_SUPER_8:
            case OP_SUPER_9:
            case OP_SUPER_10:
            case OP_SUPER_11:
            case OP_SUPER_12:
            case OP_SUPER_13:
            case OP_SUPER_14:
            case OP_SUPER_15:
            case OP_SUPER_16:
            case OP_SUPER_17:
            case OP_SUPER_18:
            case OP_SUPER_19:
            case OP_SUPER_20:
            case OP_SUPER_21:
            case OP_SUPER_22:
            case OP_SUPER_23:
            case OP_SUPER_24:
            case OP_SUPER_25:
            case OP_SUPER_26:
            case OP_SUPER_27:
            case OP_SUPER_28:
            case OP_SUPER_29:
            case OP_SUPER_30:
            case OP_SUPER_31: {
                ObjString *method = READ_STRING();
                int argCount = instruction - OP_SUPER_0;
                ObjClass *superclass = AS_CLASS(pop());
                if (!invokeFromClass(superclass, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            case OP_CLOSURE: {
                ObjFunction *function = AS_FUNCTION(READ_CONSTANT());

                // Create the closure and push it on the stack before creating
                // upvalues so that it doesn't get collected.
                ObjClosure *closure = newClosure(function);
                push(OBJ_VAL(closure));

                // Capture upvalues.
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        // Make an new upvalue to close over the parent's local
                        // variable.
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        // Use the same upvalue as the current call frame.
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }

                break;
            }

            case OP_CLOSE_UPVALUE: {
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            }

            case OP_RETURN: {
                Value result = pop();

                // Close any upvalues still in scope.
                closeUpvalues(frame->slots);

                vm.frameCount--;

                if (vm.frameCount == vm.currentFrameCount) {
                    vm.currentScriptName = vm.scriptName;
                    vm.currentFrameCount = -1;
                }

                if (vm.frameCount == 0) return INTERPRET_OK;

                vm.stackTop = frame->slots;
                push(result);

                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            case OP_CLASS:
                createClass(READ_STRING(), NULL);
                break;

            case OP_SUBCLASS: {
                Value superclass = peek(0);
                if (!IS_CLASS(superclass)) {
                    runtimeError("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                createClass(READ_STRING(), AS_CLASS(superclass));
                break;
            }

            case OP_METHOD:
                defineMethod(READ_STRING());
                break;

            case OP_OPEN_FILE: {
                Value openType = pop();
                Value fileName = pop();

                if (!IS_STRING(openType)) {
                    runtimeError("File open type must be a string");
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (!IS_STRING(fileName)) {
                    runtimeError("Filename must be a string");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjString *openTypeString = AS_STRING(openType);
                ObjString *fileNameString = AS_STRING(fileName);

                ObjFile *file = initFile();
                file->file = fopen(fileNameString->chars, openTypeString->chars);
                file->path = fileNameString->chars;
                file->openType = openTypeString->chars;

                if (file->file == NULL) {
                    runtimeError("Unable to open file");
                    return INTERPRET_RUNTIME_ERROR;
                }

                push(OBJ_VAL(file));
                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP

}

InterpretResult interpret(const char *source) {
    ObjFunction *function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    push(OBJ_VAL(function));
    ObjClosure *closure = newClosure(function);
    pop();
    callValue(OBJ_VAL(closure), 0);
    InterpretResult result = run();
    return result;
}
