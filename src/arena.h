#ifndef ARENA_H
#define ARENA_H
#include "./common.h"

struct Arena
{
  u64 memory;
  u64   ptr;
  u64   maxSize;
  bool top;
};
u64 ArenaPush(Arena* arena, u64 size);
void  ArenaPop(Arena* arena, u64 size);
#define ArenaPushArray(arena, type, count) (type*)ArenaPush((arena), sizeof(type) * (count))
#define ArenaPushStruct(arena, type)       ArenaPushArray((arena), type, 1)

#endif
