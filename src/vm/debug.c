#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "object.h"
#include "value.h"

void disassembleChunk(Chunk *chunk, const char *name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int constantInstruction(const char *name, Chunk *chunk,
                               int offset) {
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2;
}

static int invokeInstruction(const char* name, Chunk* chunk,
                             int offset) {
    uint8_t argCount = chunk->code[offset + 1];
    uint8_t constant = chunk->code[offset + 2];
    printf("%-16s (%d args) %4d '", name, argCount, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 3;
}

static int builtinImportInstruction(const char* name, Chunk* chunk,
                             int offset) {
    uint8_t module = chunk->code[offset + 2];
    printf("%-16s '", name);
    printValue(chunk->constants.values[module]);
    printf("'\n");
    return offset + 3;
}

static int builtinFromImportInstruction(const char* name, Chunk* chunk,
                                    int offset) {
    uint8_t module = chunk->code[offset + 2];
    uint8_t argCount = chunk->code[offset + 3];
    printf("%-16s '", name);
    printValue(chunk->constants.values[module]);
    printf("'\n");
    return offset + 3 + argCount;
}

static int classInstruction(const char* name, Chunk* chunk,
                            int offset) {
    uint8_t type = chunk->code[offset + 1];
    uint8_t constant = chunk->code[offset + 2];
    char *typeString;

    switch (type) {
        case CLASS_DEFAULT: {
            typeString = "default";
            break;
        }

        case CLASS_ABSTRACT: {
            typeString = "abstract";
            break;
        }

        case CLASS_TRAIT: {
            typeString = "trait";
            break;
        }

        default: {
            typeString = "default";
            break;
        }
    }


    printf("%-16s (Type: %s) %4d '", name, typeString, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 3;
}

static int simpleInstruction(const char *name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int byteInstruction(const char *name, Chunk *chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int jumpInstruction(const char *name, int sign, Chunk *chunk,
                           int offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int disassembleInstruction(Chunk *chunk, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);
        case OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OP_POP_REPL:
            return simpleInstruction("OP_POP_REPL", offset);
        case OP_GET_LOCAL:
            return byteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return byteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:
            return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_GET_MODULE:
            return constantInstruction("OP_GET_MODULE", chunk, offset);
        case OP_DEFINE_MODULE:
            return constantInstruction("OP_DEFINE_MODULE", chunk, offset);
        case OP_DEFINE_OPTIONAL:
            return constantInstruction("OP_DEFINE_OPTIONAL", chunk, offset);
        case OP_SET_MODULE:
            return constantInstruction("OP_SET_MODULE", chunk, offset);
        case OP_GET_UPVALUE:
            return byteInstruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:
            return byteInstruction("OP_SET_UPVALUE", chunk, offset);
        case OP_GET_PROPERTY:
            return constantInstruction("OP_GET_PROPERTY", chunk, offset);
        case OP_GET_PRIVATE_PROPERTY:
            return constantInstruction("OP_GET_PRIVATE_PROPERTY", chunk, offset);
        case OP_GET_PROPERTY_NO_POP:
            return constantInstruction("OP_GET_PROPERTY_NO_POP", chunk, offset);
        case OP_GET_PRIVATE_PROPERTY_NO_POP:
            return constantInstruction("OP_GET_PRIVATE_PROPERTY_NO_POP", chunk, offset);
        case OP_SET_PROPERTY:
            return constantInstruction("OP_SET_PROPERTY", chunk, offset);
        case OP_SET_PRIVATE_PROPERTY:
            return constantInstruction("OP_SET_PRIVATE_PROPERTY", chunk, offset);
        case OP_SET_CLASS_VAR:
            return constantInstruction("OP_SET_CLASS_VAR", chunk, offset);
        case OP_SET_INIT_PROPERTIES:
            return constantInstruction("OP_SET_INIT_PROPERTIES", chunk, offset);
        case OP_SET_PRIVATE_INIT_PROPERTIES:
            return constantInstruction("OP_SET_PRIVATE_INIT_PROPERTIES", chunk, offset);
        case OP_GET_SUPER:
            return constantInstruction("OP_GET_SUPER", chunk, offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", offset);
        case OP_POW:
            return simpleInstruction("OP_POW", offset);
        case OP_MOD:
            return simpleInstruction("OP_MOD", offset);
        case OP_BITWISE_AND:
            return simpleInstruction("OP_BITWISE_AND", offset);
        case OP_BITWISE_XOR:
            return simpleInstruction("OP_BITWISE_XOR", offset);
        case OP_BITWISE_OR:
            return simpleInstruction("OP_BITWISE_OR", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_COMPARE_JUMP:
	        return jumpInstruction("OP_COMPARE_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP_IF_NIL:
            return jumpInstruction("OP_JUMP_IF_NIL", 1, chunk, offset);
        case OP_LOOP:
            return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_IMPORT:
            return constantInstruction("OP_IMPORT", chunk, offset);
        case OP_IMPORT_BUILTIN:
            return builtinImportInstruction("OP_IMPORT_BUILTIN", chunk, offset);
        case OP_IMPORT_BUILTIN_VARIABLE:
            return builtinFromImportInstruction("OP_IMPORT_BUILTIN_VARIABLE", chunk, offset);
        case OP_IMPORT_VARIABLE:
            return simpleInstruction("OP_IMPORT_VARIABLE", offset);
        case OP_IMPORT_FROM:
            return constantInstruction("OP_IMPORT_FROM", chunk, offset);
        case OP_IMPORT_END:
            return simpleInstruction("OP_IMPORT_END", offset);
        case OP_NEW_LIST:
            return byteInstruction("OP_NEW_LIST", chunk, offset);
        case OP_UNPACK_LIST:
            return byteInstruction("OP_UNPACK_LIST", chunk, offset);
        case OP_SUBSCRIPT:
            return simpleInstruction("OP_SUBSCRIPT", offset);
        case OP_SUBSCRIPT_ASSIGN:
            return simpleInstruction("OP_SUBSCRIPT_ASSIGN", offset);
        case OP_SLICE:
            return simpleInstruction("OP_SLICE", offset);
        case OP_SUBSCRIPT_PUSH:
            return simpleInstruction("OP_SUBSCRIPT_PUSH", offset);
        case OP_NEW_DICT:
            return byteInstruction("OP_NEW_DICT", chunk, offset);
        case OP_CALL:
            return byteInstruction("OP_CALL", chunk, offset);
        case OP_INVOKE_INTERNAL:
            return invokeInstruction("OP_INVOKE_INTERNAL", chunk, offset);
        case OP_INVOKE:
            return invokeInstruction("OP_INVOKE", chunk, offset);
        case OP_SUPER:
            return invokeInstruction("OP_SUPER_", chunk, offset);
        case OP_CLOSURE: {
            offset++;
            uint8_t constant = chunk->code[offset++];
            printf("%-16s %4d ", "OP_CLOSURE", constant);
            printValue(chunk->constants.values[constant]);
            printf("\n");

            ObjFunction *function = AS_FUNCTION(
                    chunk->constants.values[constant]);
            for (int j = 0; j < function->upvalueCount; j++) {
                int isLocal = chunk->code[offset++];
                int index = chunk->code[offset++];
                printf("%04d   |                     %s %d\n",
                       offset - 2, isLocal ? "local" : "upvalue", index);
            }

            return offset;
        }

        case OP_CLOSE_UPVALUE:
            return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        case OP_EMPTY:
            return simpleInstruction("OP_EMPTY", offset);
        case OP_CLASS:
            return classInstruction("OP_CLASS", chunk, offset);
        case OP_SUBCLASS:
            return classInstruction("OP_SUBCLASS", chunk, offset);
        case OP_END_CLASS:
            return simpleInstruction("OP_END_CLASS", offset);
        case OP_METHOD:
            return constantInstruction("OP_METHOD", chunk, offset);
        case OP_ENUM:
            return constantInstruction("OP_ENUM", chunk, offset);
        case OP_SET_ENUM_VALUE:
            return constantInstruction("OP_SET_ENUM_VALUE", chunk, offset);
        case OP_USE:
            return constantInstruction("OP_USE", chunk, offset);
        case OP_OPEN_FILE:
            return constantInstruction("OP_OPEN_FILE", chunk, offset);
        case OP_CLOSE_FILE:
            return constantInstruction("OP_CLOSE_FILE", chunk, offset);
        case OP_BREAK:
            return simpleInstruction("OP_BREAK", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
