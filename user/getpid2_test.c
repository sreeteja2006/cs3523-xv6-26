#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
  //  Normal test: getpid and getpid2 should match
  int pid1 = getpid();
  int pid2 = getpid2();

  printf("Normal: getpid=%d, getpid2=%d -> %s\n",
         pid1, pid2, (pid1 == pid2) ? "PASS" : "FAIL");

  //  Stability test: repeated calls in same process should not change
  int ok = 1;
  for (int i = 0; i < 20; i++) {
    if (getpid2() != pid2) {
      ok = 0;
      break;
    }
  }
  printf("Stability (20 calls): %s\n", ok ? "PASS" : "FAIL");

  // After fork()
  int fpid = fork();
  if (fpid == 0) {
    int c1 = getpid();
    int c2 = getpid2();

    printf("Child:  getpid=%d, getpid2=%d -> %s\n",
           c1, c2, (c1 == c2) ? "PASS" : "FAIL");

    int ok2 = 1;
    for (int i = 0; i < 20; i++) {
      if (getpid2() != c2) {
        ok2 = 0;
        break;
      }
    }
    printf("Child stability (20 calls): %s\n", ok2 ? "PASS" : "FAIL");
  }
  wait(0);

  // Multiple children test 
  int N = 5;
  printf("\nMultiple children test\n");

  for (int i = 0; i < N; i++) {
    int p = fork();
    if (p == 0) {
      pause(10 * i); // stagger output since printf is not atomic in xv6
      int p1 = getpid();
      int p2 = getpid2();
      printf("Child %d: getpid=%d, getpid2=%d -> %s\n",
             i, p1, p2, (p1 == p2) ? "PASS" : "FAIL");
      exit(0);
    }
  }

  for (int i = 0; i < N; i++)
    wait(0);

  exit(0);
}
