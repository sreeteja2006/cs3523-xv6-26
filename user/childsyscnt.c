#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
    int pid = fork();
    if (pid == 0)
    {
        for (int j = 0; j < 100; j++)
        {
            getpid2();
        }
        exit(0);
    }

    pause(30);
    int cnt = getchildsyscount(pid);
    printf("child %d syscall count = %d\n", pid, cnt);

    for (int i = 0; i < 3; i++)
        wait(0);

    // Invalid PID test
     cnt = getchildsyscount(12345);               // assuming 12345 is not a valid child PID
    printf("child 12345 syscall count = %d\n", cnt); // should be -1


    // No child test
    cnt = getchildsyscount(getpid2());            // passing own PID
    printf("child %d syscall count = %d\n", getpid2(), cnt); // should be -1
    exit(0);


}
