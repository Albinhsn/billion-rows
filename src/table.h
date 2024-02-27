#ifndef TABLE_H
#define TABLE_H

#include "common.h"
#include "string.h"
#include <stdint.h>

struct Value {
  f64 sum;
  u64 count;
  f64 max;
  f64 min;
};

struct Key {
  String key;
  u32 hash;
  u32 distance;
};
struct HashMap {
  u32 cap;
  u32 len;
  Key *keys;
  Value *values;
};

Value *lookupHashMap(struct HashMap *map, Key key);
void initHashMap(HashMap *map, int len);
void updateHashMap(HashMap * map, String key, Value value);
void debugHashMap(HashMap map);

#endif
