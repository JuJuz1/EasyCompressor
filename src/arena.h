#pragma once

struct Arena {
    u8* base;
    u64 used;
    u64 size;

    // Debug
    u64 totalUsed; // Accumulated used
    u64 maxUsed;   // Updated at every push
    u64 totalFreed;
    u64 maxFreed;
};

static void
ArenaInit(Arena* arena, void* base, u64 size) {
    *arena = {};

    arena->base = static_cast<u8*>(base);
    arena->size = size;
}

static void*
ArenaPush(Arena* arena, u64 size) {
    ASSERT(arena->used + size <= arena->size);
    //if (arena->used + size > arena->size) {
    //DEBUG_PRINT("ArenaPush: Out of memory!\n");
    //PrintArena(arena); nocheckin
    //ExitProcess(1);
    //}

    void* result = arena->base + arena->used;
    arena->used += size;

    // Debug
    arena->totalUsed += size;
    arena->maxUsed = MAX(arena->used, arena->maxUsed);

    return result;
}

static void*
ArenaPushZero(Arena* arena, u64 size) {
    void* result = ArenaPush(arena, size);
    ZeroMemory(result, size);
    return result;
}

static void
ArenaPop(Arena* arena, u64 size) {
    ASSERT(arena->used >= size);

    arena->totalFreed += size;
    arena->maxFreed = MAX(size, arena->maxUsed);

    arena->used -= size;
}

static void
ArenaClear(Arena* arena) {
    arena->totalFreed += arena->used;
    arena->maxFreed = MAX(arena->used, arena->maxUsed);

    arena->used = 0;
}

static u64
ArenaGetPos(Arena* arena) {
    return arena->used;
}

static void
ArenaSetPos(Arena* arena, u64 pos) {
    // It would be a programmer error to set equal to size
    ASSERT(pos >= 0 && pos < arena->size);
    arena->used = pos;
}

#define PushArray(arena, type, count) (type*)ArenaPush((arena), sizeof(type) * (count))
#define PushArrayZero(arena, type, count) (type*)ArenaPushZero((arena), sizeof(type) * (count))
#define PushStruct(arena, type) PushArray((arena), type, 1)
#define PushStructZero(arena, type) PushArrayZero((arena), type, 1)
