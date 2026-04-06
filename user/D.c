#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PAGE_SIZE 4096
#define NUM_PAGES 128
#define NUM_PASSES 3

int main(void)
{
    printf("=== TEST 4: Swap Correctnes + Data Integtiry ===\n");

    int pid = getpid();
    struct vmstats st0, st1;

    printf("[4.1] Allocate %d pages for swap testing\n", NUM_PAGES);
    char *mem = sbrk(NUM_PAGES * PAGE_SIZE);
    if (mem == (char *)-1)
    {
        printf("FAIL: sbrk failed\n");
        exit(1);
    }

    getvmstats(pid, &st0);

    printf("\n[4.2] Multi-pass test: write different patterns, verify integrity\n");
    for (int pass = 0; pass < NUM_PASSES; pass++)
    {
        printf("\n--- Pass %d ---\n", pass);

        printf("Writing pattern 0x%02x...\n", (pass * 17) & 0xFF);
        for (int i = 0; i < NUM_PAGES; i++)
        {
            mem[i * PAGE_SIZE] = (char)((pass * 17 + i) & 0xFF);
        }

        printf("Reading back...\n");
        struct vmstats st_pass;
        getvmstats(pid, &st_pass);

        int errors = 0;
        for (int i = 0; i < NUM_PAGES; i++)
        {
            char expected = (char)((pass * 17 + i) & 0xFF);
            if (mem[i * PAGE_SIZE] != expected)
            {
                errors++;
            }
        }

        printf("Stats: pf=%d, ev=%d, sout=%d, sin=%d, res=%d\n",
               st_pass.page_faults, st_pass.pages_evicted, st_pass.pages_swapped_out,
               st_pass.pages_swapped_in, st_pass.resident_pages);

        if (errors == 0)
        {
            printf("PASS: pass %d data integrity verified\n", pass);
        }
        else
        {
            printf("FAIL: pass %d had %d data errors\n", pass, errors);
            exit(1);
        }
    }

    getvmstats(pid, &st1);

    printf("\n[4.3] Verify swap mechanics worked\n");
    printf("Total pages_swapped_out: %d\n", st1.pages_swapped_out);
    printf("Total pages_swapped_in: %d\n", st1.pages_swapped_in);

    if (st1.pages_swapped_out > 0)
    {
        printf("PASS: pages were swapped out\n");
    }
    else
    {
        printf("INFO: no swap-outs (all pages fit in memory)\n");
    }

    if (st1.pages_swapped_in > 0)
    {
        printf("PASS: pages were swapped in\n");
    }
    else
    {
        printf("INFO: no swap-ins (no pages were evicted)\n");
    }

    printf("\n[4.4] Final sanity check\n");
    printf("Total page_faults: %d\n", st1.page_faults);
    printf("Total pages_evicted: %d\n", st1.pages_evicted);
    printf("Current resident_pages: %d\n", st1.resident_pages);

    if (st1.page_faults >= 0 && st1.pages_evicted >= 0 && st1.resident_pages >= 0)
    {
        printf("PASS: all counters valid and non-negative\n");
    }
    else
    {
        printf("FAIL: negative counter detected\n");
        exit(1);
    }

    printf("\n=== TEST 4: PASS ===\n\n");
    exit(0);
}
