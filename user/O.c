#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE  4096
#define NPAGES  80

static inline char sentinel(int page, int salt) {
    return (char)((page * 37 + salt + 1) & 0xFF);
}

static void write_pages(char *mem, int n, int salt) {
    for (int i = 0; i < n; i++)
        mem[i * PGSIZE] = sentinel(i, salt);
}

static int verify_pages(char *mem, int n, int salt, const char *ctx) {
    int errs = 0;
    for (int i = 0; i < n; i++) {
        char got = mem[i * PGSIZE];
        char exp = sentinel(i, salt);
        if (got != exp) {
            if (errs < 5)
                printf("  FAIL[%s]: page %d expected 0x%x got 0x%x\n",
                       ctx, i, (unsigned char)exp, (unsigned char)got);
            errs++;
        }
    }
    return errs;
}

int main(void) {
    printf("=== TEST O: setdisksched + setraidmode strict ===\n\n");

    // [O.1] Invalid disk sched policy
    printf("[O.1] setdisksched(-1) must return -1\n");
    if (setdisksched(-1) != -1) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.2]
    printf("[O.2] setdisksched(2) must return -1\n");
    if (setdisksched(2) != -1) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.3]
    printf("[O.3] setdisksched(0) FCFS must return 0\n");
    if (setdisksched(0) != 0) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.4]
    printf("[O.4] setdisksched(1) SSTF must return 0\n");
    if (setdisksched(1) != 0) { printf("FAIL\n"); exit(1); }
    setdisksched(0);
    printf("PASS\n\n");

    // [O.5] Invalid RAID mode
    printf("[O.5] setraidmode(-1) must return -1\n");
    if (setraidmode(-1) != -1) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.6]
    printf("[O.6] setraidmode(3) must return -1\n");
    if (setraidmode(3) != -1) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.7]
    printf("[O.7] setraidmode(0) RAID 0 must return 0\n");
    if (setraidmode(0) != 0) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.8]
    printf("[O.8] setraidmode(1) RAID 1 must return 0\n");
    if (setraidmode(1) != 0) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.9]
    printf("[O.9] setraidmode(5) RAID 5 must return 0\n");
    if (setraidmode(5) != 0) { printf("FAIL\n"); exit(1); }
    setraidmode(0);
    printf("PASS\n\n");

    char *mem = sbrklazy(NPAGES * PGSIZE);
    if (mem == SBRK_ERROR) { printf("FAIL: sbrklazy\n"); exit(1); }

    // [O.10] Data integrity under FCFS + RAID 0
    printf("[O.10] FCFS + RAID 0 (salt=10)\n");
    setdisksched(0); setraidmode(0);
    write_pages(mem, NPAGES, 10);
    if (verify_pages(mem, NPAGES, 10, "FCFS+R0") != 0) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.11] SSTF + RAID 1
    printf("[O.11] SSTF + RAID 1 (salt=20)\n");
    setdisksched(1); setraidmode(1);
    write_pages(mem, NPAGES, 20);
    if (verify_pages(mem, NPAGES, 20, "SSTF+R1") != 0) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    // [O.12] FCFS + RAID 5
    printf("[O.12] FCFS + RAID 5 (salt=30)\n");
    setdisksched(0); setraidmode(5);
    write_pages(mem, NPAGES, 30);
    if (verify_pages(mem, NPAGES, 30, "FCFS+R5") != 0) { printf("FAIL\n"); exit(1); }
    printf("PASS\n\n");

    setraidmode(0);

    struct vmstats st;
    getvmstats(getpid(), &st);
    printf("Final stats: faults=%d evicted=%d sout=%d sin=%d res=%d\n",
           st.page_faults, st.pages_evicted,
           st.pages_swapped_out, st.pages_swapped_in,
           st.resident_pages);

    if (st.pages_swapped_out == 0) {
        printf("FAIL: no swap-outs recorded\n"); exit(1);
    }
    if (st.pages_swapped_in == 0) {
        printf("FAIL: no swap-ins recorded\n"); exit(1);
    }

    printf("\n=== TEST O: PASS ===\n");
    exit(0);
}
