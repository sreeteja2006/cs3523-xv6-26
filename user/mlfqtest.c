#include "kernel/types.h"
#include "user/user.h"

static void
print_info(char *name, int pid)
{
  struct mlfqinfo info;
  if (getmlfqinfo(pid, &info) < 0)
  {
    printf("%s: getmlfqinfo failed for pid %d\n", name, pid);
    return;
  }

  printf("%s\n", name);
  printf("  PID: %d | Level: %d | Scheduled: %d times | Syscalls: %d\n",
         pid, info.level, info.times_scheduled, info.total_syscalls);
  printf("  Ticks: [L0:%d L1:%d L2:%d L3:%d]\n",
         info.ticks[0], info.ticks[1], info.ticks[2], info.ticks[3]);
}

static void
run_cpu_bound(void)
{
  int pid = fork();

  if (pid < 0)
  {
    printf("cpu-bound: fork failed\n");
    exit(1);
  }

  if (pid == 0)
  {
    unsigned long x = 0;
    for (unsigned long i = 0; i < 2000000000; i++)
      x += i;
    exit(0);
  }
  pause(50);
  print_info("=== CPU-bound test ===", pid);
  wait(0);
}

static void
run_interactive(void)
{
  int pid = fork();

  if (pid < 0)
  {
    printf("interactive: fork failed\n");
    exit(1);
  }

  if (pid == 0)
  {
    for (int i = 0; i < 500000; i++)
      getpid();
    exit(0);
  }
  pause(50);
  print_info("=== Interactive/Syscall-heavy test ===", pid);
  wait(0);
}

static void
run_boost(void)
{
  int pid = fork();

  if (pid < 0)
  {
    printf("boost: fork failed\n");
    exit(1);
  }

  if (pid == 0)
  {
    unsigned long x = 0;
    for (;;)
    {
      for (unsigned long i = 0; i < 200000000; i++)
        x += i;
    }
    exit(0);
  }

  struct mlfqinfo info;
  int seen_low = 0, seen_boost = 0;
  printf("\n=== Boost test ===\n");

  for (int t = 1; t <= 35; t++)
  {
    pause(10);
    if (getmlfqinfo(pid, &info) < 0)
    {
      printf("getmlfqinfo failed\n");
      kill(pid);
      wait(0);
      return;
    }

    printf("  sample %d: level=%d sched=%d ticks=[%d %d %d %d]\n",
           t, info.level, info.times_scheduled,
           info.ticks[0], info.ticks[1], info.ticks[2], info.ticks[3]);
    if (info.level == 3)
      seen_low = 1;
    if (seen_low && info.level < 3)
    {
      seen_boost = 1;
      printf("Boost clearly observed at sample %d\n", t);
      break;
    }
  }

  if (!seen_boost)
    printf("  [Boost may have occurred between samples]\n");

  kill(pid);
  wait(0);
}

static void
run_mixed(void)
{
  int pid = fork();

  if (pid < 0)
  {
    printf("mixed: fork failed\n");
    exit(1);
  }

  if (pid == 0)
  {
    unsigned long x = 0;
    for (int k = 0; k < 200; k++)
    {
      for (unsigned long i = 0; i < 5000000; i++)
        x += i;
      getpid();
    }
    exit(0);
  }
  pause(60);
  print_info("=== Mixed workload test ===", pid);
  wait(0);
}

int main(void)
{
  printf("\n========== MLFQ Scheduler Tests ==========\n\n");

  run_cpu_bound();
  run_interactive();
  run_boost();
  run_mixed();
  printf("\n========== Tests Complete ==========\n\n");
  exit(0);
}