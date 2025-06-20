#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <sys/stat.h>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>


#include "common.h"
#include "common.cpp"
#include "arena.h"
#include "arena.cpp"
#include "string.h"
#include "table.h"

#define TABLE_MAX_LOAD    0.50
#define NUMBER_OF_THREADS 15

#define Assert(Expr)\
  if(!(Expr))\
    *(int*)0 = 5;

struct Buffer
{
  u8* buffer;
  u64 curr;
  u64 len;
};

struct KeyIdx
{
  Key key;
  u64 idx;
};

struct IdxPair
{
  u32  idx;
  bool inserted;
};

struct ParsePairsArgs
{
  Buffer   buffer;
  HashMap* map;
  Arena*   arena;
  u32      threadId;
};
u8*                fileContent;

inline void * 
Allocate(u64 Size)
{
  return VirtualAlloc(0, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}


static inline bool isOutOfBounds(Buffer* buffer)
{
  return buffer->curr >= buffer->len;
}

static inline u8 getCurrentCharBuffer(Buffer* buffer)
{
  return buffer->buffer[buffer->curr];
}

static inline void advanceBuffer(Buffer* buffer)
{
  buffer->curr++;
}

inline i32 parseDigitFromChar(Buffer* buffer)
{
  return getCurrentCharBuffer(buffer) - (u8)'0';
}

static inline i32 Min(i32 a, i32 b)
{
  return a ^ ((b ^ a) & -(b < a));
}

static inline i32 Max(i32 a, i32 b)
{
  return a ^ ((b ^ a) & -(b > a));
}

static inline int cmp(const void* a_, const void* b_)
{
  String a   = ((KeyIdx*)a_)->key.key;
  String b   = ((KeyIdx*)b_)->key.key;

  int    res = strncmp((char*)a.buffer, (char*)b.buffer, Min(a.len, b.len));
  return res != 0 ? res : a.len - b.len;
}

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
    map->mask        = map->cap - 1;

    Key*   oldKeys   = map->keys;
    Value* oldValues = map->values;

    allocateMapArena(arena, map, map->cap);

    for (u32 i = 0; i < map->cap; i++)
    {
      map->keys[i] = (Key){
          .key  = (String){.len = 0, .buffer = 0},
          .hash = 0,
      };
      map->values[i] = (Value){
          .count = 0,
          .sum   = 0,
          .max   = 0,
          .min   = 0,
      };
    }

    map->len = 0;

    for (u32 i = 0; i < prevCap; i++)
    {
      if (oldKeys[i].hash != 0)
      {
        updateHashMap(arena, map, oldKeys[i], oldValues[i]);
      }
    }
  }
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
    map->values[i].max      = -100;
    map->values[i].min      = 100;
  }
  map->len  = 0;
  map->mask = len - 1;
  map->cap  = len;
}

static inline IdxPair getIdx(Key* keys, Key key, u32 mask)
{
  u32 idx = key.hash & mask;
  while (true)
  {
    if (keys[idx].hash == 0 || keys[idx].hash == key.hash)
    {
      IdxPair out = (IdxPair){.idx = idx, .inserted = keys[idx].hash == 0};
      keys[idx]   = key;
      return out;
    }
    idx = (idx + 1) & mask;
  }
}

void updateHashMap(Arena* arena, HashMap* map, Key key, Value value)
{
  resizeHashMap(arena, map);

  IdxPair idx = getIdx(map->keys, key, map->mask);
  Value*  val = &map->values[idx.idx];
  if (idx.inserted)
  {
    map->len++;
    val->sum   = value.sum;
    val->count = 1;
    val->min   = value.min;
    val->max   = value.max;
  }
  else
  {
    val->count += 1;
    val->min = Min(val->min, value.min);
    val->max = Max(val->max, value.max);
    val->sum += value.sum;
  }
}

Value* lookupHashMap(HashMap* map, Key key)
{
  u32  idx  = key.hash & map->cap;

  Key* keys = map->keys;
  if (keys[idx].hash == key.hash)
  {
    return &map->values[idx];
  }
  u32 start = idx;

  idx++;
  idx &= map->mask;

  while (idx != start)
  {
    if (keys[idx].hash == key.hash)
    {
      return &map->values[idx];
    }
    idx++;
    idx &= map->mask;
    // Isn't there
    if (keys[idx].hash == 0)
    {
      return NULL;
    }
  }

  return NULL;
}

static inline i32 parseNumber(Buffer* buffer)
{
  i32 sign = getCurrentCharBuffer(buffer) == '-' ? -1 : 1;
  if (sign == -1)
  {
    advanceBuffer(buffer);
  }
  i32 result = parseDigitFromChar(buffer);
  advanceBuffer(buffer);

  if (getCurrentCharBuffer(buffer) != '.')
  {
    result = result * 10 + parseDigitFromChar(buffer);
    advanceBuffer(buffer);
  }
  advanceBuffer(buffer);

  result = result * 10 + parseDigitFromChar(buffer);
  advanceBuffer(buffer);

  return sign * result;
}

static inline void parseKey(Key* key, Buffer* buffer)
{

  u64 start = buffer->curr;
  while (getCurrentCharBuffer(buffer) != ';')
  {
    key->hash ^= getCurrentCharBuffer(buffer);
    key->hash *= 16777619;
    advanceBuffer(buffer);
  }

  key->key.buffer = &buffer->buffer[start];
  key->key.len    = buffer->curr - start;
}
static inline void parseSum(Value* value, Buffer* buffer)
{

  i32 v        = parseNumber(buffer);
  value->sum   = v;
  value->count = 1;
  value->max   = v;
  value->min   = v;
}

DWORD parsePairs(void* args_)
{
  ParsePairsArgs* parsePairsArgs = (ParsePairsArgs*)args_;
  HashMap*        map            = parsePairsArgs->map;
  Buffer          buffer         = parsePairsArgs->buffer;
  Arena*          arena          = parsePairsArgs->arena;
  arena->maxSize                 = 1024 * 1024 * 4;
  arena->memory                  = (u64)Allocate(arena->maxSize);
  arena->top                     = false;

  initHashMap(arena, map, 4096);

  if (parsePairsArgs->threadId != 0)
  {
    while (getCurrentCharBuffer(&buffer) != '\n')
    {
      advanceBuffer(&buffer);
    }
    advanceBuffer(&buffer);
  }

  Value value;
  while (buffer.curr < buffer.len)
  {
    Key key = (Key){.hash = 2166136261u};

    parseKey(&key, &buffer);
    advanceBuffer(&buffer);
    parseSum(&value, &buffer);
    advanceBuffer(&buffer);
    advanceBuffer(&buffer);

    updateHashMap(arena, map, key, value);
  }

  return 0;
}



DWORD touchyTouchy(void* args)
{
  u64 fileSize = *(u64*)args;
  for (u32 i = 0; i < fileSize; i++)
  {
    u8 Read = fileContent[i];
  }
  return 0;
}

int main(int argc, char** argv)
{

  profiler.bestTime = FLT_MAX;

  initProfiler();

  struct __stat64 fileStat;
  _stat64(argv[1], &fileStat);

  size_t fileSize = fileStat.st_size;

  HANDLE File = CreateFileA(argv[1], GENERIC_READ, FILE_SHARE_READ, 0,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  HANDLE Mapping = CreateFileMappingA(File, 0, PAGE_READONLY, 0, 0, 0);
  fileContent = (u8*)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);

  Arena          lastArena;
  Arena          arenas[NUMBER_OF_THREADS];
  HashMap        lastMap;
  HashMap        maps[NUMBER_OF_THREADS];
  HANDLE         threadIds[1 + NUMBER_OF_THREADS];
  ParsePairsArgs args[NUMBER_OF_THREADS];
  Buffer         buffers[NUMBER_OF_THREADS];

  u64            batchSize = fileSize / NUMBER_OF_THREADS;

  // threadIds[NUMBER_OF_THREADS] = CreateThread(0, 0, touchyTouchy, &fileSize, 0, 0);

  for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
  {
    buffers[i] = (Buffer){
        .buffer = &fileContent[i * batchSize],
        .curr   = 0,
        .len    = batchSize,
    };

    args[i] = (ParsePairsArgs){.buffer = buffers[i], .map = &maps[i], .arena = &arenas[i], .threadId = i};
    threadIds[i] = CreateThread(0, 0, parsePairs, &args[i], 0, 0);
  }

  lastArena.maxSize = 1024 * 1024 * 4;
  lastArena.memory  = (u64)Allocate(lastArena.maxSize);
  lastArena.top     = false;
  initHashMap(&lastArena, &lastMap, 4096);

  // WaitForSingleObject(threadIds[NUMBER_OF_THREADS], INFINITE);
  for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
  {
    WaitForSingleObject(threadIds[i], INFINITE);
    Key*   keys   = maps[i].keys;
    Value* values = maps[i].values;
    for (u64 j = 0; j < maps[i].cap; j++)
    {
      if (values[j].count != 0)
      {
        updateHashMap(&lastArena, &lastMap, keys[j], values[j]);
      }
    }
  }

  KeyIdx *keys = (KeyIdx*)Allocate(lastMap.len * sizeof(KeyIdx));
  u64    count = 0;
  for (u64 i = 0; i < lastMap.cap; i++)
  {
    if (lastMap.values[i].count != 0)
    {
      keys[count] = (KeyIdx){.key = lastMap.keys[i], .idx = i};
      count++;
    }
  }

  qsort(keys, lastMap.len, sizeof(KeyIdx), cmp);

  for (u64 i = 0; i < lastMap.len; i++)
  {
    Key   key = keys[i].key;
    Value val = lastMap.values[keys[i].idx];
    printf("%.*s:%.2lf/%.2lf/%.2lf\n", (i32)key.key.len, key.key.buffer, val.min * 0.1f, val.sum / ((f64)val.count * 10.0f), val.max * 0.1f);
  }

  u64 endTime      = ReadCPUTimer();
  u64 totalElapsed = endTime - profiler.StartTSC;
  u64 cpuFreq      = EstimateCPUTimerFreq();
  f64 tot          = 1000.0 * (f64)totalElapsed / (f64)cpuFreq;
  printf("Took %lfms\n", tot);
}
