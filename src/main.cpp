#include "common.h"
#include "string.h"
#include "table.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <pthread.h>

bool isDone;
bool readingIsDone;

#define PAIR_STACK_SIZE  4096
#define SUM_PAIR_THREADS 3
#define PAIR_AMOUNT      12
#define STACK_PER_THREAD PAIR_AMOUNT / SUM_PAIR_THREADS
#define STACK_SIZE       1024 * 1024 * 1024

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
  Buffer** buffers;
};

u64 parsePairStack(PairStack* stack, Buffer* buffer)
{
  String key;
  Value  value;
  while (buffer->curr < buffer->len && stack->ptr < PAIR_STACK_SIZE)
  {
    u64 start = buffer->curr;
    while (!isOutOfBounds(buffer) && getCurrentCharBuffer(buffer) != ';')
    {
      advanceBuffer(buffer);
    }
    if (isOutOfBounds(buffer))
    {
      return buffer->len - start;
    }
    key.buffer = &buffer->buffer[start];
    key.len    = buffer->curr - start;

    advanceBuffer(buffer);
    if (isOutOfBounds(buffer))
    {
      return buffer->len - start;
    }

    value.sum   = parseNumber(buffer);
    value.count = 1;
    value.max   = value.sum;
    value.min   = value.sum;
    advanceBuffer(buffer);

    stack->pairs[stack->ptr] = (Pair){.key = key, .value = value};
    stack->ptr++;
  }
  return 0;
}

#define NUMBER_OF_BUFFERS 4

struct ReadPairsArgs
{
  PairStack** pairStack;
  u64**       pairStackPointers;
  Buffer**    buffers;
};

void* readPairs(void* args)
{
  Buffer*        buffer;
  Buffer*        prevBuffer;
  ReadPairsArgs* arg                = (ReadPairsArgs*)args;
  PairStack**    pairStacks         = arg->pairStack;
  u64**          pairStacksPointers = arg->pairStackPointers;
  Buffer**       buffers            = arg->buffers;
  u64            start              = 0;

  while (!readingIsDone)
  {
    for (u32 i = 0; i < NUMBER_OF_BUFFERS; i++)
    {
      if (buffers[i] != 0)
      {
        buffer = buffers[i];
        if (start != 0)
        {
          u64 newLen    = start + buffers[i]->len;
          u8* newBuffer = (u8*)malloc(sizeof(u8) * (newLen));
          memcpy(newBuffer, &prevBuffer->buffer[prevBuffer->len - start], start);
          memcpy(&newBuffer[start], buffers[i]->buffer, buffers[i]->len);
          buffer->len    = newLen;
          buffer->buffer = newBuffer;
        }

        while (buffer->curr < buffer->len)
        {
          for (i32 i = 0; i < PAIR_AMOUNT; i++)
          {
            if (pairStacks[i] == 0)
            {
              pairStacks[i] = (PairStack*)pairStacksPointers[i];
              start         = parsePairStack(pairStacks[i], buffer);
              prevBuffer    = buffer;
              break;
            }
          }
        }

        buffers[i] = 0;
      }
    }
  }

  isDone = true;

  return 0;
}

void* parsePairs(void* args)
{
  ParsePairStackArgs* arg     = (ParsePairStackArgs*)args;
  Buffer**            buffers = arg->buffers;
  Buffer*             buffer;
  u8*                 memory;
  u64                 count;
  FILE*               filePtr;
  u64                 fileSize;

  u64                 totalBytesRead = 0;

  const char*         fileName       = "./measurements1b.txt";
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

    memory = (u8*)malloc(sizeof(u8) * STACK_SIZE);
    count  = fread(memory, 1, STACK_SIZE, filePtr);
    totalBytesRead += count;

    buffer         = (Buffer*)malloc(sizeof(Buffer));
    buffer->buffer = memory;
    buffer->curr   = 0;
    buffer->len    = count;

    while (buffer != 0)
    {
      for (u32 i = 0; i < NUMBER_OF_BUFFERS; i++)
      {
        if (buffers[i] == 0)
        {
          buffers[i] = buffer;
          buffer     = 0;
        }
      }
    }
    printf("Read %ld out of %ld, %lf\n", totalBytesRead, fileSize, totalBytesRead / (f64)fileSize);
  }

  printf("Read everything %ld\n", totalBytesRead);
  readingIsDone = true;

  return 0;
}

int main()
{
  isDone        = false;
  readingIsDone = false;

  initProfiler();

  u32        cap = PAIR_STACK_SIZE;
  Pair       pairs00[cap], pairs01[cap], pairs02[cap], pairs03[cap];
  Pair       pairs10[cap], pairs11[cap], pairs12[cap], pairs13[cap];
  Pair       pairs20[cap], pairs21[cap], pairs22[cap], pairs23[cap];

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

  PairStack* stack[PAIR_AMOUNT];
  for (i32 i = 0; i < PAIR_AMOUNT; i++)
  {
    stack[i] = 0;
  }

  u64* stackPointers[PAIR_AMOUNT] = {
      (u64*)&stack00, (u64*)&stack10, (u64*)&stack20, //
      (u64*)&stack01, (u64*)&stack11, (u64*)&stack21, //
      (u64*)&stack02, (u64*)&stack12, (u64*)&stack22, //
      (u64*)&stack03, (u64*)&stack13, (u64*)&stack23, //
  };
  Buffer*            buffers[NUMBER_OF_BUFFERS] = {0, 0, 0, 0};

  ParsePairStackArgs args                       = (ParsePairStackArgs){.buffers = buffers};

  u32                numberOfThreads            = SUM_PAIR_THREADS + 2;
  pthread_t          threadIds[numberOfThreads];
  pthread_create(&threadIds[0], NULL, parsePairs, (void*)&args);

  ReadPairsArgs readArgs = (ReadPairsArgs){.pairStack = stack, .pairStackPointers = stackPointers, .buffers = buffers};
  pthread_create(&threadIds[1], NULL, readPairs, (void*)&readArgs);

  HashMap     maps[SUM_PAIR_THREADS];
  SumPairArgs sumPairArgs[SUM_PAIR_THREADS];
  for (u32 i = 0; i < SUM_PAIR_THREADS; i++)
  {
    initHashMap(&maps[i], 1000);
    sumPairArgs[i] = (SumPairArgs){.map = &maps[i], .pairStack = &stack[i * (STACK_PER_THREAD)]};
    pthread_create(&threadIds[i + 2], NULL, sumPairs, (void*)&sumPairArgs[i]);
  }

  for (u32 i = 0; i < numberOfThreads; i++)
  {
    if (pthread_join(threadIds[i], NULL) != 0)
    {
      printf("Failed something with thread?\n");
    }
  }

  {
    TimeBlock("Last Gather");
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
  }

  displayProfilingResult();
  return 0;
}
