

inline void
BeginProfile()
{
  GlobalProfiler.CPUFreq = EstimateCPUTimerFreq();
  GlobalProfiler.BeginClock = ReadCPUTimer();
  GlobalProfiler.MeasurementCount = 0;
}

inline void
PrintTimeElapsedBegin(char const *Label, u64 TotalTSCElapsed, u64 Begin, u64 End)
{
    u64 Elapsed = End - Begin;
    f64 Percent = 100.0 * ((f64)Elapsed / (f64)TotalTSCElapsed);
    printf("  %s: %llu (%.2f%%)\n", Label, Elapsed, Percent);
}

inline void
PrintTimeElapsed(char const *Label, u64 TotalTSCElapsed, u64 Elapsed, u64 HitCount)
{
    f64 Percent = 100.0 * ((f64)Elapsed / (f64)TotalTSCElapsed);
    printf("  %s: %llu cyl %llu h, %llu cyl/h (%.2f%%)\n", Label, Elapsed, 
           HitCount, Elapsed / HitCount, Percent);
}


void 
EndAndPrintProfile(u64 Size, u64 Counter)
{

  u64 End = ReadCPUTimer();
  u64 TotalElapsed = End - GlobalProfiler.BeginClock;
  u64 Freq = GlobalProfiler.CPUFreq;
  f64 TotalSeconds = (TotalElapsed / (f64)Freq);
  printf("Total time: %0.4fms (CPU freq %llu)\n", 1000.0 * (f64)TotalElapsed / (f64)Freq, Freq);
  printf("Filesize: %.2fmb after %.2fs, mb/s:%.2f\n", Size / (f32)Megabyte(1), TotalSeconds, Size / (TotalSeconds * Megabyte(1)));

  #ifndef PROFILER
  #define PROFILER 1
  #endif
  #if PROFILER
  for(u32 MeasurementIndex = 0;
      MeasurementIndex < Counter;
      MeasurementIndex++)
  {
    profile_measurement * Measurement = GlobalProfiler.Measurements + MeasurementIndex;

    if(Measurement->Function)
    {
      u64 HitCount  = Measurement->HitCount;
      f64 Percent = 100.0 * ((f64)Measurement->CyclesExclusive / (f64)TotalElapsed);
      printf("%s[%llu]: %llu, (%.2f%%", Measurement->Function, HitCount,
             Measurement->CyclesExclusive, Percent);
      if(Measurement->CyclesInclusive != Measurement->CyclesExclusive)
      {
        f64 PercentWithChildren  = 100.0 * ((f64)(Measurement->CyclesInclusive) / (f64)TotalElapsed);
        printf(", %.2f%% wc", PercentWithChildren);
      }
      printf(")");

      if(Measurement->ProcessedByteCount)
      {
        f64 SecondsForMeasurement = Measurement->CyclesInclusive / (f64)Freq;
        f64 BytesPerSecond = Measurement->ProcessedByteCount / SecondsForMeasurement;
        f64 Megabytes = Measurement->ProcessedByteCount / Megabyte(1);
        f64 GBS = BytesPerSecond / Gigabyte(1);

        printf(" %.3fmb at %.2fgb/s", Megabytes, GBS);

      }
      printf("\n");
    }
  }
  #endif

}
