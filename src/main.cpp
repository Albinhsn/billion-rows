#include "arena.h"
#include "common.h"
#include "string.h"
#include "table.h"
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define IS_EMPTY(keys, i) (keys[i].hash == 0)

#define TABLE_MAX_LOAD    0.50
#define NUMBER_OF_THREADS 15

static inline i32 min(i32 a, i32 b)
{
  return a ^ ((b ^ a) & -(b < a));
}

static inline i32 max(i32 a, i32 b)
{
  return a ^ ((b ^ a) & -(b > a));
}

struct KeyIdx
{
  Key key;
  u64 idx;
};

static inline int cmp(const void* a_, const void* b_)
{
  String a   = ((KeyIdx*)a_)->key.key;
  String b   = ((KeyIdx*)b_)->key.key;

  int    res = strncmp((char*)a.buffer, (char*)b.buffer, min(a.len, b.len));
  return res != 0 ? res : a.len - b.len;
}

u8* fileContent;

struct Buffer
{
  u8* buffer;
  u64 curr;
  u64 len;
};

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

    for (int i = 0; i < prevCap; i++)
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

struct IdxPair
{
  u32  idx;
  bool inserted;
};

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
    val->min = min(val->min, value.min);
    val->max = max(val->max, value.max);
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
    if (IS_EMPTY(keys, idx))
    {
      return NULL;
    }
  }

  return NULL;
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

struct ParsePairsArgs
{
  Buffer   buffer;
  HashMap* map;
  Arena*   arena;
  u32      threadId;
};

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

void* parsePairs(void* args_)
{
  ParsePairsArgs* parsePairsArgs = (ParsePairsArgs*)args_;
  HashMap*        map            = parsePairsArgs->map;
  Buffer          buffer         = parsePairsArgs->buffer;
  Arena*          arena          = parsePairsArgs->arena;
  TimeFunction;

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

    updateHashMap(arena, map, key, value);
  }

  return 0;
}

int main()
{

  profiler.bestTime = FLT_MAX;
  f64 minimum = FLT_MAX, maximum = -FLT_MAX;
  u64 COUNT = 20;
  f64 SUM   = 0.0f;
  for (u64 k = 0; k < COUNT; k++)
  {

    initProfiler();

    const char* fileName = "./measurements1b.txt";
    int         fd       = open(fileName, O_RDONLY);
    struct stat fileStat;
    fstat(fd, &fileStat);

    size_t fileSize = fileStat.st_size;
    fileContent     = (u8*)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);

    HashMap        maps[NUMBER_OF_THREADS];
    pthread_t      threadIds[NUMBER_OF_THREADS];
    Arena          arenas[NUMBER_OF_THREADS];
    ParsePairsArgs args[NUMBER_OF_THREADS];
    Buffer         buffers[NUMBER_OF_THREADS];

    u64            batchSize = fileSize / NUMBER_OF_THREADS;
    {
      TimeBlock("Init parsing");
      for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
      {
        arenas[i].maxSize = 1024 * 1024 * 4;
        arenas[i].memory  = (u64)mmap(NULL, arenas[i].maxSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        arenas[i].top     = false;

        initHashMap(&arenas[i], &maps[i], 4096);
        buffers[i] = (Buffer){
            .buffer = &fileContent[i * batchSize],
            .curr   = 0,
            .len    = batchSize,
        };

        args[i] = (ParsePairsArgs){.buffer = buffers[i], .map = &maps[i], .arena = &arenas[i], .threadId = i};
        pthread_create(&threadIds[i], NULL, parsePairs, (void*)&args[i]);
      }

      for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
      {
        if (pthread_join(threadIds[i], NULL) != 0)
        {
          printf("Failed something with thread?\n");
        }
      }
    }

    {
      HashMap lastMap;
      Arena   lastArena;
      lastArena.maxSize = 1024 * 1024 * 4;
      lastArena.memory  = (u64)mmap(NULL, lastArena.maxSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      lastArena.top     = false;
      initHashMap(&lastArena, &lastMap, 4096);

      TimeBlock("Last Gather");
      for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
      {
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

      KeyIdx keys[lastMap.len];
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

      munmap((u8*)lastArena.memory, lastArena.maxSize);
    }

    u64 endTime      = ReadCPUTimer();
    u64 totalElapsed = endTime - profiler.StartTSC;
    u64 cpuFreq      = EstimateCPUTimerFreq();
    f64 tot          = 1000.0 * (f64)totalElapsed / (f64)cpuFreq;
    displayProfilingResult();
    minimum = tot < minimum ? tot : minimum;
    maximum = tot > maximum ? tot : maximum;
    SUM += tot;

    for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
    {
      munmap((u8*)arenas[i].memory, arenas[i].maxSize);
    }

    close(fd);
  }
  printf("MIN: %lf\n", minimum);
  printf("MAX: %lf\n", maximum);
  printf("MEAN: %lf\n", SUM / (f64)COUNT);
}
