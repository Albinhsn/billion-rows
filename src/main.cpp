
#if 0
#define TRACY_ENABLE 1
#include "tracy/public/tracy/Tracy.hpp"
#include "tracy/public/TracyClient.cpp"
#else

#define ZoneScopedN(Name)
#define FrameMark
#endif

#include <windows.h>
#include <intrin.h>
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
  u32 TotalChunkCount;
};

struct weather_entry
{
  u8* Name;
  s64 Sum;
  u32 Key;
  s32 Min;
  s32 Max;
  u32 Count;
};

struct parse_chunks_input
{
  weather_entry * Table; // Always 1024 * 16 sized
  weather_entry * Entries; // Always 10k
  u8 * StringMemory;
  
  char * Path;

  u8 * ChunkMemory;
  u64  ChunkSize;

  u64 FileOffset;
  u64 BytesToProcess;

  u32 ThreadIndex;
};


struct chunk_buffer
{
  // Thing we work on
  u8*  ChunkMemory;
  u64  ChunkOffset;
  u64  ChunkSize;

  // File we read from if we need more
  HANDLE FileHandle;
  u64    FileOffset;
  u64    FileSize;

  // Thing we need to figure out if we're done or have more to process
  u64 BytesToProcess;
  u64 BytesProcessed;
};

struct string_entry
{
  volatile u32 Key;
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
volatile u32 TotalStringEntries = 0;
volatile string_entry * GlobalStringTable;
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
#define AllocateStruct(Size, Type) (Type*)Allocate((Size) * sizeof(Type))

inline u8 
Current(chunk_buffer * Buffer)
{
  return *(Buffer->ChunkMemory + Buffer->ChunkOffset);
}

inline u64 Min_(u64 A, u64 B)
{
  return A < B ? A : B;
}

inline void 
AdvanceBuffer(chunk_buffer * Buffer, b32 InName)
{
  Buffer->ChunkOffset++;
  if(Buffer->ChunkOffset >= Buffer->ChunkSize)
  {
    Buffer->BytesProcessed += Buffer->ChunkSize;
    u64 ToRead = Min_(Buffer->ChunkSize, Buffer->FileSize - Buffer->FileOffset);
    Buffer->ChunkSize = ToRead;

    DWORD BytesRead = 0;
    Assert(ReadFile(Buffer->FileHandle, Buffer->ChunkMemory, ToRead, &BytesRead, 0));
    Assert(BytesRead == ToRead);

    Buffer->ChunkOffset = 0;
    Buffer->FileOffset += ToRead;

  }
}


inline s32
Min(s32 a, s32 b)
{
  s32 a0 = b ^ a;
  s32 a1 = -(b < a);
  s32 a2 = a0 & a1;
  s32 a3 = a ^ a2;

  return a3;
}

inline s32
Max(s32 a, s32 b)
{
  s32 Result = a ^ ((b ^ a) & -(b > a));
  return Result;
}

inline u32
AtomicCompareExchange(volatile u32* Dest, u32 Exchange, u32 Compare)
{
  u32 Result = InterlockedCompareExchange(Dest, Exchange, Compare);
  return Result;
}

inline void
AtomicIncrementU32(volatile u32* Dest)
{
  InterlockedIncrement(Dest);
}

inline u64
AtomicCompareExchange64(volatile u64* Dest, u64 Exchange, u64 Compare)
{
  u64 Result = InterlockedCompareExchange64((volatile s64*)Dest, Exchange, Compare);
  return Result;
}

inline void
AddToGlobalStringTable(u32 Key, u8* Name)
{
  // ZoneScopedN("Add to global string table");
  u32 Mask = (1024 * 16) - 1;
  u32 Index = Key & Mask;
  while(true)
  {
    volatile string_entry * Entry = GlobalStringTable + Index;
    if(Entry->Key == Key)
    {
      return;
    }
    if(Entry->Key == 0)
    {
      u32 PrevKey = AtomicCompareExchange(&Entry->Key, Key, 0);
      if(!PrevKey)
      {
        AtomicIncrementU32(&TotalStringEntries);
        Entry->Key = Key;
        Entry->Name = Name;
        return;
      }
    }
    Index = (Index + 1) & Mask;
  }

}

inline b32
LookupOrAddEntry(weather_entry ** Out, weather_entry * Table, u32 Key, u8 * Name)
{
  // ZoneScopedN("Lookup or add entry");
  u32 Mask = (1024 * 16) - 1;
  u32 Index = Key & Mask;

  while(true)
  {
    weather_entry * Entry = Table + Index;
    if(Entry->Key == Key)
    {
      *Out = Entry;
      return false;
    }
    if(Entry->Key == 0)
    {
      Entry->Key = Key;
      Entry->Name = Name;
      Entry->Min = INT_MAX;
      Entry->Max = -INT_MAX;
      *Out = Entry;
      AddToGlobalStringTable(Key, Name);
      return true;
    }
    Index = (Index + 1) & Mask;
  }
}



inline s32
WeatherCompare(const void * A_, const void * B_)
{
  weather_entry * A = (weather_entry*)A_;
  weather_entry * B = (weather_entry*)B_;

  return strcmp((const char*)A->Name, (const char*)B->Name);
};

inline s32
StringCompare(const void * A_, const void * B_)
{
  string_entry * A = (string_entry*)A_;
  string_entry * B = (string_entry*)B_;

  return strcmp((const char*)A->Name, (const char*)B->Name);
};


struct number_lut_element{
        u16 E[16];
};
number_lut_element NumberLUT[] =
{
  {0, 0, 0, 0, 0},
  {1, 0, 0, 0, 0},
  {0, 1, 0, 0, 0},
  {10, 1, 0, 0, 0},
  {0, 0, 1, 0, 0},
  {100, 0, 1, 0, 0}, // 3
  {0, 10, 1, 0, 0},
  {100, 10, 1, 0, 0},
  {0, 0, 0, 1, 0},
  {1000, 0, 0, 1, 0},
  {0, 100, 0, 1, 0},
  {1000, 100, 0, 1, 0}, // 4
  {0, 0, 10, 1, 0},
  {1000, 0, 10, 1, 0},
  {0, 100, 10, 1, 0},
  {1000, 100, 10, 1, 0},
  {0, 0, 0, 0, 1},
  {10000, 0, 0, 0, 1},
  {0, 1000, 0, 0, 1},
  {10000, 1000, 0, 0, 1},
  {0, 0, 100, 0, 1},
  {10000, 0, 100, 0, 1},
  {0, 1000, 100, 0, 1},
  {10000, 1000, 100, 0, 1}, // 5
  {0, 0, 0, 10, 1},
  {10000, 0, 0, 10, 1},
  {0, 1000, 0, 10, 1},
  {10000, 1000, 0, 10, 1},
  {0, 0, 100, 10, 1},
  {10000, 0, 100, 10, 1},
  {0, 1000, 100, 10, 1},
  {10000, 1000, 100, 10, 1},
};

u16 OffsetLUT[ArrayCount(NumberLUT)] =
{
  0,
  0,
  0,
  0,
  0,
  3, // 3
  0,
  0,
  0,
  0,
  0,
  4, // 4
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  5, // 5
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0
};

THREAD_ENTRYPOINT(ParseChunks)
{
  parse_chunks_input * Input = (parse_chunks_input*)RawInput;

  chunk_buffer Buffer = {};
  Buffer.ChunkMemory = Input->ChunkMemory;
  Buffer.ChunkSize   = Input->ChunkSize;
  Buffer.ChunkOffset = 0;

  Buffer.FileHandle = CreateFileA(Input->Path, GENERIC_READ, FILE_SHARE_READ, 0,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  Assert(Buffer.FileHandle != INVALID_HANDLE_VALUE);
  Buffer.FileOffset  = Input->FileOffset;

  struct __stat64 Stat;
  _stat64(Input->Path, &Stat);
  Buffer.FileSize    = Stat.st_size;

  DWORD BytesRead = 0;
  SetFilePointerEx(Buffer.FileHandle, *(LARGE_INTEGER*)&Buffer.FileOffset, 0, FILE_BEGIN);
  Assert(ReadFile(Buffer.FileHandle, Buffer.ChunkMemory, Buffer.ChunkSize, &BytesRead, 0));
  Buffer.FileOffset += BytesRead;

  Buffer.BytesToProcess = Input->BytesToProcess;
  Buffer.BytesProcessed = 0;


  if(Input->ThreadIndex != 0)
  {
    while(Current(&Buffer) != '\n')
    {
      AdvanceBuffer(&Buffer, false);
    }
    AdvanceBuffer(&Buffer, false);
  }


  u8 * Strings = Input->StringMemory;
  u64 Allocated = 0;
  while(Buffer.BytesProcessed < Buffer.BytesToProcess)
  {
    // Parse name
    u32 Key = 2166136261u;
    u8 * Start = Strings;
    while(Current(&Buffer) != ';')
    {
      u8 C = Current(&Buffer);
      *Strings++ = C;
      Key ^= C;
      Key *= 16777619;
      AdvanceBuffer(&Buffer, false);
    }

    // Skip the ';'
    *Strings++ = '\0';

    AdvanceBuffer(&Buffer, false);

    s32 Sign = 1;
    if(Current(&Buffer) == '-')
    {
      AdvanceBuffer(&Buffer, false);
      Sign = -1;
    }

    s32 Number = 0;
    #if 1
    u32 D0 = Current(&Buffer) - '0';
    AdvanceBuffer(&Buffer, false);
    u32 D1 = Current(&Buffer) - '0';
    AdvanceBuffer(&Buffer, false);
    if(D1 <= 9)
    {
      AdvanceBuffer(&Buffer, false);
      D0 *= 100;
      D1 *= 10;
    }
    else
    {
      D0 *= 10;
      D1 = 0;
    }
    u8 F1 = Current(&Buffer) - '0';
    AdvanceBuffer(&Buffer, false);
    Number = D1 + D0 + F1;
    Number = Sign * Number;
    #else

    __m128i D = _mm_loadu_si128((__m128i*)(Buffer.ChunkMemory + Buffer.ChunkOffset));
    __m128i Zero = _mm_set1_epi8(-1);
    __m128i ZeroA = _mm_set1_epi8('0');
    __m128i Nine = _mm_set1_epi8(10);
    __m128i Diff = _mm_sub_epi8(D, ZeroA);
    __m128i MaskGT = _mm_cmpgt_epi8(Diff, Zero);
    __m128i MaskLT = _mm_cmplt_epi8(Diff, Nine);
    __m128i Mask128 = _mm_and_si128(MaskGT, MaskLT);
    u32 Mask       = _mm_movemask_epi8(Mask128);
    Mask &= 0b11111;


    __m128i MulMask = *(__m128i*)&NumberLUT[Mask];
    u32 ExtraOffset = OffsetLUT[Mask];
    Buffer.ChunkOffset += ExtraOffset;

    __m128i A = MulMask;
    __m128i B = _mm_unpacklo_epi8(Diff, _mm_setzero_si128());

    __m128i P = _mm_mullo_epi16(A, B);

    __m128i Sum32 = _mm_add_epi32(_mm_unpacklo_epi16(P, _mm_setzero_si128()),
                                  _mm_unpackhi_epi16(P, _mm_setzero_si128())
                                  );

    __m128i S1 = _mm_add_epi32(Sum32, _mm_shuffle_epi32(Sum32, _MM_SHUFFLE(2, 3, 0, 1)));
    __m128i S2 = _mm_add_epi32(S1, _mm_shuffle_epi32(S1, _MM_SHUFFLE(1, 0, 3, 2)));

    s32 Result = _mm_cvtsi128_si32(S2);


    Result = Sign ? -Result : Result;
    Number = Result;
    #endif

    AdvanceBuffer(&Buffer, false);
    AdvanceBuffer(&Buffer, false);

    weather_entry * Entry = 0;
    if(LookupOrAddEntry(&Entry, Input->Table, Key, Start))
    {
      Allocated += (u64)Strings - (u64)Start;
    }
    else
    {
      Strings = Start;
    }

    Entry->Min = Min(Entry->Min, Number);
    Entry->Max = Max(Entry->Max, Number);
    Entry->Sum += Number;
    Entry->Count++;
  }

  weather_entry * SortedEntry = Input->Entries;
  u32 EntryCount = 0;
  for(u32 Index = 0;
      Index < 1024 * 16;
      Index++)
  {
    weather_entry * Entry = Input->Table + Index;
    if(Entry->Key)
    {
      *SortedEntry++ = *Entry;
      EntryCount++;
    }
  }

  qsort(Input->Entries, EntryCount, sizeof(weather_entry), WeatherCompare);

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

    u64 BytesToProcessPerThread     = Stat.st_size / MAX_THREAD_COUNT;
    GlobalStringTable       = AllocateStruct(1024 * 16, string_entry);

    u64 ChunkAlignment = 16;
    u64 ChunkSize = Megabyte(2) + ChunkAlignment;
    u8* ChunkMemory = AllocateStruct(ChunkSize * MAX_THREAD_COUNT, u8);

    parse_chunks_input ParseInputs[MAX_THREAD_COUNT] = {};
    weather_entry * SortedEntries[MAX_THREAD_COUNT] = {};

    u8 * StringMemory = (u8*)Allocate(Megabyte(100) * MAX_THREAD_COUNT);
    u64 TableEntries = (1024 * 16 + 10000);
    weather_entry * Tables = AllocateStruct(TableEntries * MAX_THREAD_COUNT, weather_entry);
    {
      TimeBlock("Setup");

      for(u64 ParseThreadIndex = 0;
          ParseThreadIndex < MAX_THREAD_COUNT;
          ParseThreadIndex++)
      {
        parse_chunks_input * ParseInput = ParseInputs + ParseThreadIndex;

        ParseInput->Table       = Tables + (TableEntries * ParseThreadIndex);
        ParseInput->Entries = ParseInput->Table + 1024 * 16;
        SortedEntries[ParseThreadIndex] = ParseInput->Entries;
        ParseInput->StringMemory = StringMemory + (Megabyte(100) * ParseThreadIndex);


        ParseInput->FileOffset = ParseThreadIndex * BytesToProcessPerThread;
        ParseInput->Path = Args[1]; 
        ParseInput->ThreadIndex = (u32)ParseThreadIndex;
        ParseInput->ChunkMemory = ChunkMemory + (ParseThreadIndex * ChunkSize);
        ParseInput->ChunkSize   = ChunkSize;
        ParseInput->BytesToProcess = BytesToProcessPerThread;

        if(ParseThreadIndex == MAX_THREAD_COUNT - 1)
        {
          ParseInput->BytesToProcess += Stat.st_size % MAX_THREAD_COUNT;
        }

        ThreadHandles[ParseThreadIndex] = CreateThread(0, 0, ParseChunks, ParseInput, 0, 0);
      }

    }

    for(u32 ParseThreadIndex = 0;
        ParseThreadIndex < MAX_THREAD_COUNT;
        ParseThreadIndex++)
    {
      WaitForSingleObject(ThreadHandles[ParseThreadIndex], INFINITE);
    }

    {
      #if 1
      TimeBlock("Printing");

      string_entry * Strings = AllocateStruct(TotalStringEntries, string_entry);
      string_entry * Curr = Strings;
      for(u32 Index = 0;
          Index < 1024 * 16;
          Index++)
      {
        string_entry * Entry = (string_entry*)GlobalStringTable + Index;
        if(Entry->Key)
        {
          *Curr++ = *Entry;
        }
      }

      qsort(Strings, TotalStringEntries, sizeof(string_entry), StringCompare);


      u32 BufferIndices[MAX_THREAD_COUNT] = {};

      u8 * Memory = (u8*)Allocate(Megabyte(2));
      u32 Offset = 0;
      u8 * Start = Memory;
      for(u32 StringIndex = 0;
          StringIndex < TotalStringEntries;
          StringIndex++)
      {
        string_entry * StringEntry = Strings + StringIndex;
        weather_entry FinalWeatherEntry = {};
        FinalWeatherEntry.Min = INT_MAX;
        FinalWeatherEntry.Max = -INT_MAX;
        for(u32 BufferIndex = 0;
            BufferIndex < ArrayCount(BufferIndices);
            BufferIndex++)
        {
          weather_entry * Buffer = SortedEntries[BufferIndex];
          u32 CurrentIndex       = BufferIndices[BufferIndex];
          weather_entry * Entry = Buffer + CurrentIndex;

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

        u32 Written = sprintf((char*)Memory, "%s=%.1f/%.1f/%.1f/%d, ", StringEntry->Name, Minimum,
                          Mean, Maximum, FinalWeatherEntry.Count);
        Offset += Written;
        Memory += Written;
      }

        #if 1
      printf("{%s}\n", Start);
#else
      FILE * File;
      fopen_s(&File, "out.txt", "w");

      fprintf(File, "{%.*s}\n", Offset,Start);

#endif

      printf("---\n");
      #endif
      EndAndPrintProfile(Stat.st_size, __COUNTER__ + 1);


    }
  }
  else
  {
    OutputDebugStringA("No file given!\n");
  }

}
