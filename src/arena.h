#pragma once

// Diagnostics for debugging
struct ArenaDiagnostics {
    u64 totalUsed; // Accumulated used
    u64 maxUsed;   // Updated at every push
    u64 totalFreed;
    u64 maxFreed;

    // Counts
    u64 initCount;
    u64 pushCount; // Includes pushZeroCount as well
    u64 pushZeroCount;
    u64 popCount;
    u64 clearCount;
    u64 setPosCount;
};

struct Arena {
    u8* base;
    u64 used;
    u64 size;

    ArenaDiagnostics diagn;
};

static void
ArenaInit(Arena* arena, void* base, u64 size) {
    *arena = {};

    arena->base = static_cast<u8*>(base);
    arena->size = size;

    ++arena->diagn.initCount;
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
    arena->diagn.totalUsed += size;
    arena->diagn.maxUsed = MAX(arena->used, arena->diagn.maxUsed);
    ++arena->diagn.pushCount;
    ASSERT(arena->diagn.pushCount >= arena->diagn.pushZeroCount);

    return result;
}

static void*
ArenaPushZero(Arena* arena, u64 size) {
    ++arena->diagn.pushZeroCount;

    void* result = ArenaPush(arena, size);
    ZeroMemory(result, size);
    return result;
}

static void
ArenaPop(Arena* arena, u64 size) {
    ASSERT(arena->used >= size);

    arena->diagn.totalFreed += size;
    arena->diagn.maxFreed = MAX(size, arena->diagn.maxFreed);
    ++arena->diagn.popCount;

    arena->used -= size;
}

static void
ArenaClear(Arena* arena) {
    arena->diagn.totalFreed += arena->used;
    arena->diagn.maxFreed = MAX(arena->used, arena->diagn.maxFreed);
    ++arena->diagn.clearCount;

    arena->used = 0;
}

static u64
ArenaGetPos(Arena* arena) {
    return arena->used;
}

static void
ArenaSetPos(Arena* arena, u64 pos) {
    // It would be a programmer error to set equal to size
    ASSERT( //pos >= 0 && // unsigned...
        pos < arena->size);
    arena->used = pos;

    ++arena->diagn.setPosCount;
}

#define PushArray(arena, type, count) (type*)ArenaPush((arena), sizeof(type) * (count))
#define PushArrayZero(arena, type, count) (type*)ArenaPushZero((arena), sizeof(type) * (count))
#define PushStruct(arena, type) PushArray((arena), type, 1)
#define PushStructZero(arena, type) PushArrayZero((arena), type, 1)
