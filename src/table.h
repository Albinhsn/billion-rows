#ifndef TABLE_H
#define TABLE_H

#include "arena.h"
#include "common.h"
#include "string.h"
#include <stdint.h>

struct Value
{
  u64 count;
  i32 sum;
  i32 max;
  i32 min;
};

struct Key
{
  String key;
  u32    hash;
};
struct HashMap
{
  u32    cap;
  u32    len;
  Key*   keys;
  Value* values;
  u32    mask;
};

struct Pair
{
  Key   key;
  Value value;
};

Value* lookupHashMap(struct HashMap* map, Key key);
void   initHashMap(Arena* arena, HashMap* map, int len);
void   updateHashMap(Arena* arena, HashMap* map, Key key, Value value);
void   debugHashMap(HashMap map);

#endif
