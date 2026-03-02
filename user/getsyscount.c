#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NUM_SYSCALLS 100

int main(void)
{
    int initial = getsyscount();

    // loop calling getpid2()
    for (int i = 0; i < 100; i++)
    {
        getpid2();
    }

    // measure new count
    int final = getsyscount();
    printf("Syscalls incremented = %d\n", final - initial); // should be 101

    // stress test
    printf("stress testing\n");
    for(int i=0;i<5;i++){
        initial = getsyscount();

        for (int j = 0; j < 1000; j++)
        {
            getppid();
            getpid2();
        }

        final = getsyscount();
        printf("Syscalls incremented = %d\n", final - initial); // should be 2001
    }

}
