#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
  // No children test
  printf("No children test: \n");
  if(getppid() != 2) {
    printf("Error: getppid() returned %d, expected 2\n", getppid()); // we have to get two because of init and the shell, which is the parent of this process
    exit(1);
  }
  else{
    printf("getppid() returned %d as expected\n", getppid());
  }

  int num_children = 5;
  int pids[5];

  printf("\nConcurrent children test\n");
  printf("Parent PID=%d\n", getpid());

  for (int i = 0; i < num_children; i++)
  {
    int pid = fork();
    if (pid == 0)
    {
      pause(10*i); // stagger output since printf is not atomic in xv6
      printf("Child %d: PID=%d, PPID=%d\n",
             i, getpid(), getppid());
      exit(0);
    }
    pids[i] = pid;
  }

  for (int i = 0; i < num_children; i++)
    wait(0);

  for (int i = 0; i < num_children; i++)
    printf("Parent: forked child PID=%d\n", pids[i]);

  
  printf("\nRe-parenting test\n");
  int cpid = fork();
  if (cpid == 0)
  {
    pause(10); // ensure parent exits first
    printf("Child: getppid after parent exit = %d (expected 1)\n",
           getppid());
    printf("Press Enter to exit child process...\n"); // have to exit using enter because the parent exited before shell
    exit(0);
  }
  exit(0);
}
