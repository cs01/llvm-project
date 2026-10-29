// Stand-in for the profile runtime when building with function entry
// coverage (-fprofile-instrument=llvm -mllvm -pgo-function-entry-coverage).
// At exit, each instrumented image appends the NameRef (MD5 of the PGO name)
// of every function that ran to $SEMTEST_DIR/<basename of LLVM_PROFILE_FILE>.
// The file is created empty at startup and only gets its trailing marker at
// exit, so a process that never reached exit (crash, _exit) leaves a file the
// reader can tell is incomplete instead of one that silently lists too little.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef intptr_t IntPtrT;
enum ValueKind {
#define VALUE_PROF_KIND(Enumerator, Value, Descr) Enumerator = Value,
#include "llvm/ProfileData/InstrProfData.inc"
};
typedef struct ProfData {
#define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer) Type Name;
#include "llvm/ProfileData/InstrProfData.inc"
} ProfData;

#ifdef __APPLE__
extern ProfData DataStart __asm("section$start$__DATA$__llvm_prf_data");
extern ProfData DataEnd __asm("section$end$__DATA$__llvm_prf_data");
#define DATA_BEGIN (&DataStart)
#define DATA_END (&DataEnd)
#else
extern ProfData __start___llvm_prf_data[] __attribute__((weak, visibility("hidden")));
extern ProfData __stop___llvm_prf_data[] __attribute__((weak, visibility("hidden")));
#define DATA_BEGIN __start___llvm_prf_data
#define DATA_END __stop___llvm_prf_data
#endif

// Instrumented code references this to pull the profile runtime in.
__attribute__((visibility("hidden"))) int __llvm_profile_runtime;

static const uint64_t CompleteMarker = 0x73656d7465737421ull; // "semtest!"
static int OutFD = -1;

__attribute__((constructor)) static void semtestOpen(void) {
  const char *Dir = getenv("SEMTEST_DIR"), *Prof = getenv("LLVM_PROFILE_FILE");
  if (!Dir || !Prof)
    return;
  const char *Base = strrchr(Prof, '/');
  Base = Base ? Base + 1 : Prof;
  // One file per process and image: %p becomes the pid, and the image's own
  // data address tells apart a tool and the dylibs it loads.
  char Path[4096];
  int N = snprintf(Path, sizeof(Path), "%s/", Dir);
  for (const char *C = Base; *C && N < (int)sizeof(Path) - 64; ++C) {
    if (C[0] == '%' && C[1] == 'p') {
      N += snprintf(Path + N, sizeof(Path) - N, "%d", (int)getpid());
      ++C;
    } else if (C[0] == '%' && C[1] == 'm') {
      N += snprintf(Path + N, sizeof(Path) - N, "%lx",
                    (unsigned long)(uintptr_t)DATA_BEGIN);
      ++C;
    } else if (C[0] == '%' && C[1] >= '0' && C[1] <= '9') {
      ++C;
    } else {
      Path[N++] = *C;
    }
  }
  Path[N] = 0;
  OutFD = open(Path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
}

__attribute__((destructor)) static void semtestWrite(void) {
  if (OutFD < 0)
    return;
  uint64_t Buf[512];
  size_t N = 0;
  for (const ProfData *D = DATA_BEGIN; D < DATA_END; ++D) {
    // With entry coverage each function has one byte counter that starts at
    // 0xff and is cleared on entry.
    const uint8_t *Counter = (const uint8_t *)D + D->CounterPtr;
    if (*Counter != 0)
      continue;
    Buf[N++] = D->NameRef;
    if (N == 512) {
      write(OutFD, Buf, sizeof(Buf));
      N = 0;
    }
  }
  Buf[N++] = CompleteMarker;
  write(OutFD, Buf, N * sizeof(uint64_t));
  close(OutFD);
  OutFD = -1;
}
