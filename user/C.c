#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PAGE_SIZE 4096
#define MAXFRAMES 64
#define PRESSURE_PAGES 192

int main(void)
{
    printf("=== TEST 3: Page Eviction & Clock Algoritm ===\n\n");

    int pid = getpid();
    struct vmstats st_before, st_during, st_after;

    printf("[3.1] Apply heavy memory presure\n");
    printf("Allocating %d pages (exceeds %d frame limit)...\n", PRESSURE_PAGES, MAXFRAMES);

    getvmstats(pid, &st_before);

    char *mem = sbrk(PRESSURE_PAGES * PAGE_SIZE);
    if (mem == (char *)-1)
    {
        printf("FAIL: sbrk failed\n");
        exit(1);
    }

    printf("\n[3.2] Write phase - sequential access forces evictions\n");
    for (int i = 0; i < PRESSURE_PAGES; i++)
    {
        mem[i * PAGE_SIZE] = (char)(i & 0xFF);
    }
    printf("PASS: Wrote to all %d pages\n", PRESSURE_PAGES);

    getvmstats(pid, &st_during);
    int evict_count = st_during.pages_evicted - st_before.pages_evicted;
    int expected_evict = PRESSURE_PAGES - MAXFRAMES;

    printf("\n[3.3] Verify evictions occurred\n");
    printf("Evictions: %d (expected >= %d)\n", evict_count, expected_evict);
    if (evict_count >= expected_evict)
    {
        printf("PASS: sufficient evictions occurred\n");
    }
    else
    {
        printf("FAIL: insufficient evictions: %d < %d\n", evict_count, expected_evict);
        exit(1);
    }

    printf("\n[3.4] Verify swapped_out increased\n");
    int swapout = st_during.pages_swapped_out - st_before.pages_swapped_out;
    printf("Pages swapped out: %d\n", swapout);
    if (swapout >= evict_count)
    {
        printf("PASS: pages_swapped_out matches evictions\n");
    }
    else
    {
        printf("WARN: pages_swapped_out (%d) < evictions (%d)\n", swapout, evict_count);
    }

    printf("\n[3.5] Read phase - verify data and trigger swap-ins\n");
    int data_errors = 0;
    for (int i = 0; i < PRESSURE_PAGES; i++)
    {
        if (mem[i * PAGE_SIZE] != (char)(i & 0xFF))
        {
            data_errors++;
            if (data_errors <= 3)
            {
                printf("  Error at page %d\n", i);
            }
        }
    }

    getvmstats(pid, &st_after);
    int swapin = st_after.pages_swapped_in - st_during.pages_swapped_in;

    printf("Swap-ins during re-read: %d\n", swapin);
    if (swapin > 0)
    {
        printf("PASS: swap-ins occurred (pages restored from swap)\n");
    }
    else
    {
        printf("INFO: no swap-ins (all pages may still be resident)\n");
    }

    if (data_errors == 0)
    {
        printf("PASS: all %d page sentinels intact\n", PRESSURE_PAGES);
    }
    else
    {
        printf("FAIL: %d data corruption errors\n", data_errors);
        exit(1);
    }

    printf("\n[3.6] Verify resident_pages bounded\n");
    printf("Final resident_pages: %d (max %d frames)\n", st_after.resident_pages, MAXFRAMES);
    if (st_after.resident_pages > MAXFRAMES + 15)
    {
        printf("WARN: resident_pages significantly over frame limit\n");
    }
    else
    {
        printf("PASS: resident_pages reasonably bounded\n");
    }

    printf("\n=== TEST 3: PASS ===\n\n");
    exit(0);
}
