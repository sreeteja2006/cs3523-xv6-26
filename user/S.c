// S.c — Repeated evict/restore cycles (swap slot recycling)
//
// WHY THIS MATTERS:
//   When a page is swapped back in, its swap slot is freed
//   (swap_slot_usage[slot] = 0). That slot must then be reusable for future
//   evictions. If slots are leaked (never freed), the swap area fills up
//   and the kernel panics. This test recycles slots repeatedly.
//
//   Also tests: does the same page survive being evicted and restored
//   multiple times with different data each time?
//
// Strategy:
//   - "hot" region: 20 pages, written with a unique value each cycle
//   - "evict" region: 80 pages, touched once to fill frames and push hot pages out
//   - Cycle: write hot → touch evict (hot pages go to disk) → read hot (swap-in)
//            → verify hot values → repeat with new values
//   - 6 cycles total; each cycle uses a different salt so stale data is visible
//
// Tests:
//   [S.1–S.6] Each cycle: write hot, evict, restore, verify — FAIL on any error
//   [S.7] Total swap-out and swap-in counters are non-zero and growing
//   [S.8] No kernel panic (process survives all cycles)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE       4096
#define HOT_PAGES    20
#define EVICT_PAGES  60    // enough to push all hot pages out (20+60>64)
#define NCYCLES      6

static inline char hot_val(int page, int cycle) {
    return (char)((page * 131 + cycle * 29 + 1) & 0xFF);
}

static void run_raid_cycles(char *hot, char *evict, int raid_mode, const char *label) {
    setraidmode(raid_mode);
    printf("  --- %s ---\n", label);
    for (int cycle = 0; cycle < NCYCLES; cycle++) {
        for (int i = 0; i < HOT_PAGES; i++)
            hot[i * PGSIZE] = hot_val(i, cycle);
        for (int i = 0; i < EVICT_PAGES; i++)
            evict[i * PGSIZE] = (char)(i & 0xFF);
        int errs = 0;
        for (int i = 0; i < HOT_PAGES; i++) {
            char got = hot[i * PGSIZE];
            char exp = hot_val(i, cycle);
            if (got != exp) {
                if (errs < 5)
                    printf("  FAIL[%s] cycle %d page %d: exp=0x%x got=0x%x\n",
                           label, cycle, i,
                           (unsigned char)exp, (unsigned char)got);
                errs++;
            }
        }
        if (errs != 0) {
            printf("FAIL: %s cycle %d had %d corruption(s)\n", label, cycle, errs);
            exit(1);
        }
    }
    printf("  PASS: %s — all %d cycles intact\n\n", label, NCYCLES);
}

int main(void) {
    printf("=== TEST S: Repeated evict/restore cycles (%d cycles) ===\n\n", NCYCLES);

    char *hot   = sbrklazy(HOT_PAGES  * PGSIZE);
    char *evict = sbrklazy(EVICT_PAGES * PGSIZE);
    if (hot == SBRK_ERROR || evict == SBRK_ERROR) {
        printf("FAIL: sbrklazy\n");
        exit(1);
    }

    run_raid_cycles(hot, evict, 0, "RAID-0");
    run_raid_cycles(hot, evict, 1, "RAID-1");
    run_raid_cycles(hot, evict, 5, "RAID-5");

    setraidmode(0);

    struct vmstats s_start, s_end;
    getvmstats(getpid(), &s_start);

    for (int cycle = 0; cycle < NCYCLES; cycle++) {
        printf("[S.%d] Cycle %d ---\n", cycle + 1, cycle);

        // Step 1: Write unique values to hot pages
        for (int i = 0; i < HOT_PAGES; i++)
            hot[i * PGSIZE] = hot_val(i, cycle);

        // Step 2: Touch evict pages — this fills the frame table
        //         and forces hot pages to be evicted to disk
        for (int i = 0; i < EVICT_PAGES; i++)
            evict[i * PGSIZE] = (char)(i & 0xFF);

        // Step 3: Read hot pages back (triggers swap-in for each)
        int errs = 0;
        for (int i = 0; i < HOT_PAGES; i++) {
            char got = hot[i * PGSIZE];
            char exp = hot_val(i, cycle);
            if (got != exp) {
                if (errs < 5)
                    printf("  FAIL: cycle %d page %d: exp=0x%x got=0x%x\n",
                           cycle, i,
                           (unsigned char)exp, (unsigned char)got);
                errs++;
            }
        }

        if (errs != 0) {
            printf("FAIL: cycle %d had %d corruption(s)\n", cycle, errs);
            printf("      Likely cause: swap slot not recycled or stale data\n");
            exit(1);
        }

        struct vmstats sc;
        getvmstats(getpid(), &sc);
        printf("  PASS cycle %d: sout=%d sin=%d ev=%d res=%d\n",
               cycle,
               sc.pages_swapped_out, sc.pages_swapped_in,
               sc.pages_evicted, sc.resident_pages);
    }

    // [S.7] Stats check
    printf("\n[S.7] Checking cumulative swap stats...\n");
    getvmstats(getpid(), &s_end);
    int total_sout = s_end.pages_swapped_out - s_start.pages_swapped_out;
    int total_sin  = s_end.pages_swapped_in  - s_start.pages_swapped_in;
    int total_ev   = s_end.pages_evicted     - s_start.pages_evicted;

    printf("  Total: evicted=%d  swapped_out=%d  swapped_in=%d\n",
           total_ev, total_sout, total_sin);

    if (total_sout == 0) {
        printf("FAIL: pages_swapped_out == 0 across %d cycles\n", NCYCLES);
        exit(1);
    }
    if (total_sin == 0) {
        printf("FAIL: pages_swapped_in == 0 across %d cycles\n", NCYCLES);
        exit(1);
    }
    if (total_ev == 0) {
        printf("FAIL: pages_evicted == 0 — clock algorithm not running?\n");
        exit(1);
    }
    printf("PASS: swap activity confirmed across all %d cycles\n\n", NCYCLES);

    // [S.8] Verify final data integrity (process is alive and data is valid)
    printf("[S.8] Final hot-page integrity check (cycle %d values)...\n", NCYCLES - 1);
    int errs = 0;
    for (int i = 0; i < HOT_PAGES; i++) {
        char got = hot[i * PGSIZE];
        char exp = hot_val(i, NCYCLES - 1);
        if (got != exp) {
            if (errs < 5)
                printf("  FAIL: page %d exp=0x%x got=0x%x\n",
                       i, (unsigned char)exp, (unsigned char)got);
            errs++;
        }
    }
    if (errs != 0) {
        printf("FAIL: final hot-page check: %d error(s)\n", errs);
        exit(1);
    }
    printf("PASS: process survived all %d cycles, data intact\n\n", NCYCLES);

    printf("=== TEST S: PASS ===\n");
    exit(0);
}
