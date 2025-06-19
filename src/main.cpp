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

struct flag_table
{
  b32 FinishedReading;
  u32 TotalChunkCount;
};
volatile flag_table * GlobalFlagTable;

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

struct weather_entry
{
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

struct string_entry
{
  u64 Key;
  u8* Name;
};

string_entry * GlobalStringTable; // Array of 10k
u32 StringCount = 0;

struct parse_string_hashes_input
{
  arena Arena;
  chunk * Chunks;
  u32 ChunkCount;
};

inline b32
StringAlreadyExists(u64 Key)
{
  b32 Result = false;
  for(u32 Index = 0;
      Index < StringCount;
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

        string_entry * Entry = GlobalStringTable + StringCount++;
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

  return 0;
}

int
main(int ArgCount, char** Args)
{
  if(ArgCount > 1)
  {

    flag_table Table = {};
    GlobalFlagTable = &Table;

    GlobalStringTable = AllocateStruct(10000, string_entry);

    u32 ChunkCount = 1;
    chunk * Chunks = AllocateStruct(ChunkCount, chunk);

    read_file_in_chunks_input Input = {};
    Input.Path        = Args[1];
    Input.ChunkSize   = Gigabyte(15);
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


  }
  else
  {
    OutputDebugStringA("No file given!\n");
  }

}
