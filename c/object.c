#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType, true)

#define ALLOCATE_OBJ_NO_GC(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType, false)

static Obj *allocateObject(size_t size, ObjType type, bool garbageCollect) {
    Obj *object;

    if (garbageCollect) {
        object = (Obj *) reallocate(NULL, 0, size);
    } else {
        object = (Obj *) realloc(NULL, size);
    }

    object->type = type;
    object->isDark = false;
    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_TRACE_GC
    printf("%p allocate %ld for %d\n", (void *)object, size, type);
#endif

    return object;
}

ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method) {
    ObjBoundMethod *bound = ALLOCATE_OBJ(ObjBoundMethod,
                                         OBJ_BOUND_METHOD);

    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass *newClass(ObjString *name, ObjClass *superclass) {
    ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    klass->superclass = superclass;
    initTable(&klass->methods);
    return klass;
}

ObjClosure *newClosure(ObjFunction *function) {
    ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjFunction *newFunction(bool isStatic) {
    ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);

    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    function->staticMethod = isStatic;
    initChunk(&function->chunk);
    return function;
}

ObjInstance *newInstance(ObjClass *klass) {
    ObjInstance *instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

ObjNative *newNative(NativeFn function) {
    ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}


ObjNativeVoid *newNativeVoid(NativeFnVoid function) {
    ObjNativeVoid *native = ALLOCATE_OBJ(ObjNativeVoid, OBJ_NATIVE_VOID);
    native->function = function;
    return native;
}

static ObjString *allocateString(char *chars, int length,
                                 uint32_t hash) {
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    pop();
    return string;
}

ObjList *initList(bool garbageCollect) {
    ObjList *list;
    if (garbageCollect) {
        list = ALLOCATE_OBJ(ObjList, OBJ_LIST);
    } else {
        list = ALLOCATE_OBJ_NO_GC(ObjList, OBJ_LIST);
    }
    initValueArray(&list->values);
    return list;
}

ObjDict *initDict(bool garbageCollect) {
    ObjDict *dict;
    if (garbageCollect) {
        dict = ALLOCATE_OBJ(ObjDict, OBJ_DICT);
    } else {
        dict = ALLOCATE_OBJ_NO_GC(ObjDict, OBJ_DICT);
    }

    initDictValues(dict, 8);
    return dict;
}

ObjFile *initFile() {
    ObjFile *file = ALLOCATE_OBJ(ObjFile, OBJ_FILE);
    return file;
}

static uint32_t hashString(const char *key, int length) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= 16777619;
    }

    return hash;
}

ObjString *takeString(char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length,
                                          hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length);
        return interned;
    }

    return allocateString(chars, length, hash);
}

char *removeBackslash(char *string, char c) {
    int write = 0, read = 0;
    while (string[read]) {
        if (string[read] != c) {
            string[write++] = string[read];
        }

        read++;
    }
    string[write] = '\0';

    return string;
}

ObjString *copyString(const char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length,
                                          hash);
    if (interned != NULL) return interned;

    //removeBackslash(chars, '\\');

    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, hash);
}

ObjUpvalue *newUpvalue(Value *slot) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->value = slot;
    upvalue->next = NULL;

    return upvalue;
}

char *objectToString(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_CLASS: {
            ObjClass *klass = AS_CLASS(value);
            char *classString = malloc(sizeof(char) * (klass->name->length + 8));
            snprintf(classString, klass->name->length + 7, "<cls %s>", klass->name->chars);
            return classString;
        }

        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *method = AS_BOUND_METHOD(value);
            char *methodString;

            if (method->method->function->name != NULL) {
                methodString = malloc(sizeof(char) * (method->method->function->name->length + 17));
                char *methodType = method->method->function->staticMethod ? "<static method %s>" : "<bound method %s>";
                snprintf(methodString, method->method->function->name->length + 17, methodType, method->method->function->name->chars);
            } else {
                methodString = malloc(sizeof(char) * 16);
                char *methodType = method->method->function->staticMethod ? "<static method>" : "<bound method>";
                snprintf(methodString, 16, "%s", methodType);
            }

            return methodString;
        }

        case OBJ_CLOSURE: {
            ObjClosure *closure = AS_CLOSURE(value);
            char *closureString;

            if (closure->function->name != NULL) {
                closureString = malloc(sizeof(char) * (closure->function->name->length + 6));
                snprintf(closureString, closure->function->name->length + 6, "<fn %s>", closure->function->name->chars);
            } else {
                closureString = malloc(sizeof(char) * 9);
                snprintf(closureString, 9, "%s", "<script>");
            }

            return closureString;
        }

        case OBJ_FUNCTION: {
            ObjFunction *function = AS_FUNCTION(value);
            char *functionString;

            if (function->name != NULL) {
                functionString = malloc(sizeof(char) * (function->name->length + 6));
                snprintf(functionString, function->name->length + 6, "<fn %s>", function->name->chars);
            } else {
                functionString = malloc(sizeof(char) * 5);
                snprintf(functionString, 5, "%s", "<fn>");
            }

            return functionString;
        }

        case OBJ_INSTANCE: {
            ObjInstance *instance = AS_INSTANCE(value);
            char *instanceString = malloc(sizeof(char) * (instance->klass->name->length + 12));
            snprintf(instanceString, instance->klass->name->length + 12, "<%s instance>", instance->klass->name->chars);
            return instanceString;
        }

        case OBJ_NATIVE_VOID:
        case OBJ_NATIVE: {
            char *nativeString = malloc(sizeof(char) * 12);
            snprintf(nativeString, 12, "%s", "<native fn>");
            return nativeString;
        }

        case OBJ_STRING: {
            ObjString *stringObj = AS_STRING(value);
            char *string = malloc(sizeof(char) * stringObj->length + 3);
            snprintf(string, stringObj->length + 3, "'%s'", stringObj->chars);
            return string;
        }

        case OBJ_FILE: {
            ObjFile *file = AS_FILE(value);
            char *fileString = malloc(sizeof(char) * (strlen(file->path) + 8));
            snprintf(fileString, strlen(file->path) + 8, "<file %s>", file->path);
            return fileString;
        }

        case OBJ_LIST: {
            int size = 50;
            ObjList *list = AS_LIST(value);
            char *listString = calloc(size, sizeof(char));
            snprintf(listString, 2, "%s", "[");

            for (int i = 0; i < list->values.count; ++i) {
                char *element = valueToString(list->values.values[i]);

                int elementSize = strlen(element);
                int listStringSize = strlen(listString);

                if (elementSize > (size - listStringSize - 1)) {
                    if (elementSize > size * 2) {
                        size += elementSize * 2;
                    } else {
                        size *= 2;
                    }

                    char *newB = realloc(listString, sizeof(char) * size);

                    if (newB == NULL) {
                        printf("Unable to allocate memory\n");
                        exit(71);
                    }

                    listString = newB;
                }

                strncat(listString, element, size - listStringSize - 1);

                free(element);

                if (i != list->values.count - 1)
                    strncat(listString, ", ", size - listStringSize - 1);
            }

            strncat(listString, "]", size - strlen(listString) - 1);
            return listString;
        }

        case OBJ_DICT: {
            int count = 0;
            int size = 50;
            ObjDict *dict = AS_DICT(value);
            char *dictString = malloc(sizeof(char) * size);
            snprintf(dictString, 2, "%s", "{");

            for (int i = 0; i < dict->capacity; ++i) {
                dictItem *item = dict->items[i];
                if (!item || item->deleted)
                    continue;

                count++;


                int keySize = strlen(item->key);
                int dictStringSize = strlen(dictString);

                if (keySize > (size - dictStringSize - 1)) {
                    if (keySize > size * 2) {
                        size += keySize * 2;
                    } else {
                        size *= 2;
                    }

                    char *newB = realloc(dictString, sizeof(char) * size);

                    if (newB == NULL) {
                        printf("Unable to allocate memory\n");
                        exit(71);
                    }

                    dictString = newB;
                }

                char *dictKeyString = malloc(sizeof(char) * (strlen(item->key) + 5));
                snprintf(dictKeyString, (strlen(item->key) + 5), "\"%s\": ", item->key);

                strncat(dictString, dictKeyString, size - dictStringSize - 1);
                free(dictKeyString);

                char *element = valueToString(item->item);
                int elementSize = strlen(element);

                if (elementSize > (size - dictStringSize - 1)) {
                    if (elementSize > size * 2) {
                        size += elementSize * 2;
                    } else {
                        size *= 2;
                    }

                    char *newB = realloc(dictString, sizeof(char) * size);

                    if (newB == NULL) {
                        printf("Unable to allocate memory\n");
                        exit(71);
                    }

                    dictString = newB;
                }

                strncat(dictString, element, size - dictStringSize - 1);

                free(element);

                if (count != dict->count)
                    strncat(dictString, ", ", size - dictStringSize - 1);
            }

            strncat(dictString, "}", size - strlen(dictString) - 1);
            return dictString;
        }

        case OBJ_UPVALUE: {
            char *nativeString = malloc(sizeof(char) * 8);
            snprintf(nativeString, 8, "%s", "upvalue");
            return nativeString;
        }
    }

    char *unknown = malloc(sizeof(char) * 9);
    snprintf(unknown, 8, "%s", "unknown");
    return unknown;
}
