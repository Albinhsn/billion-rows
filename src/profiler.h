#ifndef PROFILER_H
#define PROFILER_H

struct profile_measurement
{
  const char * Function;
  u64 HitCount;
  u64 CyclesExclusive;
  u64 CyclesInclusive;
  u64 ProcessedByteCount;
};

#define MAX_MEASUREMENTS 65536
struct profiler
{
  u64 BeginClock;
  u64 CPUFreq;
  u64 MeasurementCount;
  u64 Parent;
  profile_measurement Measurements[MAX_MEASUREMENTS];
};

profiler GlobalProfiler;


struct timed_function
{
  u64 InitialTimestamp;
  u64 CounterIndex;
  u64 Parent;
  u64 OldInclusive;
  timed_function(const char * Function, u64 CounterIndex, u32 ByteCount)
  {
    this->Parent        = GlobalProfiler.Parent;
    this->CounterIndex       = CounterIndex;

    profile_measurement * Measurement = GlobalProfiler.Measurements + CounterIndex;
    Measurement->Function = Function;
    Measurement->HitCount++;
    Measurement->ProcessedByteCount = ByteCount;

    OldInclusive = Measurement->CyclesInclusive;

    GlobalProfiler.Parent = CounterIndex;

    InitialTimestamp = ReadCPUTimer();
  }

  ~timed_function()
  {
    u64 Elapsed = ReadCPUTimer() - InitialTimestamp;
    GlobalProfiler.Parent = this->Parent;

    profile_measurement * Measurement = GlobalProfiler.Measurements + CounterIndex;
    profile_measurement * Parent = GlobalProfiler.Measurements + this->Parent;

    Parent->CyclesExclusive -= Elapsed;
    Measurement->CyclesExclusive += Elapsed;
    Measurement->CyclesInclusive = OldInclusive + Elapsed;
  }
};


#define TimeFunction__(Line, Name, ByteCount) timed_function TimedFunction##Line(Name, __COUNTER__ + 1, ByteCount)
#define TimeFunction_(Line, Name, ByteCount) TimeFunction__(Line, Name, ByteCount)

#if PROFILER
#define TimeFunction TimeFunction_(__LINE__, __FUNCTION__, 0)
#define TimeBlock(Name) TimeFunction_(__LINE__, Name, 0)
#define TimeBandwidth(Name, ByteCount) TimeFunction_(__LINE__, Name, ByteCount)
#else
#define TimeFunction
#define TimeBlock(Name) 
#define TimeBandwidth(Name, ByteCount) 
#endif





#endif
