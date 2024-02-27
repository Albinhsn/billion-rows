#include "common.h"
#include "string.h"
#include "table.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <pthread.h>

struct Pair
{
  String key;
  Value  value;
};

struct PairStack
{
  Pair* pairs;
  u32   ptr;
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
bool isDone;

#define PAIR_STACK_SIZE  4096
#define SUM_PAIR_THREADS 5
#define PAIR_AMOUNT      20
#define STACK_PER_THREAD PAIR_AMOUNT / SUM_PAIR_THREADS

struct SumPairArgs
{
  HashMap*    map;
  PairStack** pairStack;
};

void* sumPairs(void* args_)
{
  SumPairArgs* args  = (SumPairArgs*)args_;
  PairStack**  stack = args->pairStack;
  HashMap*     map   = args->map;

  while (!isDone)
  {
    for (i32 i = 0; i < STACK_PER_THREAD; i++)
    {
      if (stack[i] != 0)
      {
        printf("found one for %ld\n", (u64)map);
        Pair* s = stack[i]->pairs;
        for (u32 j = 0; j < stack[i]->ptr; j++)
        {
          updateHashMap(map, s[j].key, s[j].value);
        }
        stack[i]->ptr = 0;
        stack[i]      = 0;
      }
    }
  }

  for (i32 i = 0; i < STACK_PER_THREAD; i++)
  {
    if (stack[i] != 0)
    {
      printf("found one for %ld\n", (u64)map);
      Pair* s = stack[i]->pairs;
      for (u32 j = 0; j < stack[i]->ptr; j++)
      {
        updateHashMap(map, s[j].key, s[j].value);
      }
      stack[i]->ptr = 0;
      stack[i]      = 0;
    }
  }
  printf("Summed a thread\n");

  return 0;
}

struct ParsePairStackArgs
{
  PairStack** pairStack;
  u64**       pairStackPointers;
};

void parsePairStack(PairStack* stack, Buffer* buffer)
{
  String key;
  Value  value;
  while (buffer->curr < buffer->len && stack->ptr < PAIR_STACK_SIZE)
  {
    u64 start  = buffer->curr;
    key.buffer = &buffer->buffer[start];
    while (!isOutOfBounds(buffer) && getCurrentCharBuffer(buffer) != ';')
    {
      advanceBuffer(buffer);
    }
    if (isOutOfBounds(buffer))
    {
      printf("returned after %d\n", stack->ptr);
      return;
    }
    key.len = buffer->curr - start;

    advanceBuffer(buffer);
    if (isOutOfBounds(buffer))
    {
      printf("returned after %d\n", stack->ptr);
      return;
    }

    value.sum   = parseNumber(buffer);
    value.count = 1;
    value.max   = value.sum;
    value.min   = value.sum;
    advanceBuffer(buffer);

    stack->pairs[stack->ptr] = (Pair){.key = key, .value = value};
    stack->ptr++;
  }
}

void* parsePairs(void* args)
{
  Buffer              buffer;
  ParsePairStackArgs* arg                = (ParsePairStackArgs*)args;
  PairStack**         pairStacks         = arg->pairStack;
  u64**               pairStacksPointers = arg->pairStackPointers;
  u8*                 memory;
  u64                 count;
  FILE*               filePtr;
  u64                 fileSize;

  u64                 totalBytesRead = 0;
  u64                 stackSize      = ((u64)(100));

  const char*         fileName       = "./measurements10000.txt";
  filePtr                            = fopen(fileName, "rb");
  if (!filePtr)
  {
    printf("Failed to open file '%s'\n", fileName);
    exit(1);
  }

  fseek(filePtr, 0, SEEK_END);
  fileSize = ftell(filePtr);
  fseek(filePtr, 0, SEEK_SET);

  while (totalBytesRead < fileSize)
  {
    memory = (u8*)malloc(sizeof(u8) * stackSize);
    count  = fread(memory, 1, stackSize, filePtr);
    totalBytesRead += count;

    buffer = (Buffer){.buffer = (u8*)memory, .curr = 0, .len = count};

    while (buffer.curr < buffer.len)
    {
      for (i32 i = 0; i < PAIR_AMOUNT; i++)
      {
        if (pairStacks[i] == 0)
        {
          pairStacks[i] = (PairStack*)pairStacksPointers[i];
          parsePairStack(pairStacks[i], &buffer);
          break;
        }
      }
    }
    if(totalBytesRead > 100){
      break;
    }
  }

  printf("Read everything %ld\n", totalBytesRead);
  isDone = true;

  return 0;
}

int main()
{
  isDone = false;

  initProfiler();

  u32        cap = PAIR_STACK_SIZE;
  Pair       pairs00[cap], pairs01[cap], pairs02[cap], pairs03[cap];
  Pair       pairs10[cap], pairs11[cap], pairs12[cap], pairs13[cap];
  Pair       pairs20[cap], pairs21[cap], pairs22[cap], pairs23[cap];
  Pair       pairs30[cap], pairs31[cap], pairs32[cap], pairs33[cap];
  Pair       pairs40[cap], pairs41[cap], pairs42[cap], pairs43[cap];

  PairStack  stack00 = (PairStack){.pairs = pairs00, .ptr = 0};
  PairStack  stack01 = (PairStack){.pairs = pairs01, .ptr = 0};
  PairStack  stack02 = (PairStack){.pairs = pairs02, .ptr = 0};
  PairStack  stack03 = (PairStack){.pairs = pairs03, .ptr = 0};

  PairStack  stack10 = (PairStack){.pairs = pairs10, .ptr = 0};
  PairStack  stack11 = (PairStack){.pairs = pairs11, .ptr = 0};
  PairStack  stack12 = (PairStack){.pairs = pairs12, .ptr = 0};
  PairStack  stack13 = (PairStack){.pairs = pairs13, .ptr = 0};

  PairStack  stack20 = (PairStack){.pairs = pairs20, .ptr = 0};
  PairStack  stack21 = (PairStack){.pairs = pairs21, .ptr = 0};
  PairStack  stack22 = (PairStack){.pairs = pairs22, .ptr = 0};
  PairStack  stack23 = (PairStack){.pairs = pairs23, .ptr = 0};

  PairStack  stack30 = (PairStack){.pairs = pairs30, .ptr = 0};
  PairStack  stack31 = (PairStack){.pairs = pairs31, .ptr = 0};
  PairStack  stack32 = (PairStack){.pairs = pairs32, .ptr = 0};
  PairStack  stack33 = (PairStack){.pairs = pairs33, .ptr = 0};

  PairStack  stack40 = (PairStack){.pairs = pairs40, .ptr = 0};
  PairStack  stack41 = (PairStack){.pairs = pairs41, .ptr = 0};
  PairStack  stack42 = (PairStack){.pairs = pairs42, .ptr = 0};
  PairStack  stack43 = (PairStack){.pairs = pairs43, .ptr = 0};

  PairStack* stack[PAIR_AMOUNT];
  for (i32 i = 0; i < PAIR_AMOUNT; i++)
  {
    stack[i] = 0;
  }

  u64* stackPointers[PAIR_AMOUNT] = {
      (u64*)&stack00, (u64*)&stack10, (u64*)&stack20, (u64*)&stack30, (u64*)&stack40, //
      (u64*)&stack01, (u64*)&stack11, (u64*)&stack21, (u64*)&stack31, (u64*)&stack41, //
      (u64*)&stack02, (u64*)&stack12, (u64*)&stack22, (u64*)&stack32, (u64*)&stack42, //
      (u64*)&stack03, (u64*)&stack13, (u64*)&stack23, (u64*)&stack33, (u64*)&stack43, //
  };
  ParsePairStackArgs args            = (ParsePairStackArgs){.pairStack = stack, .pairStackPointers = stackPointers};

  u32                numberOfThreads = SUM_PAIR_THREADS + 1;
  pthread_t          threadIds[numberOfThreads];
  pthread_create(&threadIds[0], NULL, parsePairs, (void*)&args);

  HashMap     maps[SUM_PAIR_THREADS];
  SumPairArgs sumPairArgs[SUM_PAIR_THREADS];
  for (u32 i = 0; i < SUM_PAIR_THREADS; i++)
  {
    initHashMap(&maps[i], 1000);
    sumPairArgs[i] = (SumPairArgs){.map = &maps[i], .pairStack = &stack[i * (STACK_PER_THREAD)]};
    pthread_create(&threadIds[i + 1], NULL, sumPairs, (void*)&sumPairArgs[i]);
  }

  for (u32 i = 0; i < numberOfThreads; i++)
  {
    if (pthread_join(threadIds[i], NULL) != 0)
    {
      printf("Failed something with thread?\n");
    }
  }

  HashMap map0 = maps[0];
  for (u64 i = 0; i < map0.cap; i++)
  {
    if (map0.values[i].count != 0)
    {
      for (u64 j = 1; j < SUM_PAIR_THREADS; j++)
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

  displayProfilingResult();
  return 0;
}
