#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PAGE_SIZE 4096
#define CHILD_PAGES 100

int main(void)
{
    printf("=== TEST 5: Multi-Proces Isolation + vmstats ===\n\n");

    int pid = getpid();
    struct vmstats parent_before, parent_after;
    struct vmstats child_stats;

    printf("[5.1] Test vmstats isolation between parent and child\n");
    getvmstats(pid, &parent_before);
    printf("Parent (PID %d) initial: pf=%d, ev=%d, res=%d\n",
           pid, parent_before.page_faults, parent_before.pages_evicted,
           parent_before.resident_pages);

    int child = fork();
    if (child < 0)
    {
        printf("FAIL: fork failed\n");
        exit(1);
    }

    if (child == 0)
    {
        printf("\n[5.2] Child process allocates and uses memory\n");
        int child_pid = getpid();
        printf("Child PID: %d\n", child_pid);

        getvmstats(child_pid, &child_stats);
        printf("Child initial: pf=%d, ev=%d, res=%d\n",
               child_stats.page_faults, child_stats.pages_evicted,
               child_stats.resident_pages);

        if (child_stats.page_faults != 0 || child_stats.pages_evicted != 0)
        {
            printf("FAIL: child should start with fresh counters\n");
            exit(1);
        }
        printf("PASS: child has fresh/zero counters\n");

        printf("\nChild allocating %d pages...\n", CHILD_PAGES);
        char *mem = sbrk(CHILD_PAGES * PAGE_SIZE);
        if (mem == (char *)-1)
        {
            printf("FAIL: sbrk in child\n");
            exit(1);
        }

        for (int i = 0; i < CHILD_PAGES; i++)
        {
            mem[i * PAGE_SIZE] = (char)(i & 0xFF);
        }

        getvmstats(child_pid, &child_stats);
        printf("Child after allocation: pf=%d, ev=%d, res=%d\n",
               child_stats.page_faults, child_stats.pages_evicted,
               child_stats.resident_pages);

        if (child_stats.page_faults > 0 || child_stats.resident_pages > 0)
        {
            printf("PASS: child stats updated by its memory activity\n");
        }
        else
        {
            printf("WARN: child stats not updated\n");
        }

        exit(0);
    }

    wait(0);

    printf("\n[5.3] Parent stats should not be affected by child\n");
    getvmstats(pid, &parent_after);
    printf("Parent after child: pf=%d, ev=%d, res=%d\n",
           parent_after.page_faults, parent_after.pages_evicted,
           parent_after.resident_pages);

    int pf_delta = parent_after.page_faults - parent_before.page_faults;
    int ev_delta = parent_after.pages_evicted - parent_before.pages_evicted;

    if (pf_delta == 0 && ev_delta == 0)
    {
        printf("PASS: parent stats unchanged by child activity\n");
    }
    else
    {
        printf("INFO: parent stats changed (pf_delta=%d, ev_delta=%d)\n",
               pf_delta, ev_delta);
    }

    printf("\n[5.4] Test invalid PIDs\n");
    if (getvmstats(-1, &child_stats) != -1)
    {
        printf("FAIL: PID -1 should return -1\n");
        exit(1);
    }
    printf("PASS: getvmstats(-1) returned -1\n");

    if (getvmstats(9999999, &child_stats) != -1)
    {
        printf("FAIL: non-existent PID should return -1\n");
        exit(1);
    }
    printf("PASS: getvmstats(non-existent) returned -1\n");

    printf("\n[5.5] Verify parent can access parent's own stats\n");
    if (getvmstats(pid, &parent_after) != 0)
    {
        printf("FAIL: cannot get own stats\n");
        exit(1);
    }
    printf("PASS: parent stats still accessible\n");
    printf("Final parent: pf=%d, ev=%d, res=%d\n",
           parent_after.page_faults, parent_after.pages_evicted,
           parent_after.resident_pages);

    printf("\n=== TEST 5: PASS ===\n\n");
    exit(0);
}
