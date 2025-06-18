#ifndef PLATFORM_H
#define PLATFORM_H

typedef struct platform_thread platform_thread;

#if PLATFORM_LINUX
#include <sys/mman.h>
#include <unistd.h>
#include <sys/time.h>
#include <x86intrin.h>
#include <pthread.h>



inline void *
PlatformAllocate(u64 Size)
{
  return mmap(NULL, Size,
       PROT_READ | PROT_WRITE,
       MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE2MB,
       -1, 0);
}

inline u8* 
PlatformMapFile(char * Path, u32 * Size, void ** Handle)
{
  int         fd = open(Path, O_RDONLY);
  struct stat fileStat;
  fstat(fd, &fileStat);

  size_t fileSize = fileStat.st_size;
  u8 * Content     = (u8*)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);

  *Handle = fd;
  *Size = fileSize;
  return Content;
}

inline u8* 
PlatformMapFile(char * Path, u32 * Size, void ** Handle)
{
  int         fd = open(Path, O_RDONLY);
  struct stat fileStat;
  fstat(fd, &fileStat);

  size_t fileSize = fileStat.st_size;
  u8 * Content     = (u8*)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);

  *Handle = fd;
  *Size = fileSize;
  return Content;
}

inline void
PlatformDeallocate(void * Memory)
{
  // ToDo this is wrong!
  return munmap(Memory);
}


#else


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

inline void *
PlatformAllocate(u64 Size)
{
  return VirtualAlloc(0, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

inline void
PlatformDeallocate(void * Memory)
{
  VirtualFree(Memory, 0, MEM_RELEASE | MEM_DECOMMIT);
}

inline u8* 
PlatformMapFile(char * Path, u32 * Size, void ** Handle)
{
  struct __stat64 Stat;
  _stat64(Path, &Stat);

  *Size = Stat.st_size;
  HANDLE File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, 0,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  HANDLE Mapping = CreateFileMappingA(File, 0, PAGE_READWRITE, 0, 0, 0);

  *Handle = (void*)File;
  return (u8 *)MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
}

inline u8* 
PlatformMapFile(char * Path, u64 * Size)
{
  struct __stat64 Stat;
  _stat64(Path, &Stat);

  *Size = Stat.st_size;
  HANDLE File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, 0,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  HANDLE Mapping = CreateFileMappingA(File, 0, PAGE_READONLY, 0, 0, 0);
  return (u8 *)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
}

#define PLATFORM_THREAD_FUNC(Name) DWORD Name(void* Input)
typedef PLATFORM_THREAD_FUNC(platform_thread_func);

struct platform_thread
{
  HANDLE Handle;
  DWORD ID;
};


inline void
PlatformThreadCreate(platform_thread * Thread, platform_thread_func * Func, void * Input)
{
  Thread->Handle = CreateThread(0, 0, Func, Input, 0, &Thread->ID);
}

inline void
PlatformThreadJoin(platform_thread * Thread)
{
  WaitForSingleObject(Thread->Handle, INFINITE);
  CloseHandle(Thread->Handle);
}



#endif


#endif
