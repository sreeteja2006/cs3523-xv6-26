// kernel/disksched.h
#ifndef DISKSCHED_H
#define DISKSCHED_H

#define MAX_DISK_REQUESTS 256

struct disk_request
{
    int blockno;        // logical block number being accessed
    int write;          // 1=write, 0=read
    void *data;         // pointer to PGSIZE data buffer
    struct proc *owner; // process that issued this request
    int done;           // 1 when completed
    int valid;          // 1 if this slot is occupied
    int mode;           // RAID mode for this request
};

void disksched_init(void);
void disksched_submit(int blockno, int write, void *data, struct proc *owner, int mode);
void disksched_run(void);
int sys_setdisksched(void);
uint64 disksched_get_reads(void);
uint64 disksched_get_writes(void);
uint64 disksched_avg_latency(void);

#endif