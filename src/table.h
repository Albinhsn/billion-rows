#ifndef TABLE_H
#define TABLE_H

#include "arena.h"
#include "common.h"
#include "string.h"
#include <stdint.h>

struct Value
{
  u64 count;
  i64 sum;
  i64 max;
  i64 min;
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
};

Value* lookupHashMap(struct HashMap* map, Key key);
void   initHashMap(Arena* arena, HashMap* map, int len);
void   updateHashMap(Arena* arena, HashMap* map, Key key, Value value);
void   debugHashMap(HashMap map);

#endif
