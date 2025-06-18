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
  volatile u32 ParseIndex;
};
volatile flag_table * GlobalFlagTable;

struct chunk
{
  u8 * Memory;
  u8 * Next;
  u64  Size;
  b32  Lock;
  b32  Done;
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
  chunk * Chunks         = Input->Chunks;

  u32 BytesRead   = 0;
  u64 ChunkIndex  = 0;

  b32 FirstLap = false;

  while(BytesRemaining)
  {
    chunk* Chunk = Chunks + (ChunkIndex * ChunkSize);
    ChunkIndex = (ChunkIndex + 1) & ChunkMask;
    FirstLap |= ChunkIndex == 0 ? true : false;
    Assert(!(Chunk->Done ^ FirstLap));

    u64 ToRead = BytesRemaining < ChunkSize ? BytesRemaining : ChunkSize;

    Chunk->Size = ToRead;
    ReadFile(File, Chunk->Memory, ToRead, (LPDWORD)&BytesRead, 0);
    BytesRemaining -= ToRead;
  }

  GlobalFlagTable->FinishedReading = true;

  CloseHandle(File);
  return 0;
}

struct parse_chunks_input
{
  arena   Arena;
  chunk * Chunks;
  u32     ChunkCount;
};

struct weather_entry
{
  u64 Hash;
  u32 Min;
  u32 Max;
  u32 Sum;
  u32 Count;
};

struct parsed_chunk
{
  weather_entry * Entries;
  u64 EntryCount;
};

struct parse_chunk_output
{
  weather_entry * Entries; // Sorted entries?
  u64 EntryCount;
};

THREAD_ENTRYPOINT(ParseChunks)
{
  u64 ChunkIndex = 0;

  while(true)
  {
    // Check if we're done 
    if(GlobalFlagTable->FinishedParsing)
    {

    }

    // Check if the one you're at is either locked or done 
    //  Fastfowards if so

    // Check if you can acquire the one you're at

  }
}

int
main(int ArgCount, char** Args)
{
  if(ArgCount > 1)
  {

    flag_table Table = {};
    GlobalFlagTable = &Table;

    read_file_in_chunks_input Input = {};
    Input.Path = Args[1];
    Input.ChunkSize = Megabyte(4);
    u32 NumberOfChunks = 32;
    Input.ChunkCount = NumberOfChunks;
    Input.Chunks = AllocateStruct(Input.ChunkCount, chunk);
    for(u32 ChunkIndex = 0;
        ChunkIndex < NumberOfChunks;
        ChunkIndex++)
    {
      Input.Chunks[ChunkIndex].Memory = AllocateStruct(NumberOfChunks, u8);
    }

    ReadFileInChunks(&Input);

  }
  else
  {
    OutputDebugStringA("No file given!\n");
  }

}
