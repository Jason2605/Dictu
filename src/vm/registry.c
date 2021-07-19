#include "registry.h"

#include "memory.h"

static bool isHandleValid(const Registry *registry, const uint32_t handle) {
    return ((handle < registry->count) && registry->entries[handle].isOccupied);
}

void initRegistry(Registry *registry) {
    registry->entries = NULL;
    registry->count = 0;
    registry->capacity = 0;
    registry->nextFree = 0;
}

void freeRegistry(DictuVM *vm, Registry *registry) {
    FREE_ARRAY(vm, RegistryEntry, registry->entries, registry->capacity + 1);
}

uint32_t registryInsert(DictuVM *vm, Registry *registry, Value value) {
    uint32_t handle;

    if (registry->nextFree == registry->capacity) {
        // Conditionally grow if the existing registry buffer is full.
        if (registry->count == registry->capacity) {
            uint32_t const oldCapacity = registry->capacity;
            registry->capacity = ((registry->capacity == 0) ? 2 : (registry->capacity * 2));

            registry->entries = GROW_ARRAY(
                vm,
                registry->entries,
                RegistryEntry,
                oldCapacity,
                registry->capacity
            );
        }

        handle = registry->count;
        registry->nextFree += 1;
    } else {
        handle = registry->nextFree;
        registry->nextFree = registry->entries[handle].vacant;
    }

    registry->entries[handle] = (RegistryEntry){
        .occupied = value,
        .isOccupied = true
    };

    registry->count += 1;

    return handle;
}

Value registryLookup(Registry *registry, const uint32_t handle) {
    return (isHandleValid(registry, handle) ? registry->entries[handle].occupied : NIL_VAL);
}

Value registryRemove(Registry *registry, const uint32_t handle) {
    if (isHandleValid(registry, handle)) {
        Value value = registry->entries[handle].occupied;

        registry->entries[handle] = (RegistryEntry){
            .vacant = registry->nextFree,
            .isOccupied = false
        };

        registry->nextFree = handle;
        registry->count -= 1;

        return value;
    }

    return NIL_VAL;
}
