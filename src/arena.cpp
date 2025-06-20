
u64 ArenaPush(Arena *arena, u64 size) {
  if (arena->ptr + size > arena->maxSize) {
    printf("Assigned %lld + %lld out of %lld\n", arena->ptr, size, arena->maxSize);
    exit(1);
  }
  u64 out = arena->memory + arena->ptr;
  arena->ptr += size;
  return out;
}
void ArenaPop(Arena *arena, u64 size) { arena->ptr -= size; }
