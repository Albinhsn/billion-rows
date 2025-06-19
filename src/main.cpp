
#if 0
#define TRACY_ENABLE 0
#include "tracy/public/tracy/Tracy.hpp"
#include "tracy/public/TracyClient.cpp"
#else

#define ZoneScopedN(Name)
#define FrameMark
#endif

#include <windows.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <psapi.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef float f32;
typedef double f64;

typedef bool b32;

#define Kilobyte(B) (1024LL * B)
#define Megabyte(B) (Kilobyte(B) * 1024LL)
#define Gigabyte(B) (Megabyte(B) * 1024LL)
#define ArrayCount(Array) (sizeof(Array) / sizeof(Array[0]))

#define PROFILER 1

#define Assert(Expr)\
  if(!(Expr))\
    *(int*)0 = 5;

#include "repetition.cpp"
#include "profiler.h"
#include "profiler.cpp"

#define THREAD_ENTRYPOINT(Name) DWORD Name(void * RawInput)
typedef THREAD_ENTRYPOINT(thread_entrypoint);

struct arena
{
  u8 * Memory;
  u64  Size;
  u64  Offset;
};

struct chunk
{
  u8 * Memory;
  u8 * Next;
  u64           Size;

  volatile u32  Lock;
  volatile u32  WrittenTo;
  b32           Parsed;
};

struct read_file_in_chunks_input
{
  chunk * Chunks;
  char * Path;
  u8* PrintMemory;
  u64 ChunkCount;
  u64 ChunkSize;
  u32 PrintMemorySize;
};

struct flag_table
{
  volatile b32 FinishedReading;
  u32 TotalChunkCount;
};

struct weather_entry
{
  u64 Key;
  char * Name;
  volatile b32 Written;
  volatile b32 Done;

  s64 Sum;
  s32 Min;
  s32 Max;
  u32 Count;
};

struct parse_chunks_input
{
  weather_entry * Table; // Always 1024 * 16 sized
  u8 * FileMemory;
  u64 MemorySize;
  u32 ThreadIndex;
};


struct chunk_buffer
{
  u8 * Memory;
  u64 Offset;
  u64 Size;
  b32 ReachedEndOfBuffer;
};
struct string_entry
{
  u64 Key;
  u8* Name;
};


struct parse_string_hashes_input
{
  chunk* Chunks;
  string_entry* Table;
  arena Arena;
  u32 ChunkCount;
};

#define MAX_THREAD_COUNT 16

volatile flag_table * GlobalFlagTable;
weather_entry * GlobalEntryList[MAX_THREAD_COUNT];

inline void
Deallocate(void * Memory)
{
  VirtualFree(Memory, 0, MEM_RELEASE);
}

inline void *
Allocate(u64 Size)
{
  return VirtualAlloc(0, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
#define AllocateStruct(Size, Type) (Type*)Allocate(Size * sizeof(Type))

inline u8 
Current(chunk_buffer * Buffer)
{
  return *(Buffer->Memory + Buffer->Offset);
}

inline void 
AdvanceBuffer(chunk_buffer * Buffer, b32 InName)
{
  Buffer->Offset++;
  if(Buffer->Offset == Buffer->Size)
  {
    Assert(!Buffer->ReachedEndOfBuffer);
    Buffer->ReachedEndOfBuffer = true;
  }
}


inline s32
Min(s32 a, s32 b)
{
  return a ^ ((b ^ a) & -(b < a));
}

inline s32
Max(s32 a, s32 b)
{
  return a ^ ((b ^ a) & -(b > a));
}

inline weather_entry * 
LookupEntry(weather_entry * Table, u64 Key)
{
  u32 Mask = (1024 * 16) - 1;
  u32 Index = Key & Mask;

  u32 Start = Index;
  while(true)
  {
    weather_entry * Entry = Table + Index;
    if(Entry->Key == Key)
    {
      return Entry;
    }
    if(Entry->Key == 0)
    {
      return 0;
    }
    Index = (Index + 1) & Mask;
    if(Index == Start)
    {
      return 0;
    }
  }
}

inline weather_entry * 
LookupOrAddEntry(weather_entry * Table, u64 Key)
{
  u32 Mask = (1024 * 16) - 1;
  u32 Index = Key & Mask;

  while(true)
  {
    weather_entry * Entry = Table + Index;
    if(Entry->Key == Key)
    {
      return Entry;
    }
    if(Entry->Key == 0)
    {
      Entry->Key = Key;
      Entry->Min = INT_MAX;
      Entry->Max = -INT_MAX;
      return Entry;
    }
    Index = (Index + 1) & Mask;
  }
}

inline u32
AtomicCompareExchange(volatile u32* Dest, u32 Exchange, u32 Compare)
{
  u32 Result = InterlockedCompareExchange(Dest, Exchange, Compare);
  return Result;
}

inline b32
StringAlreadyExists(string_entry * Table, u64 Key)
{
  u32 Mask = (1024 * 16) - 1;
  u32 Index = Key & Mask;

  while(true)
  {
    string_entry * Entry = Table + Index;
    if(Entry->Key == Key)
    {
      return true;
    }
    if(Entry->Key == 0)
    {
      Entry->Key = Key;
      return false;
    }
    Index = (Index + 1) & Mask;
  }
}

inline s32
StringCompare(const void * A_, const void * B_)
{
  string_entry * A = (string_entry*)A_;
  string_entry * B = (string_entry*)B_;

  return strcmp((const char*)A->Name, (const char*)B->Name);
};


THREAD_ENTRYPOINT(ParseChunks)
{
  ZoneScopedN("Parse Chunk");
  parse_chunks_input * Input = (parse_chunks_input*)RawInput;

  b32 ReachedEndOfBuffer = false;
  chunk_buffer Buffer = {};
  Buffer.ReachedEndOfBuffer = false;
  Buffer.Memory = Input->FileMemory;
  Buffer.Size   = Input->MemorySize;
  if(Input->ThreadIndex != 0)
  {
    while(Current(&Buffer) != '\n')
    {
      AdvanceBuffer(&Buffer, false);
    }
    AdvanceBuffer(&Buffer, false);
  }


  u32 ParsedCount = 0;
  while(!Buffer.ReachedEndOfBuffer)
  {
    // Parse name
    u64 Key = 0;
    while(Current(&Buffer) != ';')
    {
      Key ^= Current(&Buffer);
      Key *= 16777619;
      AdvanceBuffer(&Buffer, false);
    }

    // Skip the ';'
    AdvanceBuffer(&Buffer, false);

    b32 Sign = Current(&Buffer) == '-';
    if(Sign)
    {
      AdvanceBuffer(&Buffer, false);
    }
    s32 Number = 0;
    
    // Read 8 u8s in unaligned
    // Subtract '0' from each
    // Create a mask if they're < 9
    // Some sort of table lookup of what to multiply with 
    // Multiply and horizontal sum
    while(Current(&Buffer) != '\r')
    {
      u8 Digit = Current(&Buffer) - '0';
      AdvanceBuffer(&Buffer, false);
      Number *= 10;
      if(Digit <= 9)
      {
        Number += Digit;
      }
    }
    Number = Sign ? -Number : Number;

    AdvanceBuffer(&Buffer, false);
    AdvanceBuffer(&Buffer, false);

    weather_entry * Entry = LookupOrAddEntry(Input->Table, Key);

    Entry->Min = Min(Number, Entry->Min);
    Entry->Max = Max(Number, Entry->Max);
    Entry->Sum += Number;
    Entry->Count++;
  }

  return 0;
}


int
main(int ArgCount, char** Args)
{
  if(ArgCount > 1)
  {
    FrameMark;
    BeginProfile();

    HANDLE ThreadHandles[MAX_THREAD_COUNT];

    struct __stat64 Stat;
    _stat64(Args[1], &Stat);

    u64 FileSize = Stat.st_size;
    u8 * FileMemory = 0;
    {
      TimeBlock("Read");
      HANDLE File = CreateFileA(Args[1], GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, 0,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
      HANDLE Mapping = CreateFileMappingA(File, 0, PAGE_READONLY, 0, 0, 0);
      FileMemory = (u8*)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
    }

    u64 MemoryPerThread = FileSize / MAX_THREAD_COUNT;

    parse_chunks_input ParseInputs[MAX_THREAD_COUNT] = {};
    {
      TimeBlock("Parsing");

      for(u64 ParseThreadIndex = 0;
          ParseThreadIndex < MAX_THREAD_COUNT;
          ParseThreadIndex++)
      {
        parse_chunks_input * ParseInput = ParseInputs + ParseThreadIndex;

        ParseInput->Table       = AllocateStruct(1024 * 16, weather_entry);
        ParseInput->ThreadIndex = (u32)ParseThreadIndex;
        ParseInput->FileMemory  = FileMemory + (ParseThreadIndex * MemoryPerThread);
        ParseInput->MemorySize  = MemoryPerThread;
        if(ParseThreadIndex == MAX_THREAD_COUNT - 1)
        {
          ParseInput->MemorySize += FileSize % MAX_THREAD_COUNT;
        }

        ThreadHandles[ParseThreadIndex] = CreateThread(0, 0, ParseChunks, ParseInput, 0, 0);
      }

      for(u32 ParseThreadIndex = 0;
          ParseThreadIndex < MAX_THREAD_COUNT;
          ParseThreadIndex++)
      {
        WaitForSingleObject(ThreadHandles[ParseThreadIndex], INFINITE);
      }
    }


    printf("---\n");
    EndAndPrintProfile(Stat.st_size, __COUNTER__ + 1);


  }
  else
  {
    OutputDebugStringA("No file given!\n");
  }

}
