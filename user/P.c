#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE   4096
#define NPAGES   128

static inline char pat(int page, int byte_off, int salt) {
    return (char)(((page * 251) ^ (byte_off * 17) ^ salt) & 0xFF);
}

static int verify_all(char *mem, int salt, const char *ctx) {
    int errs = 0;
    for (int i = 0; i < NPAGES; i++) {
        for (int j = 0; j < PGSIZE; j++) {
            char got = mem[i * PGSIZE + j];
            char exp = pat(i, j, salt);
            if (got != exp) {
                if (errs < 5)
                    printf("  FAIL[%s]: page %d byte %d exp=0x%x got=0x%x\n",
                           ctx, i, j, (unsigned char)exp, (unsigned char)got);
                errs++;
            }
        }
    }
    return errs;
}

static void run_pass(char *mem, int salt, int raid, const char *label) {
    setraidmode(raid);
    printf("  [%s] Writing %d pages x %d bytes...\n", label, NPAGES, PGSIZE);
    for (int i = 0; i < NPAGES; i++)
        for (int j = 0; j < PGSIZE; j++)
            mem[i * PGSIZE + j] = pat(i, j, salt);
    printf("  [%s] Verifying...\n", label);
    int errs = verify_all(mem, salt, label);
    if (errs != 0) {
        printf("FAIL: %d byte corruption(s) under %s\n", errs, label);
        exit(1);
    }
    printf("  PASS: all %d bytes correct under %s\n\n", NPAGES * PGSIZE, label);
}

int main(void) {
    printf("=== TEST P: Full-page byte-level integrity under all RAID modes ===\n");
    printf("Pages: %d  Bytes per page: %d  Total: %d bytes\n\n",
           NPAGES, PGSIZE, NPAGES * PGSIZE);

    char *mem = sbrklazy(NPAGES * PGSIZE);
    if (mem == SBRK_ERROR) { printf("FAIL: sbrklazy\n"); exit(1); }

    run_pass(mem, 0xAB, 0, "RAID-0 salt=0xAB");
    run_pass(mem, 0xCD, 1, "RAID-1 salt=0xCD");
    run_pass(mem, 0xEF, 5, "RAID-5 salt=0xEF");

    setraidmode(0);

    struct vmstats st;
    getvmstats(getpid(), &st);
    printf("[P.4] Stats: evicted=%d sout=%d sin=%d res=%d\n",
           st.pages_evicted, st.pages_swapped_out,
           st.pages_swapped_in, st.resident_pages);

    if (st.pages_swapped_out == 0) {
        printf("FAIL: no swap-outs recorded\n"); exit(1);
    }
    if (st.pages_swapped_in == 0) {
        printf("FAIL: no swap-ins recorded\n"); exit(1);
    }



    printf("PASS: eviction and swap stats look healthy\n\n");
    printf("=== TEST P: PASS ===\n");
    exit(0);
}
