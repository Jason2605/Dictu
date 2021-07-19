#ifndef dictu_object_h
#define dictu_object_h

#include <stdint.h>
#include <stdio.h>

#include "../include/dictu_include.h"
#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value)         (AS_OBJ(value)->type)

#define AS_MODULE(value)        ((ObjModule*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
#define AS_ENUM(value)          ((ObjEnum*)AS_OBJ(value))
#define AS_CLOSURE(value)       ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value)        (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define AS_LIST(value)          ((ObjList*)AS_OBJ(value))
#define AS_DICT(value)          ((ObjDict*)AS_OBJ(value))
#define AS_SET(value)           ((ObjSet*)AS_OBJ(value))
#define AS_FILE(value)          ((ObjFile*)AS_OBJ(value))
#define AS_ABSTRACT(value)      ((ObjAbstract*)AS_OBJ(value))
#define AS_RESULT(value)        ((ObjResult*)AS_OBJ(value))

#define IS_MODULE(value)          isObjType(value, OBJ_MODULE)
#define IS_BOUND_METHOD(value)    isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)           isObjType(value, OBJ_CLASS)
#define IS_DEFAULT_CLASS(value)   isObjType(value, OBJ_CLASS) && AS_CLASS(value)->type == CLASS_DEFAULT
#define IS_TRAIT(value)           isObjType(value, OBJ_CLASS) && AS_CLASS(value)->type == CLASS_TRAIT
#define IS_ENUM(value)            isObjType(value, OBJ_ENUM)
#define IS_CLOSURE(value)         isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)        isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value)        isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)          isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)          isObjType(value, OBJ_STRING)
#define IS_LIST(value)            isObjType(value, OBJ_LIST)
#define IS_DICT(value)            isObjType(value, OBJ_DICT)
#define IS_SET(value)             isObjType(value, OBJ_SET)
#define IS_FILE(value)            isObjType(value, OBJ_FILE)
#define IS_ABSTRACT(value)        isObjType(value, OBJ_ABSTRACT)
#define IS_RESULT(value)          isObjType(value, OBJ_RESULT)

typedef enum {
    OBJ_MODULE,
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_ENUM,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_LIST,
    OBJ_DICT,
    OBJ_SET,
    OBJ_FILE,
    OBJ_ABSTRACT,
    OBJ_RESULT,
    OBJ_UPVALUE
} ObjType;

typedef enum {
    CLASS_DEFAULT,
    CLASS_ABSTRACT,
    CLASS_TRAIT
} ClassType;

typedef enum {
    ACCESS_PUBLIC,
    ACCESS_PRIVATE
} AccessLevel;

typedef enum {
    TYPE_FUNCTION,
    TYPE_ARROW_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_STATIC,
    TYPE_ABSTRACT,
    TYPE_TOP_LEVEL
} FunctionType;

struct sObj {
    ObjType type;
    bool isDark;
    struct sObj *prev;
    struct sObj *next;
};

typedef struct {
    Obj obj;
    ObjString* name;
    ObjString* path;
    Table values;
} ObjModule;

typedef struct {
    Obj obj;
    int arity;
    int arityOptional;
    int upvalueCount;
    Chunk chunk;
    ObjString *name;
    FunctionType type;
    AccessLevel accessLevel;
    ObjModule* module;
    int propertyCount;
    int *propertyNames;
    int *propertyIndexes;
    int privatePropertyCount;
    int *privatePropertyNames;
    int *privatePropertyIndexes;
} ObjFunction;

typedef Value (*NativeFn)(DictuVM *vm, int argCount, Value *args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

struct sObjString {
    Obj obj;
    int length;
    char *chars;
    uint32_t hash;
};

struct sObjList {
    Obj obj;
    ValueArray values;
};

typedef struct {
    Value key;
    Value value;
    uint32_t psl;
} DictItem;

struct sObjDict {
    Obj obj;
    int count;
    int capacityMask;
    DictItem *entries;
};

typedef struct {
    Value value;
    bool deleted;
} SetItem;

struct sObjSet {
    Obj obj;
    int count;
    int capacityMask;
    SetItem *entries;
};

struct sObjFile {
    Obj obj;
    FILE *file;
    char *path;
    char *openType;
};

typedef void (*AbstractFreeFn)(DictuVM *vm, ObjAbstract *abstract);

struct sObjAbstract {
    Obj obj;
    Table values;
    void *data;
    AbstractFreeFn func;
};

typedef enum {
    SUCCESS,
    ERR
} ResultStatus;

struct sObjResult {
    Obj obj;
    ResultStatus status;
    Value value;
};

typedef struct sUpvalue {
    Obj obj;

    // Pointer to the variable this upvalue is referencing.
    Value *value;

    // If the upvalue is closed (i.e. the local variable it was pointing
    // to has been popped off the stack) then the closed-over value is
    // hoisted out of the stack into here. [value] is then be changed to
    // point to this.
    Value closed;

    // Open upvalues are stored in a linked list. This points to the next
    // one in that list.
    struct sUpvalue *next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct sObjClass {
    Obj obj;
    ObjString *name;
    struct sObjClass *superclass;
    Table publicMethods;
    Table privateMethods;
    Table abstractMethods;
    Table publicProperties;
    ClassType type;
} ObjClass;

typedef struct sObjEnum {
    Obj obj;
    ObjString *name;
    Table values;
} ObjEnum;

typedef struct {
    Obj obj;
    ObjClass *klass;
    Table privateFields;
    Table publicFields;
} ObjInstance;

typedef struct {
    Obj obj;
    Value receiver;
    ObjClosure *method;
} ObjBoundMethod;

ObjModule *newModule(DictuVM *vm, ObjString *name);

ObjBoundMethod *newBoundMethod(DictuVM *vm, Value receiver, ObjClosure *method);

ObjClass *newClass(DictuVM *vm, ObjString *name, ObjClass *superclass, ClassType type);

ObjEnum *newEnum(DictuVM *vm, ObjString *name);

ObjClosure *newClosure(DictuVM *vm, ObjFunction *function);

ObjFunction *newFunction(DictuVM *vm, ObjModule *module, FunctionType type, AccessLevel level);

ObjInstance *newInstance(DictuVM *vm, ObjClass *klass);

ObjNative *newNative(DictuVM *vm, NativeFn function);

ObjString *takeString(DictuVM *vm, char *chars, int length);

ObjString *copyString(DictuVM *vm, const char *chars, int length);

ObjList *newList(DictuVM *vm);

ObjDict *newDict(DictuVM *vm);

ObjSet *newSet(DictuVM *vm);

ObjFile *newFile(DictuVM *vm);

ObjAbstract *newAbstract(DictuVM *vm, AbstractFreeFn func);

ObjResult *newResult(DictuVM *vm, ResultStatus status, Value value);

Value newResultSuccess(DictuVM *vm, Value value);

Value newResultError(DictuVM *vm, char *errorMsg);

ObjUpvalue *newUpvalue(DictuVM *vm, Value *slot);

char *setToString(Value value);
char *dictToString(Value value);
char *listToString(Value value);
char *classToString(Value value);
char *instanceToString(Value value);
char *objectToString(Value value);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

static inline ObjType getObjType(Value value) {
    return AS_OBJ(value)->type;
}

#endif
