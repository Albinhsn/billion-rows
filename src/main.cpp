
#define TRACY_ENABLE 1
#include "tracy/public/tracy/Tracy.hpp"
#include "tracy/public/TracyClient.cpp"

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

#define PROFILER 0

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
  volatile b32 FinishedSorting;
  u32 TotalChunkCount;
};

struct weather_entry
{
  volatile b32 Written;
  volatile b32 Done;
  u64 Key;

  s64 Sum;
  s32 Min;
  s32 Max;
  u32 Count;
};

struct parse_chunks_input
{
  weather_entry * Table; // Always 1024 * 16 sized
  chunk * Chunks;
  u32     ChunkCount;
  u32     ThreadIndex;
};


struct chunk_buffer
{
  u8 * Memory;
  u64 Offset;
  u64 Size;
  u8 * Next;
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

string_entry * GlobalStringTable; // Array of 10k
u32 GlobalStringCount = 0;
volatile flag_table * GlobalFlagTable;
weather_entry* GlobalEntryList[MAX_THREAD_COUNT - 2]; // 

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
  Assert(Buffer->Offset < Buffer->Size);
  Buffer->Offset++;
  if(Buffer->Offset == Buffer->Size)
  {
    Assert(!Buffer->ReachedEndOfBuffer);

    if(InName)
    {
      int a = 5;
    }

    Buffer->Memory = Buffer->Next;
    Buffer->Offset = 0;
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



void
PrintSortedEntryLists(u8 * Memory, u32 Size)
{
  ZoneScopedN("Print the sorted entries");
  while(!GlobalFlagTable->FinishedSorting)
  {
    // Some intrinsic here for waiting?
  }

  u32 BufferIndices[MAX_THREAD_COUNT - 2] = {};

  u32 Offset = 0;
  u8 * Start = Memory;
  for(u32 StringIndex = 0;
      StringIndex < GlobalStringCount;
      StringIndex++)
  {
    string_entry * StringEntry = GlobalStringTable + StringIndex;
    weather_entry FinalWeatherEntry = {};
    FinalWeatherEntry.Min = INT_MAX;
    FinalWeatherEntry.Max = -INT_MAX;
    for(u32 BufferIndex = 0;
        BufferIndex < ArrayCount(BufferIndices);
        BufferIndex++)
    {
      weather_entry * Buffer = GlobalEntryList[BufferIndex];
      u32 CurrentIndex       = BufferIndices[BufferIndex];

      weather_entry * Entry = Buffer + CurrentIndex;
      while(!Entry->Written && !Entry->Done)
      {
        // Intrinsic?
      }
      if(Entry->Done)
      {
        continue;
      }

      if(Entry->Key == StringEntry->Key)
      {
        FinalWeatherEntry.Count += Entry->Count;
        FinalWeatherEntry.Min = Min(FinalWeatherEntry.Min, Entry->Min);
        FinalWeatherEntry.Max = Max(FinalWeatherEntry.Max, Entry->Max);
        FinalWeatherEntry.Sum += Entry->Sum;
        BufferIndices[BufferIndex]++;
      }
    }

    f32 Mean    = FinalWeatherEntry.Sum / (10.0f * (f32)FinalWeatherEntry.Count);
    f32 Minimum = FinalWeatherEntry.Min / 10.0f;
    f32 Maximum = FinalWeatherEntry.Max / 10.0f;

    u32 Written = sprintf((char*)Memory, "%s=%.1f/%.1f/%.1f, ", StringEntry->Name, Minimum,
                      Mean, Maximum);
    Offset += Written;
    Memory += Written;
  }

  #if 0
  printf("{%.*s}\n", Offset, Start);
#else
  FILE * File;
  fopen_s(&File, "out.txt", "w");

  fprintf(File, "{%.*s}\n", Offset,Start);

#endif

}


THREAD_ENTRYPOINT(ReadFileInChunks)
{
  ZoneScopedN("Read the file in chunks");
  read_file_in_chunks_input *Input = (read_file_in_chunks_input*)RawInput;
  TimeBlock("Reading");
  {

    struct __stat64 Stat;
    _stat64(Input->Path, &Stat);
    HANDLE File = CreateFileA(Input->Path, GENERIC_READ, FILE_SHARE_READ, 0,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    u64 BytesRemaining  = Stat.st_size;
    u64 ChunkSize       = Input->ChunkSize;
    u64 ChunkMask       = Input->ChunkCount - 1;
    chunk * Chunks         = Input->Chunks;

    u32 BytesRead   = 0;
    u64 ChunkIndex  = 0;

    chunk * PrevChunk = 0;
    while(BytesRemaining)
    {
      chunk* Chunk = Chunks + ChunkIndex;
      ChunkIndex = (ChunkIndex + 1) & ChunkMask;
      Assert(Chunk->Parsed);

      u64 ToRead = BytesRemaining < ChunkSize ? BytesRemaining : ChunkSize;

      Chunk->Size = ToRead;
      if(!ReadFile(File, Chunk->Memory, ToRead, (LPDWORD)&BytesRead, 0))
      {
        u32 Error = GetLastError();
        int a = 5;
      }
      if(PrevChunk)
      {
        PrevChunk->Next   = Chunk->Memory;
        PrevChunk->Parsed = false;
        PrevChunk->WrittenTo = true;
        GlobalFlagTable->TotalChunkCount++;
      }
      PrevChunk = Chunk;
      BytesRemaining -= ToRead;
    }
    PrevChunk->Parsed = false;
    PrevChunk->WrittenTo = true;
    GlobalFlagTable->TotalChunkCount++;

    GlobalFlagTable->FinishedReading = true;

    CloseHandle(File);
  }

  #if 0
  PrintSortedEntryLists(Input->PrintMemory, Input->PrintMemorySize);
#endif

  return 0;
}



void 
ProduceSortedEntryList(weather_entry * EntryTable, u32 ThreadIndex)
{
  ZoneScopedN("Produce sorted entry list");
  while(!GlobalFlagTable->FinishedSorting)
  {
    // Some intrinsic here for waiting?
  }

  weather_entry * Buffer = GlobalEntryList[ThreadIndex];
  weather_entry * LastBuffer = Buffer + 10000;
  for(u32 StringIndex = 0;
      StringIndex < GlobalStringCount;
      StringIndex++)
  {
    string_entry * StringEntry = GlobalStringTable + StringIndex;
    
    weather_entry * WeatherEntry = LookupEntry(EntryTable, StringEntry->Key);
    if(WeatherEntry)
    {
      *Buffer = *WeatherEntry;
      Buffer->Written = true;
      Buffer++;
    }
  }
  if(Buffer < LastBuffer)
  {
    Buffer->Done = true;
  }
}


THREAD_ENTRYPOINT(ParseChunks)
{
  ZoneScopedN("Parse Chunk");
  u64 ChunkIndex = 0;
  parse_chunks_input * Input = (parse_chunks_input*)RawInput;

  {
    while(true)
    {
      chunk * Chunk = Input->Chunks + (ChunkIndex % Input->ChunkCount);

      if(!Chunk->Parsed)
      {
        if(!Chunk->Lock)
        {
          b32 Lock;
          Lock = AtomicCompareExchange(&Chunk->Lock, true, false);
          if(!Lock)
          {
            Assert(Chunk->Lock);
            b32 ReachedEndOfBuffer = false;
            chunk_buffer Buffer = {};
            Buffer.ReachedEndOfBuffer = false;
            Buffer.Memory = Chunk->Memory;
            Buffer.Size   = Chunk->Size;
            Buffer.Next   = Chunk->Next;
            if(ChunkIndex)
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

              b32 Sign = false;
              if(Current(&Buffer) == '-')
              {
                AdvanceBuffer(&Buffer, false);
                Sign = true;
              }
              s32 Number = 0;
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
            Chunk->Parsed = true;
          }
        }
      }
      if(ChunkIndex + 1 < GlobalFlagTable->TotalChunkCount)
      {
        ChunkIndex++;
      }

      if(GlobalFlagTable->FinishedReading && GlobalFlagTable->TotalChunkCount == ChunkIndex + 1)
      {
        break;
      }
    }
  }

  // ProduceSortedEntryList(Input->Table, Input->ThreadIndex);
  return 0;
}

THREAD_ENTRYPOINT(ParseStringHashes)
{
  ZoneScopedN("Parse String Hashes");
  parse_string_hashes_input * Input = (parse_string_hashes_input*)RawInput;
  arena Arena = Input->Arena;
  string_entry * Table = Input->Table;

  u64 ChunkIndex = 0;
  while(!(volatile u32)GlobalFlagTable->TotalChunkCount)
  {

  }
  while(true)
  {
    chunk * Chunk = Input->Chunks + (ChunkIndex % Input->ChunkCount);

    while(!Chunk->WrittenTo){}

    chunk_buffer Buffer = {};
    Buffer.ReachedEndOfBuffer = false;
    Buffer.Memory = Chunk->Memory;
    Buffer.Size   = Chunk->Size;
    Buffer.Next   = Chunk->Next;
    if(ChunkIndex)
    {
      while(Current(&Buffer) != '\n')
      {
        AdvanceBuffer(&Buffer, false);
      }
      AdvanceBuffer(&Buffer, false);
    }


    while(!Buffer.ReachedEndOfBuffer)
    {
      // Parse name
      u64 Key = 0;
      u32 Offset = Arena.Offset;
      u32 StringLength = 0;
      u8 * Start = Arena.Memory + Offset;
      u8 * Curr  = Start;
      while(Current(&Buffer) != ';')
      {
        Key ^= Current(&Buffer);
        Key *= 16777619;
        *Curr++ = Current(&Buffer);
        Offset++;
        Assert(Offset < Arena.Size);
        StringLength++;

        AdvanceBuffer(&Buffer, true);
      }

      if(!StringAlreadyExists(Table, Key))
      {
        *Curr = '\0';
        Arena.Offset = Offset + 1;
        Assert(Arena.Offset < Arena.Size);
        Assert(GlobalStringCount < 10000);

        string_entry * Entry = GlobalStringTable + GlobalStringCount++;
        Entry->Key  = Key;
        Entry->Name = Start;
      }

      // Skip the ';'
      AdvanceBuffer(&Buffer, false);

      if(Current(&Buffer) == '-')
      {
        AdvanceBuffer(&Buffer, false);
      }

      while(Current(&Buffer) != '\n')
      {
        AdvanceBuffer(&Buffer, false);
      }
      AdvanceBuffer(&Buffer, false);
    }

    if(ChunkIndex + 1 < GlobalFlagTable->TotalChunkCount)
    {
      ChunkIndex++;
    }

    if(GlobalFlagTable->FinishedReading && GlobalFlagTable->TotalChunkCount == ChunkIndex + 1)
    {
      break;
    }
  }

  qsort(GlobalStringTable, GlobalStringCount, sizeof(string_entry), StringCompare);
  GlobalFlagTable->FinishedSorting = true;

  return 0;
}

int
main(int ArgCount, char** Args)
{
  if(ArgCount > 1)
  {
    FrameMark;
    #if 1
    BeginProfile();
    #endif

    flag_table Table = {};
    GlobalFlagTable = &Table;

    GlobalStringTable = AllocateStruct(10000, string_entry);
    string_entry * StringTable = AllocateStruct(1024 * 16, string_entry);
    for(u32 Index = 0;
        Index < ArrayCount(GlobalEntryList);
        Index++)
    {
      GlobalEntryList[Index] = AllocateStruct(10000, weather_entry);
    }

    u32 ChunkCount = 1;
    chunk * Chunks = AllocateStruct(ChunkCount, chunk);

    read_file_in_chunks_input Input = {};
    Input.Path        = Args[1];
    Input.ChunkSize   = Gigabyte(18);
    Input.ChunkCount  = ChunkCount;
    Input.Chunks      = Chunks;
    Input.PrintMemorySize = Megabyte(1);
    Input.PrintMemory    = (u8*)Allocate(Input.PrintMemorySize);
    for(u32 ChunkIndex = 0;
        ChunkIndex < ChunkCount;
        ChunkIndex++)
    {
      Input.Chunks[ChunkIndex].Memory = AllocateStruct(Input.ChunkSize, u8);
      Input.Chunks[ChunkIndex].Parsed = true;
    }

    HANDLE ThreadHandles[MAX_THREAD_COUNT - 1];

    ThreadHandles[0] = CreateThread(0, 0, ReadFileInChunks, &Input, 0, 0);

    #if 0
    parse_chunks_input ParseInputs[MAX_THREAD_COUNT - 2] = {};
    for(u32 ParseThreadIndex = 0;
        ParseThreadIndex < MAX_THREAD_COUNT - 2;
        ParseThreadIndex++)
    {
      parse_chunks_input * ParseInput = ParseInputs + ParseThreadIndex;

      ParseInput->Table      = AllocateStruct(1024 * 16, weather_entry);
      ParseInput->Chunks     = Chunks;
      ParseInput->ChunkCount = ChunkCount;
      ParseInput->ThreadIndex = ParseThreadIndex;

      ThreadHandles[1 + ParseThreadIndex] = CreateThread(0, 0, ParseChunks, ParseInput, 0, 0);
    }
    #endif

    parse_string_hashes_input HashInput = {};
    HashInput.Arena.Size = Megabyte(2);
    HashInput.Arena.Memory = (u8*)Allocate(HashInput.Arena.Size);
    HashInput.Chunks = Chunks;
    HashInput.ChunkCount = ChunkCount;
    HashInput.Table = StringTable;

    #if 0
    ThreadHandles[MAX_THREAD_COUNT - 2] = CreateThread(0, 0, ParseStringHashes, &HashInput, 0, 0);
    #endif

    

    #if 0
    for(u32 ThreadIndex = 0;
        ThreadIndex < MAX_THREAD_COUNT - 2;
        ThreadIndex++)
    {
      WaitForSingleObject(ThreadHandles[ThreadIndex], INFINITE);
    }
    #endif
    WaitForSingleObject(ThreadHandles[0], INFINITE);

    #if 1
    struct __stat64 Stat;
    _stat64(Args[1], &Stat);
    printf("---\n");
    printf("Total strings: %u\n", GlobalStringCount);
    EndAndPrintProfile(Stat.st_size, __COUNTER__ + 1);
    #endif


  }
  else
  {
    OutputDebugStringA("No file given!\n");
  }

}
