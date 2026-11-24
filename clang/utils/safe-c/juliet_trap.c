/* Linked into every Juliet program by juliet.ts: says whose trap ended the
 * program. A check the compiler inserted traps inside the executable; macOS
 * malloc's own checks (double free, corrupted heap) trap inside a system
 * library and must not be credited to Safe C. */
#include <dlfcn.h>
#include <mach-o/ldsyms.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
static void on_trap(int sig, siginfo_t *si, void *ctx) {
  ucontext_t *uc = ctx;
#if defined(__arm64__)
  void *pc = (void *)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
#else
  void *pc = (void *)uc->uc_mcontext->__ss.__rip;
#endif
  Dl_info info;
  int ours = dladdr(pc, &info) && info.dli_fbase == (void *)&_mh_execute_header;
  if (ours)
    write(2, "JULIET-TRAP: program\n", 21);
  else
    write(2, "JULIET-TRAP: library\n", 21);
  signal(sig, SIG_DFL); /* returning runs the trap again, now fatal */
  raise(sig);
}
__attribute__((constructor)) static void install(void) {
  struct sigaction sa = {0};
  sa.sa_sigaction = on_trap;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGTRAP, &sa, 0);
  sigaction(SIGILL, &sa, 0);
}
