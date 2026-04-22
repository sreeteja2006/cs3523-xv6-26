// Q.c — Exact eviction boundary test
//
// WHY THIS MATTERS:
//   Existing tests only check "evictions > 0". A bug that double-counts
//   evictions, or evicts too early (e.g., evicts at frame 32 instead of 64),
//   passes all current tests. This test pins exact expectations.
//
// Design note on [Q.1]:
//   The frame table (MAX_FRAMES=64) is SYSTEM-WIDE, shared by init, sh, and
//   this process. init+sh together occupy ~10 slots by the time Q runs.
//   Testing "0 evictions for exactly 64 pages" is therefore not meaningful
//   in isolation.  Instead, Q.1 uses MAX_FRAMES/2 = 32 pages — safely below
//   any realistic system overhead — to verify no *premature* eviction occurs
//   when ample frames are available.
//
// Tests:
//   [Q.1] Touch MAX_FRAMES/2 = 32 pages — ZERO evictions expected
//   [Q.2] Touch remaining pages up to TOTAL=96 — AT LEAST EXTRA=32 evictions required
//   [Q.3] Data integrity of all 96 pages after pressure
//   [Q.4] resident_pages never exceeds MAX_FRAMES
//   [Q.5] pages_swapped_out >= pages_evicted (every eviction hits disk)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE      4096
#define MAX_FRAMES  64
#define SAFE        (MAX_FRAMES / 2)   // 32 — well below any realistic system overhead
#define EXTRA       32
#define TOTAL       (MAX_FRAMES + EXTRA)   // 96

static inline char sentinel(int page) {
    return (char)((page * 53 + 7) & 0xFF);
}

int main(void) {
    printf("=== TEST Q: Exact eviction boundary ===\n\n");
    setraidmode(0);  // eviction boundary test runs under RAID 0

    struct vmstats s0, s1, s2, s3;
    int pid = getpid();

    // Allocate all pages lazily upfront
    char *mem = sbrklazy(TOTAL * PGSIZE);
    if (mem == SBRK_ERROR) {
        printf("FAIL: sbrklazy\n");
        exit(1);
    }

    getvmstats(pid, &s0);

    // [Q.1] Touch MAX_FRAMES/2 pages — expect ZERO evictions
    // (32 Q-pages + ~10 system pages = ~42 << 64 available frames)
    printf("[Q.1] Touching %d pages (= MAX_FRAMES/2) — zero evictions expected...\n", SAFE);
    for (int i = 0; i < SAFE; i++)
        mem[i * PGSIZE] = sentinel(i);

    getvmstats(pid, &s1);
    int ev1 = s1.pages_evicted - s0.pages_evicted;
    printf("  Evictions after touching %d pages: %d\n", SAFE, ev1);
    if (ev1 != 0) {
        printf("FAIL: expected 0 evictions for %d pages (MAX_FRAMES/2), got %d\n"
               "      Likely cause: eviction triggered too early\n", SAFE, ev1);
        exit(1);
    }
    printf("PASS: no premature eviction at MAX_FRAMES/2 (%d pages)\n\n", SAFE);

    // [Q.2] Touch all remaining pages up to TOTAL=96
    // TOTAL + system overhead >> MAX_FRAMES, so substantial evictions must occur
    printf("[Q.2] Touching remaining %d pages (total %d, well above MAX_FRAMES=%d)...\n",
           TOTAL - SAFE, TOTAL, MAX_FRAMES);
    for (int i = SAFE; i < TOTAL; i++)
        mem[i * PGSIZE] = sentinel(i);

    getvmstats(pid, &s2);
    int total_ev = s2.pages_evicted - s0.pages_evicted;
    printf("  Total evictions for %d pages: %d (expected >= %d)\n",
           TOTAL, total_ev, EXTRA);
    if (total_ev < EXTRA) {
        printf("FAIL: expected >= %d evictions for %d pages, got %d\n"
               "      Likely cause: eviction not triggering or under-counting\n",
               EXTRA, TOTAL, total_ev);
        exit(1);
    }
    printf("PASS: sufficient evictions (%d) for %d-page working set\n\n",
           total_ev, TOTAL);

    // [Q.3] Data integrity of all TOTAL pages
    printf("[Q.3] Verifying data integrity of all %d pages...\n", TOTAL);
    int errs = 0;
    for (int i = 0; i < TOTAL; i++) {
        char got = mem[i * PGSIZE];
        char exp = sentinel(i);
        if (got != exp) {
            if (errs < 5)
                printf("  FAIL: page %d expected 0x%x got 0x%x\n",
                       i, (unsigned char)exp, (unsigned char)got);
            errs++;
        }
    }
    if (errs != 0) {
        printf("FAIL: %d data corruption(s) across %d pages\n", errs, TOTAL);
        exit(1);
    }
    printf("PASS: all %d pages intact\n\n", TOTAL);

    // [Q.4] resident_pages should not exceed MAX_FRAMES
    getvmstats(pid, &s3);
    printf("[Q.4] Checking resident_pages <= MAX_FRAMES...\n");
    printf("  resident_pages = %d, MAX_FRAMES = %d\n",
           s3.resident_pages, MAX_FRAMES);
    if (s3.resident_pages > MAX_FRAMES) {
        printf("FAIL: resident_pages (%d) > MAX_FRAMES (%d) — frame table over-committed\n",
               s3.resident_pages, MAX_FRAMES);
        exit(1);
    }
    printf("PASS\n\n");

    // [Q.5] pages_swapped_out >= evictions (every eviction must have hit disk)
    printf("[Q.5] pages_swapped_out >= pages_evicted?\n");
    int sout = s3.pages_swapped_out - s0.pages_swapped_out;
    printf("  evicted=%d  swapped_out=%d\n", total_ev, sout);
    if (sout < total_ev) {
        printf("FAIL: pages_swapped_out (%d) < evictions (%d)\n"
               "      Some evictions did not write to disk!\n",
               sout, total_ev);
        exit(1);
    }
    printf("PASS: every eviction wrote to disk\n\n");

    printf("=== TEST Q: PASS ===\n");
    exit(0);
}
