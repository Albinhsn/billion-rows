#include <stdio.h>

Profiler      profiler;
u32           globalProfilerParentIndex = 0;
ProfileAnchor globalProfileAnchors[4096];

static void   PrintTimeElapsed(ProfileAnchor* Anchor, u64 timerFreq, u64 TotalTSCElapsed)
{

  f64 Percent = 100.0 * ((f64)Anchor->elapsedExclusive / (f64)TotalTSCElapsed);
  printf("  %s[%llu]: %llu (%.2f%%", Anchor->label, Anchor->hitCount, Anchor->elapsedExclusive, Percent);
  if (Anchor->elapsedInclusive != Anchor->elapsedExclusive)
  {
    f64 PercentWithChildren = 100.0 * ((f64)Anchor->elapsedInclusive / (f64)TotalTSCElapsed);
    printf(", %.2f%% w/children", PercentWithChildren);
  }
  if (Anchor->processedByteCount)
  {
    f64 mb             = 1024.0f * 1024.0f;
    f64 gb             = mb * 1024.0f;

    f64 seconds        = Anchor->elapsedInclusive / (f64)timerFreq;
    f64 bytesPerSecond = Anchor->processedByteCount / seconds;
    f64 mbProcessed    = Anchor->processedByteCount / mb;
    f64 gbProcessed    = bytesPerSecond / gb;

    printf(" %.3fmb at %.2fgb/s", mbProcessed, gbProcessed);
  }
  printf(")\n");
}
static u64 GetOSTimerFreq(void)
{
  return 1000000;
}

static u64 ReadOSTimer(void)
{
  // NOTE(casey): The "struct" keyword is not necessary here when compiling in C++,
  // but just in case anyone is using this file from C, I include it.
  #if PLATFORM_LINUX
  struct timeval Value;
  gettimeofday(&Value, 0);

  u64 Result = GetOSTimerFreq() * (u64)Value.tv_sec + (u64)Value.tv_usec;
#else
	LARGE_INTEGER Value;
	QueryPerformanceCounter(&Value);
	u64 Result = Value.QuadPart;

  #endif

  return Result;
}

u64 ReadCPUTimer(void)
{

  return __rdtsc();
}

#define TIME_TO_WAIT 100

u64 EstimateCPUTimerFreq(void)
{
  u64 OSFreq     = GetOSTimerFreq();

  u64 CPUStart   = ReadCPUTimer();
  u64 OSStart    = ReadOSTimer();
  u64 OSElapsed  = 0;
  u64 OSEnd      = 0;
  u64 OSWaitTime = OSFreq * TIME_TO_WAIT / 1000;
  while (OSElapsed < OSWaitTime)
  {
    OSEnd     = ReadOSTimer();
    OSElapsed = OSEnd - OSStart;
  }

  u64 CPUEnd     = ReadCPUTimer();
  u64 CPUElapsed = CPUEnd - CPUStart;

  return OSFreq * CPUElapsed / OSElapsed;
}

void initProfiler()
{
  profiler.StartTSC         = ReadCPUTimer();
  globalProfilerParentIndex = 0;
  for (u64 i = 0; i < ArrayCount(globalProfileAnchors); i++)
  {
    globalProfileAnchors[i].label              = 0;
    globalProfileAnchors[i].hitCount           = 0;
    globalProfileAnchors[i].elapsedExclusive   = 0;
    globalProfileAnchors[i].elapsedInclusive   = 0;
    globalProfileAnchors[i].processedByteCount = 0;
  }
}

void displayProfilingResult()
{
  u64 endTime      = ReadCPUTimer();
  u64 totalElapsed = endTime - profiler.StartTSC;
  u64 cpuFreq      = EstimateCPUTimerFreq();

  f64 tot = 1000.0 * (f64)totalElapsed / (f64)cpuFreq;
  profiler.bestTime = tot < profiler.bestTime ? tot : profiler.bestTime; 
  printf("\nTotal time: %0.4fms (CPU freq %llu)\n", tot, cpuFreq);
  for (u32 i = 0; i < ArrayCount(globalProfileAnchors); i++)
  {
    ProfileAnchor* profile = globalProfileAnchors + i;

    if (profile->elapsedInclusive)
    {
      PrintTimeElapsed(profile, cpuFreq, totalElapsed);
    }
  }
}
