#ifndef dictu_chunk_h
#define dictu_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_NEW_LIST,
    OP_ADD_LIST,
    OP_SUBSCRIPT,
    OP_SUBSCRIPT_ASSIGN,
    OP_POP,
    OP_NO_PRINT,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_INCREMENT,
    OP_DECREMENT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MOD,
    OP_NOT,
    OP_NEGATE,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_BREAK,
    OP_CALL_0,
    OP_CALL_1,
    OP_CALL_2,
    OP_CALL_3,
    OP_CALL_4,
    OP_CALL_5,
    OP_CALL_6,
    OP_CALL_7,
    OP_CALL_8,
    OP_CALL_9,
    OP_CALL_10,
    OP_CALL_11,
    OP_CALL_12,
    OP_CALL_13,
    OP_CALL_14,
    OP_CALL_15,
    OP_CALL_16,
    OP_CALL_17,
    OP_CALL_18,
    OP_CALL_19,
    OP_CALL_20,
    OP_CALL_21,
    OP_CALL_22,
    OP_CALL_23,
    OP_CALL_24,
    OP_CALL_25,
    OP_CALL_26,
    OP_CALL_27,
    OP_CALL_28,
    OP_CALL_29,
    OP_CALL_30,
    OP_CALL_31,
    OP_CALL_32,
    OP_INVOKE_0,
    OP_INVOKE_1,
    OP_INVOKE_2,
    OP_INVOKE_3,
    OP_INVOKE_4,
    OP_INVOKE_5,
    OP_INVOKE_6,
    OP_INVOKE_7,
    OP_INVOKE_8,
    OP_INVOKE_9,
    OP_INVOKE_10,
    OP_INVOKE_11,
    OP_INVOKE_12,
    OP_INVOKE_13,
    OP_INVOKE_14,
    OP_INVOKE_15,
    OP_INVOKE_16,
    OP_INVOKE_17,
    OP_INVOKE_18,
    OP_INVOKE_19,
    OP_INVOKE_20,
    OP_INVOKE_21,
    OP_INVOKE_22,
    OP_INVOKE_23,
    OP_INVOKE_24,
    OP_INVOKE_25,
    OP_INVOKE_26,
    OP_INVOKE_27,
    OP_INVOKE_28,
    OP_INVOKE_29,
    OP_INVOKE_30,
    OP_INVOKE_31,
    OP_INVOKE_32,
    OP_SUPER_0,
    OP_SUPER_1,
    OP_SUPER_2,
    OP_SUPER_3,
    OP_SUPER_4,
    OP_SUPER_5,
    OP_SUPER_6,
    OP_SUPER_7,
    OP_SUPER_8,
    OP_SUPER_9,
    OP_SUPER_10,
    OP_SUPER_11,
    OP_SUPER_12,
    OP_SUPER_13,
    OP_SUPER_14,
    OP_SUPER_15,
    OP_SUPER_16,
    OP_SUPER_17,
    OP_SUPER_18,
    OP_SUPER_19,
    OP_SUPER_20,
    OP_SUPER_21,
    OP_SUPER_22,
    OP_SUPER_23,
    OP_SUPER_24,
    OP_SUPER_25,
    OP_SUPER_26,
    OP_SUPER_27,
    OP_SUPER_28,
    OP_SUPER_29,
    OP_SUPER_30,
    OP_SUPER_31,
    OP_SUPER_32,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_SUBCLASS,
    OP_METHOD
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t *code;
    int *lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);

void freeChunk(Chunk *chunk);

void writeChunk(Chunk *chunk, uint8_t byte, int line);

int addConstant(Chunk *chunk, Value value);

#endif
