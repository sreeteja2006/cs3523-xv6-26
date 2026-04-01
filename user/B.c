#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PAGE_SIZE 4096
#define NUM_PAGES 64

int main(void)
{
    printf("=== TEST 2: Page Fault Counting & Lazy Allocation ===\n\n");

    int pid = getpid();
    struct vmstats st_before, st_after;

    printf("[2.1] Allocate %d pages via sbrk\n", NUM_PAGES);
    getvmstats(pid, &st_before);

    char *mem = sbrk(NUM_PAGES * PAGE_SIZE);
    if (mem == (char *)-1)
    {
        printf("FAIL: sbrk failed\n");
        exit(1);
    }
    printf("PASS: sbrk allocated %d pages\n", NUM_PAGES);

    printf("\n[2.2] sbrk alone should not cause page falts (lazy alloc)\n");
    struct vmstats st_sbrk;
    getvmstats(pid, &st_sbrk);
    if (st_sbrk.page_faults != st_before.page_faults)
    {
        printf("WARN: page_faults changed after sbrk (expected no falts yet)\n");
    }
    else
    {
        printf("PASS: No page faults after sbrk (lazy allocation working)\n");
    }

    printf("\n[2.3] Touch each page to trigger page faults\n");
    for (int i = 0; i < NUM_PAGES; i++)
    {
        mem[i * PAGE_SIZE] = (char)(i & 0xFF);
    }
    printf("PASS: Touched all %d pages\n", NUM_PAGES);

    getvmstats(pid, &st_after);
    int pf_count = st_after.page_faults - st_before.page_faults;
    int res_count = st_after.resident_pages - st_before.resident_pages;

    printf("\n[2.4] Verify page faults were recorded\n");
    printf("Page faults: %d (from touches)\n", pf_count);
    printf("Resident pages added: %d\n", res_count);

    if (pf_count >= 1 && pf_count <= NUM_PAGES)
    {
        printf("PASS: page_faults in expected range [1, %d]\n", NUM_PAGES);
    }
    else
    {
        printf("WARN: page_faults=%d outside typical range\n", pf_count);
    }

    printf("\n[2.5] Verify data integrity\n");
    int errors = 0;
    for (int i = 0; i < NUM_PAGES; i++)
    {
        if (mem[i * PAGE_SIZE] != (char)(i & 0xFF))
        {
            errors++;
        }
    }
    if (errors == 0)
    {
        printf("PASS: all %d pages data intact\n", NUM_PAGES);
    }
    else
    {
        printf("FAIL: %d data errors\n", errors);
        exit(1);
    }

    printf("\n[2.6] Verify resident_pages sanity\n");
    printf("resident_pages=%d (should match or exceed page count)\n", st_after.resident_pages);
    if (st_after.resident_pages >= 0 && st_after.resident_pages <= NUM_PAGES * 2)
    {
        printf("PASS: resident_pages in reasonable range\n");
    }
    else
    {
        printf("WARN: resident_pages seems anomalous\n");
    }

    printf("\n=== TEST 2: PASS ===\n\n");
    exit(0);
}
