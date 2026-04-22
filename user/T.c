#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NPAGES 80

void write_and_verify(char *mem, int offset)
{
    // Write sequentially to force evictions
    for (int i = 0; i < NPAGES; i++)
    {
        if (i % 10 == 0)
        {
            printf("Wrote %d pages\n", i);
        }
        mem[i * PGSIZE] = (i + offset) & 0xFF;
    }

    // Read sequentially to force swap-ins
    int errs = 0;
    for (int i = 0; i < NPAGES; i++)
    {
        if (i % 10 == 0)
        {
            printf("Read %d pages\n", i);
        }
        if (mem[i * PGSIZE] != ((i + offset) & 0xFF))
        {
            errs++;
        }
    }

    if (errs)
    {
        printf("FAIL: data corrupted (%d errors)\n", errs);
        exit(1);
    }
}

int main(void)
{
    printf("=== TEST T: Disk Failure and Recovery (RAID 1 & RAID 5) ===\n\n");

    setdisksched(0); // FCFS

    char *mem = sbrklazy(NPAGES * PGSIZE);
    if ((long)mem < 0)
    {
        printf("FAIL: sbrklazy\n");
        exit(1);
    }

    // --- RAID 1: Disk 0 Failure ---
    printf("[T.1] Testing RAID 1 with Disk 0 Failed (Write & Read)\n");
    setraidmode(1);
    faildisk(0);
    write_and_verify(mem, 100);
    recovereddisk(0);
    printf("PASS: RAID 1 handled Disk 0 failure successfully.\n\n");

    // --- RAID 1: Disk 1 Failure ---
    printf("[T.2] Testing RAID 1 with Disk 1 Failed (Write & Read)\n");
    setraidmode(1);
    faildisk(1);
    write_and_verify(mem, 110);
    recovereddisk(1);
    printf("PASS: RAID 1 handled Disk 1 failure successfully.\n\n");

    // --- RAID 5: Data Disk Failure (e.g. disk 0) ---
    printf("[T.3] Testing RAID 5 with Data Disk 0 Failed (Write & Read)\n");
    setraidmode(5);
    faildisk(0);
    write_and_verify(mem, 200);
    recovereddisk(0);
    printf("PASS: RAID 5 handled Data Disk 0 failure successfully.\n\n");

    // --- RAID 5: Parity Disk Failure (e.g. disk 3) ---
    // Note: the parity disk location changes per stripe, but failing any disk
    // will test both data failure (for some stripes) and parity failure (for others).
    printf("[T.4] Testing RAID 5 with Disk 3 Failed (Write & Read)\n");
    setraidmode(5);
    faildisk(3);
    write_and_verify(mem, 230);
    recovereddisk(3);
    printf("PASS: RAID 5 handled Disk 3 failure successfully.\n\n");

    // --- RAID 5: Multiple failures (Should panic, but we only test one per the setup) ---
    // We only test graceful degradation with 1 failed disk here.

    printf("\n=== TEST T: PASS ===\n");
    exit(0);
}
