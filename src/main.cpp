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

#define Assert(Expr)\
  if(!(Expr))\
    *(int*)0 = 5;

#include "repetition.cpp"

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
  b32           Done;
};

struct read_file_in_chunks_input
{
  chunk * Chunks;
  char * Path;
  u64 ChunkCount;
  u64 ChunkSize;
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
};


struct chunk_buffer
{
  u8 * Memory;
  u8 * End;
  u8 ** Next;
  b32 ReachedEndOfBuffer;
};
struct string_entry
{
  u64 Key;
  u8* Name;
};


struct parse_string_hashes_input
{
  arena Arena;
  chunk * Chunks;
  u32 ChunkCount;
};

#define MAX_THREAD_COUNT 3

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

inline void 
AdvanceBuffer(chunk_buffer * Buffer)
{
  Assert(Buffer->Memory < Buffer->End);
  Buffer->Memory++;
  if(Buffer->Memory == Buffer->End)
  {
    Assert(!Buffer->ReachedEndOfBuffer);

    Buffer->Memory = *Buffer->Next;
    Buffer->ReachedEndOfBuffer = true;
  }
}

inline u64
Hash(u32 a)
{
  u64 h = a;
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53L;
  h ^= h >> 33;
  return h;
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
StringAlreadyExists(u64 Key)
{
  b32 Result = false;
  for(u32 Index = 0;
      Index < GlobalStringCount;
      Index++)
  {
    string_entry * Entry = GlobalStringTable + Index;
    if(Entry->Key == Key)
    {
      Result = true;
      break;
    }
  }

  return Result;
}

s32 StringCompare(const void * A_, const void * B_)
{
  string_entry * A = (string_entry*)A_;
  string_entry * B = (string_entry*)B_;

  return strcmp((const char*)A->Name, (const char*)B->Name);
};



void
PrintSortedEntryLists(u8 * Memory, u32 Size)
{
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
      while(!Entry->Written)
      {
        // Intrinsic?
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

  printf("{%.*s}\n", Offset, Start);

}


THREAD_ENTRYPOINT(ReadFileInChunks)
{
  read_file_in_chunks_input *Input = (read_file_in_chunks_input*)RawInput;

  struct __stat64 Stat;
  _stat64(Input->Path, &Stat);
  HANDLE File = CreateFileA(Input->Path, GENERIC_READ, FILE_SHARE_READ, 0,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

  u64 BytesRemaining  = Stat.st_size;
  u64 ChunkSize       = Input->ChunkSize;
  u64 ChunkMask       = Input->ChunkCount - 1;
  ChunkMask = 1; // ToDO remove!!
  chunk * Chunks         = Input->Chunks;

  u32 BytesRead   = 0;
  u64 ChunkIndex  = 0;

  b32 FirstLap = true;

  while(BytesRemaining)
  {
    chunk* Chunk = Chunks + (ChunkIndex * ChunkSize);
    ChunkIndex = (ChunkIndex + 1) & ChunkMask;
    FirstLap &= ChunkIndex == 0 ? false : true;
    Assert(Chunk->Done || FirstLap);

    u64 ToRead = BytesRemaining < ChunkSize ? BytesRemaining : ChunkSize;

    Chunk->Size = ToRead;
    if(!ReadFile(File, Chunk->Memory, ToRead, (LPDWORD)&BytesRead, 0))
    {
      u32 Error = GetLastError();
      int a = 5;
    }
    BytesRemaining -= ToRead;
    GlobalFlagTable->TotalChunkCount++;
    Chunk->Done = false;
  }

  GlobalFlagTable->FinishedReading = true;

  CloseHandle(File);


  return 0;
}



void 
ProduceSortedEntryList(weather_entry * EntryTable)
{
  while(!GlobalFlagTable->FinishedSorting)
  {
    // Some intrinsic here for waiting?
  }

  weather_entry * Buffer = GlobalEntryList[0];
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
}


THREAD_ENTRYPOINT(ParseChunks)
{
  u64 ChunkIndex = 0;
  parse_chunks_input * Input = (parse_chunks_input*)RawInput;

  while(true)
  {
    chunk * Chunk = Input->Chunks + (ChunkIndex % Input->ChunkCount);

    if(!Chunk->Done)
    {
      if(!Chunk->Lock)
      {
        b32 Lock;
        Lock = AtomicCompareExchange(&Chunk->Lock, true, false);
        if(!Lock)
        {
          Assert(Chunk->Lock);
          if(ChunkIndex)
          {
            while(*Chunk->Memory != '\n')
            {
              Chunk->Memory++;
            }
            Chunk->Memory++;
          }

          u8 * End = Chunk->Memory + Chunk->Size;
          b32 ReachedEndOfBuffer = false;
          chunk_buffer Buffer = {};
          Buffer.ReachedEndOfBuffer = false;
          Buffer.Memory = Chunk->Memory;
          Buffer.End    = Chunk->Memory + Chunk->Size;
          Buffer.Next   = &Chunk->Next;

          while(!Buffer.ReachedEndOfBuffer)
          {
            // Parse name
            u64 Key = 0;
            while(*Buffer.Memory != ';')
            {
              Key += Hash(*Buffer.Memory);
              AdvanceBuffer(&Buffer);
            }

            // Skip the ';'
            AdvanceBuffer(&Buffer);

            b32 Sign = false;
            if(*Buffer.Memory == '-')
            {
              AdvanceBuffer(&Buffer);
              Sign = true;
            }
            s32 Number = 0;
            while(*Buffer.Memory != '\r')
            {
              u8 Digit = *Buffer.Memory - '0';
              AdvanceBuffer(&Buffer);
              Number *= 10;
              if(Digit <= 9)
              {
                Number += Digit;
              }
            }
            Number = Sign ? -Number : Number;

            AdvanceBuffer(&Buffer);
            AdvanceBuffer(&Buffer);
            weather_entry * Entry = LookupOrAddEntry(Input->Table, Key);

            Entry->Min = Min(Number, Entry->Min);
            Entry->Max = Max(Number, Entry->Max);
            Entry->Sum += Number;
            Entry->Count++;
          }
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


  return 0;
}

THREAD_ENTRYPOINT(ParseStringHashes)
{
  parse_string_hashes_input * Input = (parse_string_hashes_input*)RawInput;
  arena Arena = Input->Arena;

  u64 ChunkIndex = 0;
  while(true)
  {
    chunk * Chunk = Input->Chunks + (ChunkIndex % Input->ChunkCount);

    if(ChunkIndex)
    {
      while(*Chunk->Memory != '\n')
      {
        Chunk->Memory++;
      }
      Chunk->Memory++;
    }

    chunk_buffer Buffer = {};
    Buffer.ReachedEndOfBuffer = false;
    Buffer.Memory = Chunk->Memory;
    Buffer.End    = Chunk->Memory + Chunk->Size;
    Buffer.Next   = &Chunk->Next;

    while(!Buffer.ReachedEndOfBuffer)
    {
      // Parse name
      u64 Key = 0;
      u32 Offset = Arena.Offset;
      u8 * Start = Arena.Memory + Offset;
      u8 * Curr  = Start;
      while(*Buffer.Memory != ';')
      {
        Key    += Hash(*Buffer.Memory);
        *Curr++ = *Buffer.Memory;
        Offset++;
        Assert(Offset < Arena.Size);

        AdvanceBuffer(&Buffer);
      }

      if(!StringAlreadyExists(Key))
      {
        *Curr = '\0';
        Arena.Offset = Offset + 1;
        Assert(Arena.Offset < Arena.Size);

        string_entry * Entry = GlobalStringTable + GlobalStringCount++;
        Entry->Key  = Key;
        Entry->Name = Start;
      }

      // Skip the ';'
      AdvanceBuffer(&Buffer);

      if(*Buffer.Memory == '-')
      {
        AdvanceBuffer(&Buffer);
      }

      while(*Buffer.Memory != '\n')
      {
        AdvanceBuffer(&Buffer);
      }
      AdvanceBuffer(&Buffer);
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
    u64 CPUFreq = EstimateCPUTimerFreq();

    u64 Start = ReadCPUTimer();
    flag_table Table = {};
    GlobalFlagTable = &Table;

    GlobalStringTable = AllocateStruct(10000, string_entry);
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
    for(u32 ChunkIndex = 0;
        ChunkIndex < ChunkCount;
        ChunkIndex++)
    {
      Input.Chunks[ChunkIndex].Memory = AllocateStruct(Input.ChunkSize, u8);
    }

    ReadFileInChunks(&Input);

    parse_chunks_input ParseInput = {};
    ParseInput.Table      = AllocateStruct(1024 * 16, weather_entry);
    ParseInput.Chunks     = Chunks;
    ParseInput.ChunkCount = ChunkCount;
    ParseChunks(&ParseInput);

    parse_string_hashes_input HashInput = {};
    HashInput.Arena.Size = Megabyte(2);
    HashInput.Arena.Memory = (u8*)Allocate(HashInput.Arena.Size);
    HashInput.Chunks = Chunks;
    HashInput.ChunkCount = ChunkCount;
    ParseStringHashes(&HashInput);

    ProduceSortedEntryList(ParseInput.Table);

    u32 PrintMemorySize = Megabyte(1);
    u8 * PrintMemory    = (u8*)Allocate(PrintMemorySize);
    PrintSortedEntryLists(PrintMemory, PrintMemorySize);

    struct __stat64 Stat;
    _stat64(Args[1], &Stat);
    u64 End = ReadCPUTimer();
    u64 TotalElapsed = End - Start;
    u64 Size = Stat.st_size;
    f64 TotalSeconds = (TotalElapsed / (f64)CPUFreq);
    printf("Total time: %0.4fms\n", 1000.0 * (f64)TotalElapsed / (f64)CPUFreq);
    printf("Filesize: %llu after %.2fs, mb/s:%.2f\n", Size, TotalSeconds, Size / (TotalSeconds * Megabyte(1)));


  }
  else
  {
    OutputDebugStringA("No file given!\n");
  }

}
