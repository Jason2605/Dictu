#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(vm, type, objectType) \
    (type*)allocateObject(vm, sizeof(type), objectType)

static Obj *allocateObject(DictuVM *vm, size_t size, ObjType type) {
    Obj *object;
    object = (Obj *) reallocate(vm, NULL, 0, size);
    object->type = type;
    object->isDark = false;
    object->prev = NULL;

    if (vm->objects) {
        object->next = vm->objects;
        vm->objects->prev = object;
    } else {
        object->next = NULL;
    }

    vm->objects = object;

#ifdef DEBUG_TRACE_GC
    printf("%p allocate %zd for %d\n", (void *)object, size, type);
#endif

    return object;
}

ObjModule *newModule(DictuVM *vm, ObjString *name) {
    Value moduleVal;
    if (tableGet(&vm->modules, name, &moduleVal)) {
        return AS_MODULE(moduleVal);
    }

    ObjModule *module = ALLOCATE_OBJ(vm, ObjModule, OBJ_MODULE);
    initTable(&module->values);
    module->name = name;
    module->path = NULL;

    push(vm, OBJ_VAL(module));
    ObjString *__file__ = copyString(vm, "__file__", 8);
    push(vm, OBJ_VAL(__file__));

    tableSet(vm, &module->values, __file__, OBJ_VAL(name));
    tableSet(vm, &vm->modules, name, OBJ_VAL(module));

    pop(vm);
    pop(vm);

    return module;
}

ObjBoundMethod *newBoundMethod(DictuVM *vm, Value receiver, ObjClosure *method) {
    ObjBoundMethod *bound = ALLOCATE_OBJ(vm, ObjBoundMethod,
                                         OBJ_BOUND_METHOD);

    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass *newClass(DictuVM *vm, ObjString *name, ObjClass *superclass, ClassType type) {
    ObjClass *klass = ALLOCATE_OBJ(vm, ObjClass, OBJ_CLASS);
    klass->name = name;
    klass->superclass = superclass;
    klass->type = type;
    initTable(&klass->abstractMethods);
    initTable(&klass->privateMethods);
    initTable(&klass->publicMethods);
    initTable(&klass->publicProperties);
    return klass;
}

ObjEnum *newEnum(DictuVM *vm, ObjString *name) {
    ObjEnum *enumObj = ALLOCATE_OBJ(vm, ObjEnum , OBJ_ENUM);
    enumObj->name = name;
    initTable(&enumObj->values);
    return enumObj;
}

ObjClosure *newClosure(DictuVM *vm, ObjFunction *function) {
    ObjUpvalue **upvalues = ALLOCATE(vm, ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure *closure = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjFunction *newFunction(DictuVM *vm, ObjModule *module, FunctionType type, AccessLevel level) {
    ObjFunction *function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->arityOptional = 0;
    function->upvalueCount = 0;
    function->propertyCount = 0;
    function->propertyIndexes = NULL;
    function->propertyNames = NULL;
    function->privatePropertyCount = 0;
    function->privatePropertyIndexes = NULL;
    function->propertyNames = NULL;
    function->name = NULL;
    function->type = type;
    function->accessLevel = level;
    function->module = module;
    initChunk(vm, &function->chunk);

    return function;
}

ObjInstance *newInstance(DictuVM *vm, ObjClass *klass) {
    ObjInstance *instance = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->publicFields);
    initTable(&instance->privateFields);
    return instance;
}

ObjNative *newNative(DictuVM *vm, NativeFn function) {
    ObjNative *native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

static ObjString *allocateString(DictuVM *vm, char *chars, int length,
                                 uint32_t hash) {
    ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    push(vm, OBJ_VAL(string));
    tableSet(vm, &vm->strings, string, NIL_VAL);
    pop(vm);
    return string;
}

ObjList *newList(DictuVM *vm) {
    ObjList *list = ALLOCATE_OBJ(vm, ObjList, OBJ_LIST);
    initValueArray(&list->values);
    return list;
}

ObjDict *newDict(DictuVM *vm) {
    ObjDict *dict = ALLOCATE_OBJ(vm, ObjDict, OBJ_DICT);
    dict->count = 0;
    dict->capacityMask = -1;
    dict->entries = NULL;
    return dict;
}

ObjSet *newSet(DictuVM *vm) {
    ObjSet *set = ALLOCATE_OBJ(vm, ObjSet, OBJ_SET);
    set->count = 0;
    set->capacityMask = -1;
    set->entries = NULL;
    return set;
}

ObjFile *newFile(DictuVM *vm) {
    return ALLOCATE_OBJ(vm, ObjFile, OBJ_FILE);
}

ObjAbstract *newAbstract(DictuVM *vm, AbstractFreeFn func) {
    ObjAbstract *abstract = ALLOCATE_OBJ(vm, ObjAbstract, OBJ_ABSTRACT);
    abstract->data = NULL;
    abstract->func = func;
    initTable(&abstract->values);

    return abstract;
}

ObjResult *newResult(DictuVM *vm, ResultStatus status, Value value) {
    ObjResult *result = ALLOCATE_OBJ(vm, ObjResult, OBJ_RESULT);
    result->status = status;
    result->value = value;

    return result;
}

Value newResultSuccess(DictuVM *vm, Value value) {
    push(vm, value);
    ObjResult *result = newResult(vm, SUCCESS, value);
    pop(vm);
    return OBJ_VAL(result);
}

Value newResultError(DictuVM *vm, char *errorMsg) {
    Value error = OBJ_VAL(copyString(vm, errorMsg, strlen(errorMsg)));
    push(vm, error);
    ObjResult *result = newResult(vm, ERR, error);
    pop(vm);
    return OBJ_VAL(result);
}

static uint32_t hashString(const char *key, int length) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= 16777619;
    }

    return hash;
}

ObjString *takeString(DictuVM *vm, char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm->strings, chars, length,
                                          hash);
    if (interned != NULL) {
        FREE_ARRAY(vm, char, chars, length + 1);
        return interned;
    }

    // Ensure terminating char is present
    chars[length] = '\0';
    return allocateString(vm, chars, length, hash);
}

ObjString *copyString(DictuVM *vm, const char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm->strings, chars, length,
                                          hash);
    if (interned != NULL) return interned;

    char *heapChars = ALLOCATE(vm, char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(vm, heapChars, length, hash);
}

ObjUpvalue *newUpvalue(DictuVM *vm, Value *slot) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->value = slot;
    upvalue->next = NULL;

    return upvalue;
}

char *listToString(Value value) {
    int size = 50;
    ObjList *list = AS_LIST(value);
    char *listString = malloc(sizeof(char) * size);
    memcpy(listString, "[", 1);
    int listStringLength = 1;

    for (int i = 0; i < list->values.count; ++i) {
        Value listValue = list->values.values[i];

        char *element;
        int elementSize;

        if (IS_STRING(listValue)) {
            ObjString *s = AS_STRING(listValue);
            element = s->chars;
            elementSize = s->length;
        } else {
            element = valueToString(listValue);
            elementSize = strlen(element);
        }

        if (elementSize > (size - listStringLength - 6)) {
            if (elementSize > size) {
                size = size + elementSize * 2 + 6;
            } else {
                size = size * 2 + 6;
            }

            char *newB = realloc(listString, sizeof(char) * size);

            if (newB == NULL) {
                printf("Unable to allocate memory\n");
                exit(71);
            }

            listString = newB;
        }

        if (IS_STRING(listValue)) {
            memcpy(listString + listStringLength, "\"", 1);
            memcpy(listString + listStringLength + 1, element, elementSize);
            memcpy(listString + listStringLength + 1 + elementSize, "\"", 1);
            listStringLength += elementSize + 2;
        } else {
            memcpy(listString + listStringLength, element, elementSize);
            listStringLength += elementSize;
            free(element);
        }

        if (i != list->values.count - 1) {
            memcpy(listString + listStringLength, ", ", 2);
            listStringLength += 2;
        }
    }

    memcpy(listString + listStringLength, "]", 1);
    listString[listStringLength + 1] = '\0';

    return listString;
}

char *dictToString(Value value) {
   int count = 0;
   int size = 50;
   ObjDict *dict = AS_DICT(value);
   char *dictString = malloc(sizeof(char) * size);
   memcpy(dictString, "{", 1);
   int dictStringLength = 1;

   for (int i = 0; i <= dict->capacityMask; ++i) {
       DictItem *item = &dict->entries[i];
       if (IS_EMPTY(item->key)) {
           continue;
       }

       count++;

       char *key;
       int keySize;

       if (IS_STRING(item->key)) {
           ObjString *s = AS_STRING(item->key);
           key = s->chars;
           keySize = s->length;
       } else {
           key = valueToString(item->key);
           keySize = strlen(key);
       }

       if (keySize > (size - dictStringLength - keySize - 4)) {
           if (keySize > size) {
               size += keySize * 2 + 4;
           } else {
               size *= 2 + 4;
           }

           char *newB = realloc(dictString, sizeof(char) * size);

           if (newB == NULL) {
               printf("Unable to allocate memory\n");
               exit(71);
           }

           dictString = newB;
       }

       if (IS_STRING(item->key)) {
           memcpy(dictString + dictStringLength, "\"", 1);
           memcpy(dictString + dictStringLength + 1, key, keySize);
           memcpy(dictString + dictStringLength + 1 + keySize, "\": ", 3);
           dictStringLength += 4 + keySize;
       } else {
           memcpy(dictString + dictStringLength, key, keySize);
           memcpy(dictString + dictStringLength + keySize, ": ", 2);
           dictStringLength += 2 + keySize;
           free(key);
       }

       char *element;
       int elementSize;

       if (IS_STRING(item->value)) {
           ObjString *s = AS_STRING(item->value);
           element = s->chars;
           elementSize = s->length;
       } else {
           element = valueToString(item->value);
           elementSize = strlen(element);
       }

       if (elementSize > (size - dictStringLength - elementSize - 6)) {
           if (elementSize > size) {
               size += elementSize * 2 + 6;
           } else {
               size = size * 2 + 6;
           }

           char *newB = realloc(dictString, sizeof(char) * size);

           if (newB == NULL) {
               printf("Unable to allocate memory\n");
               exit(71);
           }

           dictString = newB;
       }

       if (IS_STRING(item->value)) {
           memcpy(dictString + dictStringLength, "\"", 1);
           memcpy(dictString + dictStringLength + 1, element, elementSize);
           memcpy(dictString + dictStringLength + 1 + elementSize, "\"", 1);
           dictStringLength += 2 + elementSize;
       } else {
           memcpy(dictString + dictStringLength, element, elementSize);
           dictStringLength += elementSize;
           free(element);
       }

       if (count != dict->count) {
           memcpy(dictString + dictStringLength, ", ", 2);
           dictStringLength += 2;
       }
   }

   memcpy(dictString + dictStringLength, "}", 1);
   dictString[dictStringLength + 1] = '\0';

   return dictString;
}

char *setToString(Value value) {
    int count = 0;
    int size = 50;
    ObjSet *set = AS_SET(value);
    char *setString = malloc(sizeof(char) * size);
    memcpy(setString, "{", 1);
    int setStringLength = 1;

    for (int i = 0; i <= set->capacityMask; ++i) {
        SetItem *item = &set->entries[i];
        if (IS_EMPTY(item->value) || item->deleted)
            continue;

        count++;

        char *element;
        int elementSize;

        if (IS_STRING(item->value)) {
            ObjString *s = AS_STRING(item->value);
            element = s->chars;
            elementSize = s->length;
        } else {
            element = valueToString(item->value);
            elementSize = strlen(element);
        }

        if (elementSize > (size - setStringLength - 5)) {
            if (elementSize > size * 2) {
                size += elementSize * 2 + 5;
            } else {
                size = size * 2 + 5;
            }

            char *newB = realloc(setString, sizeof(char) * size);

            if (newB == NULL) {
                printf("Unable to allocate memory\n");
                exit(71);
            }

            setString = newB;
        }


        if (IS_STRING(item->value)) {
            memcpy(setString + setStringLength, "\"", 1);
            memcpy(setString + setStringLength + 1, element, elementSize);
            memcpy(setString + setStringLength + 1 + elementSize, "\"", 1);
            setStringLength += 2 + elementSize;
        } else {
            memcpy(setString + setStringLength, element, elementSize);
            setStringLength += elementSize;
            free(element);
        }

        if (count != set->count) {
            memcpy(setString + setStringLength, ", ", 2);
            setStringLength += 2;
        }
    }

    memcpy(setString + setStringLength, "}", 1);
    setString[setStringLength + 1] = '\0';

    return setString;
}

char *classToString(Value value) {
    ObjClass *klass = AS_CLASS(value);
    char *classString = malloc(sizeof(char) * (klass->name->length + 7));
    memcpy(classString, "<Cls ", 5);
    memcpy(classString + 5, klass->name->chars, klass->name->length);
    memcpy(classString + 5 + klass->name->length, ">", 1);
    classString[klass->name->length + 6] = '\0';
    return classString;
}

char *instanceToString(Value value) {
    ObjInstance *instance = AS_INSTANCE(value);
    char *instanceString = malloc(sizeof(char) * (instance->klass->name->length + 12));
    memcpy(instanceString, "<", 1);
    memcpy(instanceString + 1, instance->klass->name->chars, instance->klass->name->length);
    memcpy(instanceString + 1 + instance->klass->name->length, " instance>", 10);
    instanceString[instance->klass->name->length + 11] = '\0';
    return instanceString;
}

char *objectToString(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_MODULE: {
            ObjModule *module = AS_MODULE(value);
            char *moduleString = malloc(sizeof(char) * (module->name->length + 11));
            snprintf(moduleString, (module->name->length + 10), "<Module %s>", module->name->chars);
            return moduleString;
        }

        case OBJ_CLASS: {
            if (IS_TRAIT(value)) {
                ObjClass *trait = AS_CLASS(value);
                char *traitString = malloc(sizeof(char) * (trait->name->length + 10));
                snprintf(traitString, trait->name->length + 9, "<Trait %s>", trait->name->chars);
                return traitString;
            }

            return classToString(value);
        }

        case OBJ_ENUM: {
            ObjEnum *enumObj = AS_ENUM(value);
            char *enumString = malloc(sizeof(char) * (enumObj->name->length + 8));
            memcpy(enumString, "<Enum ", 6);
            memcpy(enumString + 6, enumObj->name->chars, enumObj->name->length);
            memcpy(enumString + 6 + enumObj->name->length, ">", 1);

            enumString[7 + enumObj->name->length] = '\0';

            return enumString;
        }

        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *method = AS_BOUND_METHOD(value);
            char *methodString;

            if (method->method->function->name != NULL) {
                switch (method->method->function->type) {
                    case TYPE_STATIC: {
                        methodString = malloc(sizeof(char) * (method->method->function->name->length + 17));
                        snprintf(methodString, method->method->function->name->length + 17, "<Static Method %s>", method->method->function->name->chars);
                        break;
                    }

                    default: {
                        methodString = malloc(sizeof(char) * (method->method->function->name->length + 16));
                        snprintf(methodString, method->method->function->name->length + 16, "<Bound Method %s>", method->method->function->name->chars);
                        break;
                    }
                }
            } else {
                switch (method->method->function->type) {
                    case TYPE_STATIC: {
                        methodString = malloc(sizeof(char) * 16);
                        memcpy(methodString, "<Static Method>", 15);
                        methodString[15] = '\0';
                        break;
                    }

                    default: {
                        methodString = malloc(sizeof(char) * 15);
                        memcpy(methodString, "<Bound Method>", 14);
                        methodString[14] = '\0';
                        break;
                    }
                }
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
                memcpy(closureString, "<Script>", 8);
                closureString[8] = '\0';
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
                memcpy(functionString, "<fn>", 4);
                functionString[4] = '\0';
            }

            return functionString;
        }

        case OBJ_INSTANCE: {
            return instanceToString(value);
        }

        case OBJ_NATIVE: {
            char *nativeString = malloc(sizeof(char) * 12);
            memcpy(nativeString, "<fn native>", 11);
            nativeString[11] = '\0';
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
            snprintf(fileString, strlen(file->path) + 8, "<File %s>", file->path);
            return fileString;
        }

        case OBJ_LIST: {
            return listToString(value);
        }

        case OBJ_DICT: {
            return dictToString(value);
        }

        case OBJ_SET: {
            return setToString(value);
        }

        case OBJ_UPVALUE: {
            char *upvalueString = malloc(sizeof(char) * 8);
            memcpy(upvalueString, "upvalue", 7);
            upvalueString[7] = '\0';
            return upvalueString;
        }

        // TODO: Think about string conversion for abstract types
        case OBJ_ABSTRACT: {
            break;
        }

        case OBJ_RESULT: {
            ObjResult *result = AS_RESULT(value);
            if (result->status == SUCCESS) {
                char *resultString = malloc(sizeof(char) * 13);
                snprintf(resultString, 13, "<Result Suc>");
                return resultString;
            }

            char *resultString = malloc(sizeof(char) * 13);
            snprintf(resultString, 13, "<Result Err>");
            return resultString;
        }
    }

    char *unknown = malloc(sizeof(char) * 9);
    snprintf(unknown, 8, "%s", "unknown");
    return unknown;
}
