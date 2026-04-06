#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
    printf("=== TEST 1: getvmstats Syscall ===\n");

    struct vmstats st;
    int pid = getpid();

    printf("[1.1] Valid PID returns 0\n");
    if (getvmstats(pid, &st) != 0)
    {
    printf("FAIL: getvmstats(self) returned non-zero lol\n");
    exit(1);
    }
    printf("PASS: getvmstats(pid=%d) succeeded\n", pid);

    printf("\n[1.2] Initial counters are non-negetive\n");
    if (st.page_faults < 0 || st.pages_evicted < 0 ||
        st.pages_swapped_in < 0 || st.pages_swapped_out < 0 ||
        st.resident_pages < 0)
    {
    printf("FAIL: negative counter detected\n");
    exit(1);
    }
    printf("PASS: All counters >= 0: pf=%d, ev=%d, si=%d, so=%d, res=%d\n",
           st.page_faults, st.pages_evicted, st.pages_swapped_in,
           st.pages_swapped_out, st.resident_pages);

    printf("\n[1.3] Invalid PIDs return -1 (test invalid pids)\n");
    if (getvmstats(-1, &st) != -1)
    {
    printf("FAIL: PID=-1 should return -1\n");
    exit(1);
    }
    printf("PASS: getvmstats(-1) returned -1\n");

    if (getvmstats(99999, &st) != -1)
    {
    printf("FAIL: non-existent PID should return -1\n");
    exit(1);
    }
    printf("PASS: getvmstats(99999) returned -1\n");

    printf("\n[1.4] Parent process stats accessible\n");
    int ppid = getppid();
    struct vmstats pst;
    if (getvmstats(ppid, &pst) != 0)
    {
    printf("FAIL: cannot get parent stats\n");
    exit(1);
    }
    printf("PASS: Parent stats: pf=%d, ev=%d, si=%d, so=%d, res=%d\n",
           pst.page_faults, pst.pages_evicted, pst.pages_swapped_in,
           pst.pages_swapped_out, pst.resident_pages);

    printf("\n[1.5] Counters monotonically increase\n");
    struct vmstats before, after;
    getvmstats(pid, &before);

    char *page1 = sbrk(4096);
    if (page1 == (char *)-1)
    {
    printf("FAIL: sbrk failed\n");
    exit(1);
    }
    page1[0] = 'x';

    getvmstats(pid, &after);
    if (after.page_faults < before.page_faults ||
        after.resident_pages < before.resident_pages)
    {
    printf("FAIL: counters decreased\n");
    exit(1);
    }
    printf("PASS: page_faults %d->%d, resident_pages %d->%d\n",
           before.page_faults, after.page_faults,
           before.resident_pages, after.resident_pages);

    printf("\n=== TEST 1: PASS ===\n\n");
    exit(0);
}
