// R.c — Fork correctness when pages are swapped out
//
// WHY THIS MATTERS:
//   fork() calls uvmcopy(). For swapped-out pages, uvmcopy() does a
//   disk-to-disk copy: read parent's swap slot into a tmp buffer, write
//   tmp to child's swap slot. This path is NEVER exercised by tests A-N.
//   Bugs: wrong slot numbers, forgetting to kfree(tmp), using parent's
//   slot in child's PTE, etc.
//
// Tests:
//   [R.1] Parent allocates 80 pages (well above 64-frame limit)
//   [R.2] Parent writes unique sentinels, many pages get evicted to disk
//   [R.3] Fork — child verifies ALL its inherited pages are correct
//   [R.4] Child writes new values to its pages, verifies
//   [R.5] After child exits, parent re-reads ALL its pages — must be unchanged
//   [R.6] Parent stats: no unexpected corruption of its swap data by child

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE       4096
#define NPAGES       80     // well above 64-frame limit

// Parent uses salt=0xAA, child uses salt=0xBB for its writes
static inline char par_val(int i) { return (char)((i * 97 + 0xAA) & 0xFF); }
static inline char child_val(int i) { return (char)((i * 53 + 0xBB) & 0xFF); }

int main(void) {
    printf("=== TEST R: fork with swapped pages ===\n\n");
    setraidmode(0);

    // [R.1] Allocate pages
    char *mem = sbrklazy(NPAGES * PGSIZE);
    if (mem == SBRK_ERROR) {
        printf("FAIL: sbrklazy\n");
        exit(1);
    }

    // [R.2] Write sentinels — sequential access forces evictions
    printf("[R.2] Parent writing %d pages (forces eviction to disk)...\n", NPAGES);
    for (int i = 0; i < NPAGES; i++)
        mem[i * PGSIZE] = par_val(i);

    struct vmstats ps;
    getvmstats(getpid(), &ps);
    printf("  After write: evicted=%d sout=%d res=%d\n",
           ps.pages_evicted, ps.pages_swapped_out, ps.resident_pages);
    if (ps.pages_swapped_out == 0) {
        printf("FAIL: no swap-outs — pages did not reach disk before fork\n");
        exit(1);
    }
    printf("PASS: pages are on disk before fork\n\n");

    // [R.3] Fork
    printf("[R.3] Forking...\n");
    int child = fork();
    if (child < 0) {
        printf("FAIL: fork failed\n");
        exit(1);
    }

    if (child == 0) {
        // ---- CHILD ----
        printf("[R.3-child] Verifying inherited pages (uvmcopy disk path)...\n");
        int errs = 0;
        for (int i = 0; i < NPAGES; i++) {
            char got = mem[i * PGSIZE];
            char exp = par_val(i);
            if (got != exp) {
                if (errs < 5)
                    printf("  FAIL: child page %d: exp=0x%x got=0x%x\n",
                           i, (unsigned char)exp, (unsigned char)got);
                errs++;
            }
        }
        if (errs != 0) {
            printf("FAIL: child: %d inherited page(s) corrupted after fork\n", errs);
            exit(1);
        }
        printf("PASS: child — all %d inherited pages correct\n", NPAGES);

        // [R.4] Child writes its own values (COW-style — separate from parent)
        printf("[R.4-child] Overwriting with child values...\n");
        for (int i = 0; i < NPAGES; i++)
            mem[i * PGSIZE] = child_val(i);

        errs = 0;
        for (int i = 0; i < NPAGES; i++) {
            char got = mem[i * PGSIZE];
            char exp = child_val(i);
            if (got != exp) {
                if (errs < 5)
                    printf("  FAIL: child own-write page %d: exp=0x%x got=0x%x\n",
                           i, (unsigned char)exp, (unsigned char)got);
                errs++;
            }
        }
        if (errs != 0) {
            printf("FAIL: child own-write: %d corruption(s)\n", errs);
            exit(1);
        }
        printf("PASS: child own-write pages correct\n");

        struct vmstats cs;
        getvmstats(getpid(), &cs);
        printf("INFO child stats: faults=%d ev=%d sout=%d sin=%d res=%d\n",
               cs.page_faults, cs.pages_evicted,
               cs.pages_swapped_out, cs.pages_swapped_in,
               cs.resident_pages);
        exit(0);
    }

    // ---- PARENT: wait for child ----
    int status = -1;
    wait(&status);
    if (status != 0) {
        printf("FAIL: child exited with status %d\n", status);
        exit(1);
    }

    // [R.5] Parent re-reads its pages — must still have original values
    printf("\n[R.5] Parent re-reading all %d pages after child exited...\n", NPAGES);
    int errs = 0;
    for (int i = 0; i < NPAGES; i++) {
        char got = mem[i * PGSIZE];
        char exp = par_val(i);
        if (got != exp) {
            if (errs < 5)
                printf("  FAIL: parent page %d after fork: exp=0x%x got=0x%x\n",
                       i, (unsigned char)exp, (unsigned char)got);
            errs++;
        }
    }
    if (errs != 0) {
        printf("FAIL: parent: %d page(s) corrupted by child activity\n", errs);
        exit(1);
    }
    printf("PASS: parent data unchanged by child\n\n");

    // [R.6] Stats
    getvmstats(getpid(), &ps);
    printf("[R.6] Parent final stats: faults=%d ev=%d sout=%d sin=%d res=%d\n",
           ps.page_faults, ps.pages_evicted,
           ps.pages_swapped_out, ps.pages_swapped_in,
           ps.resident_pages);

    printf("\n=== TEST R: PASS ===\n");
    exit(0);
}
