#ifndef dictu_registry_h
#define dictu_registry_h

#include "common.h"
#include "value.h"

typedef struct {
    union {
        Value occupied;

        uint32_t vacant;
    };

    bool isOccupied;
} RegistryEntry;

typedef struct {
    RegistryEntry *entries;

    uint32_t capacity;

    uint32_t count;

    uint32_t nextFree;
} Registry;

/**
 *  Initializes `registry`.
 */
void initRegistry(Registry *registry);

/**
 * Frees all resources associated with `registry` using `vm` as the memory allocator and owning
 * virtual machine.
 */
void freeRegistry(DictuVM *vm, Registry *registry);

/**
 * Inserts `value` into `registry` as a new key using `vm` as the memory allocator and owning
 * virtual machine.
 */
uint32_t registryInsert(DictuVM *vm, Registry *registry, Value value);

/**
 * Attempts to return at value at `handle` from `registry`, returning `NIL_VAL` if the handle does
 * not exist.
 */
Value registryLookup(Registry *registry, const uint32_t handle);

/**
 * Attempts to remove a value at `handle` from the `registry`, returning the value if it was
 * successfully removed, otherwise `NIL_VAL` if the handle does not exist.
 */
Value registryRemove(Registry *registry, const uint32_t handle);

#endif
