#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  // should get 0 here
  printf("initial children = %d\n", getnumchild());

  int p1 = fork();
  // pause to make sure the child is still alive when we call getnumchild
  if(p1 == 0){
    pause(50);
    exit(0);
  }
  printf("after fork 1 children = %d\n", getnumchild());

  int p2 = fork();
  if(p2 == 0){
    pause(50);
    exit(0);
  }
  // should get 2 here
  pause(10);
  printf("alive children = %d\n", getnumchild());

  wait(0);
  wait(0);

  //zombie children should not be counted
  int pid = fork();
  if(pid == 0){
    exit(0);
  }
  wait(0);
  // should get 0 here
  pause(10);
  if(getnumchild() != 0){
    printf("Error: zombie children counted!\n");
  } else {
    printf("after reaping zombies children = %d\n", getnumchild());
  }

  // stress test
  printf("Stress testing...\n");
  for(int i=0;i<50;i++){
    int p = fork();
    if(p == 0){
      pause(10);
      exit(0);
    }
    getnumchild();
  }
  printf("after stress test children = %d\n", getnumchild());

  for(int i=0;i<50;i++){
    wait(0);
  } // clean up

  // should get 0 here
  pause(10);
  printf("after wait children = %d\n", getnumchild());

  exit(0);
}
