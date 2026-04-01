#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define ALLOC_PAGES 3500

// Ensure this is defined so the compiler knows about your custom function
char* sbrklazy(int); 

void test_basic_swap() {
    printf("\n--- TEST 1: Basic Swapping & Page Faults ---\n");
    
    printf("Allocating %d pages (Lazy Allocation)...\n", ALLOC_PAGES);
    
    // USING SBRKLAZY INSTEAD OF SBRK
    char *mem = sbrklazy(ALLOC_PAGES * PGSIZE); 
    if(mem == (char*)-1) {
        printf("sbrklazy failed!\n");
        exit(1);
    }

    printf("Writing to pages to trigger allocation and eviction...\n");
    for(int i = 0; i < ALLOC_PAGES; i++) {
        mem[i * PGSIZE] = (char)(i % 256);
    }

    printf("Reading back pages to force Swap-Ins...\n");
    int errors = 0;
    for(int i = 0; i < ALLOC_PAGES; i++) {
        if(mem[i * PGSIZE] != (char)(i % 256)) {
            errors++;
        }
    }

    if(errors > 0) {
        printf("ERROR: Data corruption detected!\n");
    } else {
        printf("SUCCESS: All data verified. Swap-In works perfectly!\n");
    }

    struct vmstats st;
    getvmstats(getpid(), &st);
    printf("[Test 1 VM Stats]\n");
    printf("Page Faults: %d\n", st.page_faults);
    printf("Pages Evicted: %d\n", st.pages_evicted);

    // Standard sbrk is fine for freeing memory (it uses SBRK_EAGER under the hood)
    sbrk(-ALLOC_PAGES * PGSIZE);
}

void test_priority_eviction() {
    printf("\n--- TEST 2: Scheduler-Aware Replacement (SC-MLFQ) ---\n");

    int pid1 = fork();
    if(pid1 == 0) {
        // USING SBRKLAZY
        char *mem = sbrklazy(1000 * PGSIZE); 
        for(int i = 0; i < 1000; i++) mem[i * PGSIZE] = 'A';

        for(int k = 0; k < 2000; k++) {
            for(volatile uint64 j = 0; j < 1000000; j++); 
        }

        pause(100); 

        struct vmstats st;
        struct mlfqinfo mlfq;
        getvmstats(getpid(), &st);
        getmlfqinfo(getpid(), &mlfq);
        printf("[Child 1 - CPU Bound] MLFQ Level: %d | Pages Evicted: %d\n", mlfq.level, st.pages_evicted);
        exit(0);
    }

    int pid2 = fork();
    if(pid2 == 0) {
        // USING SBRKLAZY
        char *mem = sbrklazy(1000 * PGSIZE); 
        for(int i = 0; i < 1000; i++) mem[i * PGSIZE] = 'B';
        
        pause(150); 

        struct vmstats st;
        struct mlfqinfo mlfq;
        getvmstats(getpid(), &st);
        getmlfqinfo(getpid(), &mlfq);
        printf("[Child 2 - I/O Bound] MLFQ Level: %d | Pages Evicted: %d\n", mlfq.level, st.pages_evicted);
        exit(0);
    }

    pause(50); 
    printf("Parent applying massive memory pressure...\n");
    
    // USING SBRKLAZY
    char *parent_mem = sbrklazy(1200 * PGSIZE); 
    for(int i = 0; i < 1200; i++) parent_mem[i * PGSIZE] = 'P';

    wait(0);
    wait(0);
}

int main(int argc, char *argv[]) {
    printf("Starting VM & SC-MLFQ Integration Tests...\n");
    test_basic_swap();
    test_priority_eviction();
    printf("\nAll tests completed successfully.\n");
    exit(0);
}