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

#define NUMBER_OF_THREADS 15

u8* fileContent;

struct Pair
{
  String key;
  Value  value;
};

struct PairStack
{
  Pair* pairs;
  u64   count;
};

struct Buffer
{
  u8* buffer;
  u64 curr;
  u64 len;
};

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
};

void* parsePairs(void* args_)
{
  ParsePairsArgs* parsePairsArgs = (ParsePairsArgs*)args_;
  HashMap*        map            = parsePairsArgs->map;
  Buffer          buffer         = parsePairsArgs->buffer;
  Arena*          arena          = parsePairsArgs->arena;

  while (getCurrentCharBuffer(&buffer) != '\n')
  {
    advanceBuffer(&buffer);
  }
  advanceBuffer(&buffer);

  while (buffer.curr < buffer.len)
  {
    Key   key = (Key){.hash = 2166136261u};
    Value value;
    u64   start = buffer.curr;
    while (getCurrentCharBuffer(&buffer) != ';')
    {
      key.hash ^= getCurrentCharBuffer(&buffer);
      key.hash *= 16777619;
      advanceBuffer(&buffer);
    }

    key.key.buffer = &buffer.buffer[start];
    key.key.len    = buffer.curr - start;

    advanceBuffer(&buffer);

    value.sum   = parseNumber(&buffer);
    value.count = 1;
    value.max   = value.sum;
    value.min   = value.sum;
    advanceBuffer(&buffer);
    updateHashMap(arena, map, key, value);
  }
  printf("Parsed pairs\n");
  return 0;
}

int main()
{

  profiler.bestTime = FLT_MAX;
  for (i32 k = 0; k < 5; k++)
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
        arenas[i].maxSize = 1024 * 1024 * 5;
        arenas[i].memory  = (u64)malloc(sizeof(u8) * arenas[i].maxSize);
        arenas[i].top     = false;

        initHashMap(&arenas[i], &maps[i], 400);
        buffers[i] = (Buffer){
            .buffer = &fileContent[i * batchSize],
            .curr   = 0,
            .len    = batchSize,
        };

        args[i] = (ParsePairsArgs){.buffer = buffers[i], .map = &maps[i], .arena = &arenas[i]};
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
      TimeBlock("Last Gather");
      HashMap map0 = maps[0];
      // Can quite easily be done in parallel
      for (u64 i = 0; i < map0.cap; i++)
      {
        if (map0.values[i].count != 0)
        {
          for (u64 j = 1; j < NUMBER_OF_THREADS; j++)
          {
            Value* val = lookupHashMap(&maps[j], map0.keys[i]);
            if (val)
            {
              map0.values[i].max = val->max > map0.values[i].max ? val->max : map0.values[i].max;
              map0.values[i].min = val->min < map0.values[i].min ? val->min : map0.values[i].min;
              map0.values[i].sum += val->sum;
              map0.values[i].count += val->count;
            }
          }
          fprintf(stderr, "%.*s=%.2lf/%.2lf/%.2lf\n", (i32)map0.keys[i].key.len, map0.keys[i].key.buffer, map0.values[i].min / 10.0f, (map0.values[i].sum * 0.1f) / (f64)map0.values[i].count,
                  map0.values[i].max / 10.0f);
        }
      }
    }

    displayProfilingResult();
    for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
    {
      free((u8*)arenas[i].memory);
    }
  }

  printf("Best was %.4lfms\n", profiler.bestTime);
}
