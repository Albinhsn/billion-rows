
struct test_value
{
  u64 CPUTimer;
  u64 MemPageFaults;
};

struct test_results
{
  test_value Max;
  test_value Min;
  u64 TestCount;
  u64 TotalTimeTested;
  u64 TotalMemPageFaults;
};

enum tester_state
{
  Tester_Error,
  Tester_Completed,
  Tester_Running
};

struct repetition_tester
{
  u64 TimeSinceLastNewMinimum;
  f64 MaxTimeWithoutNewMin;
  u64 CPUFreq;
  u64 ExpectedBytesProcessed;
  test_results Results;
  tester_state State;
};

struct os_metrics
{
  b32 Initialized;
  HANDLE ProcessHandle;
};

os_metrics GlobalMetrics;

void
InitializeOSMetrics()
{
  if(!GlobalMetrics.Initialized)
  {
    GlobalMetrics.Initialized = true;
    GlobalMetrics.ProcessHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                              false, GetCurrentProcessId());
  }
}

u64
ReadOSPageFaultCount()
{
  PROCESS_MEMORY_COUNTERS_EX MemoryCounters = {};

  MemoryCounters.cb = sizeof(MemoryCounters);
  GetProcessMemoryInfo(GlobalMetrics.ProcessHandle, (PROCESS_MEMORY_COUNTERS*)&MemoryCounters,
                       sizeof(MemoryCounters));

  u64 Result = MemoryCounters.PageFaultCount;
  return Result;
}

inline u64
GetOSTimerFreq(void)
{
	LARGE_INTEGER Freq;
	QueryPerformanceFrequency(&Freq);
	return Freq.QuadPart;
}

inline u64
ReadOSTimer(void)
{
	LARGE_INTEGER Value;
	QueryPerformanceCounter(&Value);
	return Value.QuadPart;
}

inline u64
ReadCPUTimer(void)
{
	return __rdtsc();
}



u64
EstimateCPUTimerFreq(void)
{
	u64 MillisecondsToWait = 100;
	u64 OSFreq = GetOSTimerFreq();

	u64 CPUStart = ReadCPUTimer();
	u64 OSStart = ReadOSTimer();
	u64 OSEnd = 0;
	u64 OSElapsed = 0;
	u64 OSWaitTime = OSFreq * MillisecondsToWait / 1000;
	while(OSElapsed < OSWaitTime)
	{
		OSEnd = ReadOSTimer();
		OSElapsed = OSEnd - OSStart;
	}
	
	u64 CPUEnd = ReadCPUTimer();
	u64 CPUElapsed = CPUEnd - CPUStart;
	
	u64 CPUFreq = 0;
	if(OSElapsed)
	{
		CPUFreq = OSFreq * CPUElapsed / OSElapsed;
	}
	
	return CPUFreq;
}


inline b32 
IsTesting(repetition_tester * Tester)
{
  f64 SecondsSinceMin = Tester->TimeSinceLastNewMinimum / (f64)Tester->CPUFreq;

  if(Tester->State != Tester_Error && SecondsSinceMin >= Tester->MaxTimeWithoutNewMin)
  {
    Tester->State = Tester_Completed;
  }

  return Tester->State == Tester_Running;
}

inline void
NewTestWave(repetition_tester * Tester)
{
  Tester->State = Tester_Running;
  Tester->TimeSinceLastNewMinimum = 0;
  
  test_results * Results = &Tester->Results;
  Results->Max.CPUTimer = 0;
  Results->Max.MemPageFaults = 0;
  Results->Min.CPUTimer = UINT64_MAX;
  Results->Min.MemPageFaults = 0;
  Results->TestCount = 0;
  Results->TotalTimeTested = 0;
  Results->TotalMemPageFaults = 0;
}

inline void
PrintResult(const char * Label, test_value TestValue, u64 Freq, u64 ByteCount)
{
  f64 Seconds = TestValue.CPUTimer / (f64)Freq;
  f64 Milliseconds = Seconds * 1000.0;

  f64 Throughput = ByteCount / (Seconds * Gigabyte(1));

  printf("%10s: %15llu (%10.5f) %10.4fgb/s", Label, TestValue.CPUTimer, Milliseconds, Throughput);
  if(TestValue.MemPageFaults > 0)
  {
    f64 FaultPerKB = (ByteCount / ((f64)TestValue.MemPageFaults * Kilobyte(1)));
    printf("%10llu %5.4fk/fault", TestValue.MemPageFaults, FaultPerKB);
  }
}

inline void
InitTester(repetition_tester * Tester, u64 ExpectedBytesProcessed, f64 MaxTimeWithoutNewMin)
{
  Tester->CPUFreq = EstimateCPUTimerFreq();
  Tester->ExpectedBytesProcessed = ExpectedBytesProcessed;
  Tester->State = Tester_Completed;
  Tester->MaxTimeWithoutNewMin = MaxTimeWithoutNewMin;
}


inline void
AddTestResult(repetition_tester * Tester, test_results * Result, test_value Value)
{
  u64 Elapsed = Value.CPUTimer;
  u64 MemPageFaults = Value.MemPageFaults;
  Tester->TimeSinceLastNewMinimum += Elapsed;
  Result->TotalTimeTested += Elapsed;
  Result->TestCount++;
  Result->TotalMemPageFaults += MemPageFaults;

  if(Elapsed < Result->Min.CPUTimer)
  {
    Result->Min = Value;
    printf("                                                             \r");
    PrintResult("Min", Value, Tester->CPUFreq, Tester->ExpectedBytesProcessed);
    Tester->TimeSinceLastNewMinimum = 0;
  }
  if(Elapsed > Result->Max.CPUTimer)
  {
    Result->Max = Value;
  }
}


inline test_value
BeginTime(repetition_tester * Tester)
{

  test_value Result = {};
  Result.MemPageFaults -= ReadOSPageFaultCount();
  Result.CPUTimer -= ReadCPUTimer();

  return Result;
}

inline void 
EndTime(repetition_tester * Tester, test_value *Start)
{
  Start->CPUTimer      += ReadCPUTimer();
  Start->MemPageFaults += ReadOSPageFaultCount();
}

inline void 
Error(repetition_tester * Tester, const char * Reason)
{
  printf("ERROR: %s\n", Reason);
  Tester->State = Tester_Error;
}

void 
PrintTestResults(repetition_tester * Tester)
{
  test_results Results = Tester->Results;
  printf("                                                             \r");
  PrintResult("Min", Results.Min, Tester->CPUFreq, Tester->ExpectedBytesProcessed);
  printf("\n");
  PrintResult("Max", Results.Max, Tester->CPUFreq, Tester->ExpectedBytesProcessed);
  printf("\n");

  test_value Avg = {};
  Avg.CPUTimer = Results.TotalTimeTested / Results.TestCount;
  Avg.MemPageFaults = Results.TotalMemPageFaults / Results.TestCount;
  PrintResult("Avg", Avg, Tester->CPUFreq, Tester->ExpectedBytesProcessed);
  printf("\n");
  printf("--------------------\n");
}
