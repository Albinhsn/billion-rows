#include "table.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define IS_EMPTY(map, i)     (map->keys[i].hash == 0 && map->values[i].sum == 0)
#define IS_TOMBSTONE(map, i) (map->keys[i].hash != 0 && map->keys[i].key.buffer == 0)

#define TABLE_MAX_LOAD       0.90

static u32 hashString(String key)
{
  u32 hash = 2166136261u;
  for (int i = 0; i < key.len; i++)
  {
    hash ^= key.buffer[i];
    hash *= 16777619;
  }

  return hash;
}

static void resizeHashMap(HashMap* map)
{
  if (map->len / (float)map->cap >= TABLE_MAX_LOAD)
  {
    u32 prevCap = map->cap;
    map->cap *= 2;

    Key* oldKeys     = map->keys;
    map->keys        = (Key*)malloc(sizeof(Key) * map->cap);

    Value* oldValues = map->values;
    map->values      = (Value*)malloc(sizeof(Value) * map->cap);

    for (int i = 0; i < map->cap; i++)
    {
      map->keys[i].hash       = 0;
      map->keys[i].key.buffer = 0;
      map->keys[i].key.len    = 0;
      map->keys[i].distance   = 0;
      map->values[i].sum      = 0;
      map->values[i].max      = 0;
      map->values[i].min      = 0;
      map->values[i].count    = 0;
    }

    map->len = 0;

    for (int i = 0; i < prevCap; i++)
    {
      if (oldKeys[i].hash != 0)
      {
        updateHashMap(map, oldKeys[i].key, oldValues[i]);
      }
    }
    free(oldKeys);
    free(oldValues);
  }
}

void debugHashMap(HashMap map)
{
  for (int i = 0; i < map.cap; i++)
  {
    printf("%d: (%.*s, %lf %ld %lf), %d\n", i, (i32)map.keys[i].key.len, map.keys[i].key.buffer, map.values[i].sum, map.values[i].count, map.values[i].max, map.keys[i].distance);
  }
  printf("-\n");
}

void initHashMap(HashMap* map, int len)
{
  map->keys   = (Key*)malloc(sizeof(Key) * len);
  map->values = (Value*)malloc(sizeof(Value) * len);
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

static inline u32 probe_distance(u32 hash, u32 cap, u32 pos)
{
  u32 preferred = hash % cap;
  return preferred <= pos ? pos - preferred : cap + pos - preferred;
}

void updateHashMap(HashMap* map, String key, Value value)
{
  resizeHashMap(map);
  u32 hash     = hashString(key);
  u32 idx      = hash % map->cap;

  u32 distance = 0;

  while (true)
  {
    if (IS_EMPTY(map, idx))
    {
      map->keys[idx]   = (Key){.key = key, .hash = hash, .distance = distance};
      map->values[idx] = value;
      map->len++;
      return;
    }

    if (map->keys[idx].hash == hash)
    {
      map->values[idx].sum += value.sum;
      map->values[idx].count++;
      map->values[idx].max = value.sum > map->values[idx].max ? value.sum : map->values[idx].max;
      map->values[idx].min = value.sum < map->values[idx].min ? value.sum : map->values[idx].min;
      return;
    }

    u32 prob_dis = probe_distance(map->keys[idx].hash, map->cap, idx);
    if (distance > prob_dis)
    {
      if (IS_TOMBSTONE(map, idx))
      {
        map->keys[idx]   = (Key){.key = key, .hash = hash, .distance = distance};
        map->values[idx] = value;
        return;
      }
      Key   tmpKey     = map->keys[idx];
      Value tmpValue   = map->values[idx];

      map->keys[idx]   = (Key){.key = key, .hash = hash, .distance = distance};
      map->values[idx] = value;

      hash             = tmpKey.hash;
      key              = tmpKey.key;
      value            = tmpValue;

      distance         = prob_dis;
    }
    idx = (idx + 1) % map->cap;
    ++distance;
  }
}

Value *lookupHashMap(struct HashMap *map, Key key) {
  uint32_t idx = key.hash % map->cap;

  if (map->keys[idx].hash == key.hash) {
    return &map->values[idx];
  }
  uint32_t start = idx;

  idx++;
  idx %= map->cap;

  while (idx != start) {
    if (map->keys[idx].hash == key.hash) {
      return &map->values[idx];
    }
    idx++;
    idx %= map->cap;
    // Isn't there
    if (IS_EMPTY(map, idx)) {
      return NULL;
    }
  }

  return NULL;
}
