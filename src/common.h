#ifndef COMMON_H
#define COMMON_H

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

#include <cstdio>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <x86intrin.h>

#include <time.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef int8_t i8;
typedef int16_t i16;
typedef int i32;
typedef int64_t i64;

typedef struct Vec3f32;

#define PROFILER 1

#ifndef PROFILER
#define PROFILER 0
#endif

struct Profiler {
  u64 StartTSC;
  u64 EndTSC;
};

extern Profiler profiler;
u64 ReadCPUTimer(void);
u64 EstimateCPUTimerFreq(void);

void initProfiler();
void displayProfilingResult();

#if PROFILER

struct ProfileAnchor {
  u64 elapsedExclusive; // NOTE(casey): Does NOT include children
  u64 elapsedInclusive; // NOTE(casey): DOES include children
  u64 hitCount;
  u64 processedByteCount;
  char const *label;
};
extern ProfileAnchor globalProfileAnchors[4096];
extern u32 globalProfilerParentIndex;

struct ProfileBlock {
  char const *label;
  u64 oldElapsedInclusive;
  u64 startTime;
  u32 parentIndex;
  u32 index;
  ProfileBlock(char const *label_, u32 index_, u64 byteCount) {
    parentIndex = globalProfilerParentIndex;

    index = index_;
    label = label_;

    ProfileAnchor *profile = globalProfileAnchors + index;
    oldElapsedInclusive = profile->elapsedInclusive;
    profile->processedByteCount += byteCount;

    globalProfilerParentIndex = index;
    startTime = ReadCPUTimer();
  }

  ~ProfileBlock(void) {
    u64 elapsed = ReadCPUTimer() - startTime;
    globalProfilerParentIndex = parentIndex;

    ProfileAnchor *parent = globalProfileAnchors + parentIndex;
    ProfileAnchor *profile = globalProfileAnchors + index;

    parent->elapsedExclusive -= elapsed;
    profile->elapsedExclusive += elapsed;
    profile->elapsedInclusive = oldElapsedInclusive + elapsed;
    ++profile->hitCount;

    profile->label = label;
  }
};

#define NameConcat2(A, B) A##B
#define NameConcat(A, B) NameConcat2(A, B)
#define TimeBandwidth(Name, ByteCount)                                         \
  ProfileBlock NameConcat(Block, __LINE__)(Name, __COUNTER__ + 1, ByteCount);
#define TimeBlock(Name) TimeBandwidth(Name, 0)
#define ProfilerEndOfCompilationUnit                                           \
  static_assert(                                                               \
      __COUNTER__ < ArrayCount(GlobalProfilerAnchors),                         \
      "Number of profile points exceeds size of profiler::Anchors array")
#define TimeFunction TimeBlock(__func__)

#else

#define TimeBlock(blockName)
#define TimeFunction
#endif

#define MIN(x, y) (x < y ? x : y)
#define MAX(x, y) (x < y ? y : x)
#define PI 3.14159265358979323846 /* pi */

#define DEGREES_TO_RADIANS(d) (d * PI / 100.0)
#define RANDOM_DOUBLE rand() / (RAND_MAX + 1.0)
#define RANDOM_DOUBLE_IN_RANGE(min, max) (min + (max - min) * RANDOM_DOUBLE)

#define LINEAR_TO_GAMMA(x) sqrt(x)

#define RED ((struct Vec3f32){1.0f, 0, 0})
#define YELLOW ((struct Vec3f32){1.0f, 1.0f, 0})
#define GREEN ((struct Vec3f32){0, 1.0f, 0})
#define CYAN ((struct Vec3f32){0, 1.0f, 1.0f})
#define PURPLE ((struct Vec3f32){1.0f, 0, 1.0f})
#define BLUE ((struct Vec3f32){0.0f, 0, 1.0f})
#define WHITE ((struct Vec3f32){1.0f, 1.0f, 1.0f})
#define BLACK ((struct Vec3f32){0, 0, 0})
#define SOMEBLUE ((struct Vec3f32){0.5f, 0.7f, 1.0f})

#endif
