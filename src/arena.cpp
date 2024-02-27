#include "arena.h"

u64 ArenaPush(Arena *arena, u64 size) {
  if (arena->ptr + size > arena->maxSize) {
    printf("Assigned %ld + %ld out of %ld\n", arena->ptr, size, arena->maxSize);
    exit(1);
  }
  u64 out = arena->memory + arena->ptr;
  arena->ptr += size;
  return out;
}
void ArenaPop(Arena *arena, u64 size) { arena->ptr -= size; }
