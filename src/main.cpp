#include "common.h"
#include "string.h"
#include "table.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define NUMBER_OF_THREADS 4
#define PAIR_SIZE         4096

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

inline f64 convertJsonNumber(Buffer* buffer)
{
  f64 result = 0.0f;
  while (isdigit(getCurrentCharBuffer(buffer)))
  {
    u8 ch  = getCurrentCharBuffer(buffer) - (u8)'0';
    result = 10.0 * result + (f64)ch;
    advanceBuffer(buffer);
  }
  return result;
}

f64 parseNumber(Buffer* buffer)
{
  f64 sign = getCurrentCharBuffer(buffer) == '-' ? -1.0f : 1.0f;
  if (sign == -1.0f)
  {
    advanceBuffer(buffer);
  }

  f64 result = convertJsonNumber(buffer);

  if (getCurrentCharBuffer(buffer) == '.')
  {
    advanceBuffer(buffer);
    f64 c = 1.0 / 10.0;
    while (isdigit(getCurrentCharBuffer(buffer)))
    {
      u8 ch = getCurrentCharBuffer(buffer) - (u8)'0';
      result += c * (f64)ch;
      c *= 1.0 / 10.0;
      advanceBuffer(buffer);
    }
  }

  return sign * result;
}

void parsePairStack(PairStack* stack, Buffer* buffer)
{
  String key;
  Value  value;

  while (stack->count < PAIR_SIZE && buffer->curr < buffer->len)
  {
    u64 start = buffer->curr;
    while (getCurrentCharBuffer(buffer) != ';')
    {
      advanceBuffer(buffer);
    }
    key.buffer = &buffer->buffer[start];
    key.len    = buffer->curr - start;

    advanceBuffer(buffer);

    value.sum   = parseNumber(buffer);
    value.count = 1;
    value.max   = value.sum;
    value.min   = value.sum;
    advanceBuffer(buffer);

    stack->pairs[stack->count].value = value;
    stack->pairs[stack->count].key   = key;
    stack->count++;
  }
}

struct SumPairsArgs
{
  PairStack** stack;
  HashMap*    map;
  bool*       done;
};

void* sumPairs(void* args_)
{
  SumPairsArgs* args      = (SumPairsArgs*)args_;
  PairStack**   pairStack = args->stack;
  HashMap*      map       = args->map;
  bool*         done      = args->done;

  while (!(*done))
  {
    for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
    {
      if (pairStack[i] != 0)
      {
        Pair* pairs = pairStack[i]->pairs;
        u64   count = pairStack[i]->count;
        for (u32 j = 0; j < count; j++)
        {
          updateHashMap(map, pairs[j].key, pairs[j].value);
        }
        pairStack[i] = 0;
      }
    }
  }
  for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
  {
    if (pairStack[i] != 0)
    {
      Pair* pairs = pairStack[i]->pairs;
      for (u32 j = 0; j < PAIR_SIZE; j++)
      {
        updateHashMap(map, pairs[j].key, pairs[j].value);
      }
      pairStack[i] = 0;
    }
  }

  return 0;
}

struct ParsePairsArgs
{
  PairStack** stack;
  u64*        stackPointers;
  Buffer      buffer;
  bool        done;
};

void* parsePairs(void* args_)
{
  ParsePairsArgs* parsePairsArgs = (ParsePairsArgs*)args_;
  PairStack**     pairStack      = parsePairsArgs->stack;
  u64*            stackPointers  = parsePairsArgs->stackPointers;
  Buffer          buffer         = parsePairsArgs->buffer;

  while (getCurrentCharBuffer(&buffer) != '\n')
  {
    advanceBuffer(&buffer);
  }
  advanceBuffer(&buffer);

  while (buffer.curr < buffer.len)
  {
    for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
    {
      if (pairStack[i] == 0)
      {
        parsePairStack((PairStack*)stackPointers[i], &buffer);
        pairStack[i] = (PairStack*)stackPointers[i];
      }
    }
  }
  return 0;
}

int main()
{

  initProfiler();

  const char* fileName = "./measurements10000.txt";
  int         fd       = open(fileName, O_RDONLY);
  if (fd == -1)
  {
    printf("Failed to open file '%s'\n", fileName);
    exit(1);
  }

  struct stat fileStat;
  if (fstat(fd, &fileStat) == -1)
  {
    printf("Failed to get file size");
    exit(1);
  }

  size_t fileSize = fileStat.st_size;
  fileContent     = (u8*)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
  if (fileContent == MAP_FAILED)
  {
    printf("error mapping file\n");
    exit(1);
  }

  HashMap        maps[NUMBER_OF_THREADS];
  pthread_t      threadIds[NUMBER_OF_THREADS * 2];

  u64            batchSize = fileSize / NUMBER_OF_THREADS;

  ParsePairsArgs args[NUMBER_OF_THREADS];
  SumPairsArgs   sumArgs[NUMBER_OF_THREADS];

  Pair           p00[PAIR_SIZE];
  Pair           p01[PAIR_SIZE];
  Pair           p02[PAIR_SIZE];
  Pair           p03[PAIR_SIZE];

  Pair           p10[PAIR_SIZE];
  Pair           p11[PAIR_SIZE];
  Pair           p12[PAIR_SIZE];
  Pair           p13[PAIR_SIZE];

  Pair           p20[PAIR_SIZE];
  Pair           p21[PAIR_SIZE];
  Pair           p22[PAIR_SIZE];
  Pair           p23[PAIR_SIZE];

  Pair           p30[PAIR_SIZE];
  Pair           p31[PAIR_SIZE];
  Pair           p32[PAIR_SIZE];
  Pair           p33[PAIR_SIZE];

  PairStack      stack0[4] = {
      {p00, 0},
      {p01, 0},
      {p02, 0},
      {p03, 0}
  };
  PairStack stack1[4] = {
      {p10, 0},
      {p11, 0},
      {p12, 0},
      {p13, 0}
  };
  PairStack stack2[4] = {
      {p20, 0},
      {p21, 0},
      {p22, 0},
      {p23, 0}
  };
  PairStack stack3[4] = {
      {p30, 0},
      {p31, 0},
      {p32, 0},
      {p33, 0}
  };

  PairStack* pairStacks[NUMBER_OF_THREADS * NUMBER_OF_THREADS] = {
      0, 0, 0, 0, //
      0, 0, 0, 0, //
      0, 0, 0, 0, //
      0, 0, 0, 0  //
  };
  u64* pairStacksU64[NUMBER_OF_THREADS * NUMBER_OF_THREADS] = {
      (u64*)&stack0[0], (u64*)&stack1[0], (u64*)&stack2[0], (u64*)&stack3[0], //
      (u64*)&stack0[1], (u64*)&stack1[1], (u64*)&stack2[1], (u64*)&stack3[1], //
      (u64*)&stack0[2], (u64*)&stack1[2], (u64*)&stack2[2], (u64*)&stack3[2], //
      (u64*)&stack0[3], (u64*)&stack1[3], (u64*)&stack2[3], (u64*)&stack3[3], //
  };

  Buffer buffers[NUMBER_OF_THREADS];
  {
    TimeBlock("Init parsing");
    for (u32 i = 0; i < NUMBER_OF_THREADS; i++)
    {
      initHashMap(&maps[i], 100);

      buffers[i] = (Buffer){
          .buffer = &fileContent[i * batchSize],
          .curr   = 0,
          .len    = batchSize,
      };

      args[i] = (ParsePairsArgs){.stack = &pairStacks[i], .stackPointers = pairStacksU64[i], .buffer = buffers[i], .done = false};
      pthread_create(&threadIds[i], NULL, parsePairs, (void*)&args[i]);

      sumArgs[i] = (SumPairsArgs){.stack = &pairStacks[i], .map = &maps[i], .done = &args[i].done};
      pthread_create(&threadIds[i + NUMBER_OF_THREADS], NULL, sumPairs, (void*)&sumArgs[i]);
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
        printf("%.*s=%.2lf/%.2lf/%.2lf\n", (i32)map0.keys[i].key.len, map0.keys[i].key.buffer, map0.values[i].min, map0.values[i].sum / (f64)map0.values[i].count, map0.values[i].max);
      }
    }
  }

  displayProfilingResult();
  return 0;
}
