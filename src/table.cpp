#include "table.h"
#include "arena.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define IS_EMPTY(map, i) (map->keys[i].hash == 0 && map->values[i].sum == 0)

#define TABLE_MAX_LOAD   0.50

static inline void allocateMapArena(Arena* arena, HashMap* map, int len)
{
  size_t keySize = (sizeof(Key) * len);

  u64    idx     = (u64)(arena->memory + (u64)(arena->top * arena->maxSize * 0.5f));
  map->keys      = (Key*)(idx);
  map->values    = (Value*)(idx + keySize);
  arena->top     = !arena->top;
}

static void resizeHashMap(Arena* arena, HashMap* map)
{
  if (map->len / (float)map->cap >= TABLE_MAX_LOAD)
  {
    u32 prevCap = map->cap;
    map->cap *= 2;

    Key*   oldKeys   = map->keys;
    Value* oldValues = map->values;

    allocateMapArena(arena, map, map->cap);

    for (u32 i = 0; i < map->cap; i++)
    {
      map->keys[i] = (Key){
          .key = (String){.len = 0, .buffer = 0},
            .hash = 0
      };
      map->values[i] = (Value){
          .count = 0,
          .sum   = 0,
          .max   = 0,
          .min   = 0,
      };
    }

    map->len = 0;

    for (int i = 0; i < prevCap; i++)
    {
      if (oldKeys[i].hash != 0)
      {
        updateHashMap(arena, map, oldKeys[i], oldValues[i]);
      }
    }
  }
}

void debugHashMap(HashMap map)
{
  for (int i = 0; i < map.cap; i++)
  {
    printf("%d: (%.*s, %d %ld %d)\n", i, (i32)map.keys[i].key.len, map.keys[i].key.buffer, map.values[i].sum, map.values[i].count, map.values[i].max);
  }
  printf("-\n");
}

void initHashMap(Arena* arena, HashMap* map, int len)
{

  allocateMapArena(arena, map, len);

  for (int i = 0; i < len; i++)
  {
    map->keys[i].hash       = 0;
    map->keys[i].key.buffer = 0;
    map->values[i].sum      = 0;
    map->values[i].count    = 0;
    map->values[i].max      = 0;
  }
  map->len = 0;
  map->cap = len;
}

static inline i32 min(i32 a, i32 b)
{
  return a ^ ((b ^ a) & -(b < a));
}

static inline i32 max(i32 a, i32 b)
{
  return a ^ ((b ^ a) & -(b > a));
}

void updateHashMap(Arena* arena, HashMap* map, Key key, Value value)
{
  resizeHashMap(arena, map);
  u32    idx    = key.hash % map->cap;

  Key*   keys   = map->keys;
  Value* values = map->values;

  while (true)
  {
    if (IS_EMPTY(map, idx))
    {
      keys[idx]   = (Key){.key = key.key, .hash = key.hash};
      values[idx] = value;
      map->len++;
      return;
    }

    if (keys[idx].hash == key.hash)
    {
      Value* val = &values[idx];
      val->sum += value.sum;
      val->count++;
      val->max = max(value.sum, val->max);
      val->min = min(value.sum, val->max);
      return;
    }

    idx = (idx + 1) % map->cap;
  }
}

Value* lookupHashMap(HashMap* map, Key key)
{
  uint32_t idx = key.hash % map->cap;

  if (map->keys[idx].hash == key.hash)
  {
    return &map->values[idx];
  }
  uint32_t start = idx;

  idx++;
  idx %= map->cap;

  while (idx != start)
  {
    if (map->keys[idx].hash == key.hash)
    {
      return &map->values[idx];
    }
    idx++;
    idx %= map->cap;
    // Isn't there
    if (IS_EMPTY(map, idx))
    {
      return NULL;
    }
  }

  return NULL;
}
