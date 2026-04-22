#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#include "proc.h"
#include "disksched.h"

struct
{
    struct disk_request requests[MAX_DISK_REQUESTS];
    int count;
    int policy;
    struct spinlock lock;
    int head;
    uint64 total_latency;
    uint64 total_requests;
    uint64 total_reads;
    uint64 total_writes;
    int in_flight;
} disksched;

void disksched_init(void)
{
    initlock(&disksched.lock, "disksched");
    disksched.count = 0;
    disksched.policy = DISK_SCHED_FCFS;
    disksched.head = 0;
    disksched.total_latency = 0;
    disksched.total_requests = 0;
    disksched.total_reads = 0;
    disksched.total_writes = 0;
    disksched.in_flight = 0;
    for (int i = 0; i < MAX_DISK_REQUESTS; i++)
        disksched.requests[i].valid = 0;
}

static int request_distance(int blockno)
{
    int dist = blockno - disksched.head;
    if (dist < 0)
        dist = -dist;
    return dist;
}

static int request_priority(struct disk_request *req)
{
    return req->owner ? req->owner->level : 0x7fffffff;
}

static int first_valid_request(void)
{
    for (int i = 0; i < MAX_DISK_REQUESTS; i++)
    {
        if (disksched.requests[i].valid)
            return i;
    }
    return -1;
}

static int find_empty_slot(void)
{
    for (int i = 0; i < MAX_DISK_REQUESTS; i++)
    {
        if (!disksched.requests[i].valid)
            return i;
    }
    return -1;
}

// Pick the next request to service based on the scheduling policy.
// Must be called with disksched.lock held.
static int pick_next(void)
{
    if (disksched.count == 0)
        return -1;

    if (disksched.policy == DISK_SCHED_FCFS)
        return first_valid_request();

    // SSTF: shortest seek distance first. Break ties by process priority.
    int best = -1;
    int best_dist = 0x7fffffff;
    int best_priority = 0x7fffffff;
    for (int i = 0; i < MAX_DISK_REQUESTS; i++)
    {
        if (!disksched.requests[i].valid)
            continue;

        int dist = request_distance(disksched.requests[i].blockno);
        int priority = request_priority(&disksched.requests[i]);

        if (dist < best_dist || (dist == best_dist && priority < best_priority))
        {
            best_dist = dist;
            best_priority = priority;
            best = i;
        }
    }
    return best;
}

// Execute exactly one disk request from the queue.
// Must be called WITHOUT disksched.lock held (bread/bwrite sleep).
static void run_one(void)
{
    acquire(&disksched.lock);
    if (disksched.count == 0)
    {
        release(&disksched.lock);
        return;
    }
    int pick = pick_next();
    if (pick < 0)
    {
        release(&disksched.lock);
        return;
    }
    struct disk_request req = disksched.requests[pick];
    disksched.requests[pick].valid = 0;
    disksched.count--;
    disksched.in_flight++;

    int dist = request_distance(req.blockno);
    disksched.head = req.blockno;
    disksched.total_latency += dist + ROTATIONAL_DELAY;
    disksched.total_requests++;
    if (req.write)
        disksched.total_writes++;
    else
        disksched.total_reads++;
    release(&disksched.lock);

    if (req.write)
        raid_write(req.blockno, req.data, req.mode);
    else
        raid_read(req.blockno, req.data, req.mode);

    acquire(&disksched.lock);
    disksched.in_flight--;
    release(&disksched.lock);
}

// Submit a single disk block request and immediately service one request
// from the queue (which may be this one or a previously queued one).
// blockno: the disk block number to read/write
// write:   1 = write to disk, 0 = read from disk
// data:    pointer to exactly BSIZE bytes of kernel memory
// owner:   the process issuing the request (may be 0 for kernel)
void disksched_submit(int blockno, int write, void *data, struct proc *owner, int mode)
{
    acquire(&disksched.lock);
    if (disksched.count == MAX_DISK_REQUESTS)
    {
        release(&disksched.lock);
        panic("disksched: queue full");
    }

    int idx = find_empty_slot();
    if (idx == -1)
    {
        release(&disksched.lock);
        panic("disksched: no empty slot");
    }

    disksched.requests[idx].blockno = blockno;
    disksched.requests[idx].write = write;
    disksched.requests[idx].data = data;
    disksched.requests[idx].owner = owner;
    disksched.requests[idx].done = 0;
    disksched.requests[idx].valid = 1;
    disksched.requests[idx].mode = mode;
    disksched.count++;
    release(&disksched.lock);

    run_one();
}

// Drain all pending requests from the queue.
// Can be called from a background process to catch up on queued I/O.
void disksched_run(void)
{
    int loops = 0;
    for (;;)
    {
        acquire(&disksched.lock);
        int done = (disksched.count == 0 && disksched.in_flight == 0);
        release(&disksched.lock);
        if (done)
            break;
        run_one();
        if (++loops % 100000 == 0)
            printf("disksched_run infinite loop? count=%d in_flight=%d\n", disksched.count, disksched.in_flight);
    }
}

int disksched_get_policy(void)
{
    return disksched.policy;
}

void disksched_set_policy(int policy)
{
    acquire(&disksched.lock);
    disksched.policy = policy;
    release(&disksched.lock);
}

uint64 disksched_avg_latency(void)
{
    if (disksched.total_requests == 0)
        return 0;
    return disksched.total_latency / disksched.total_requests;
}

uint64 disksched_get_reads(void)
{
    return disksched.total_reads;
}

uint64 disksched_get_writes(void)
{
    return disksched.total_writes;
}

// Syscall handler: setdisksched(int policy)
// policy 0 = FCFS, 1 = SSTF
// Returns 0 on success, -1 on invalid policy.
int sys_setdisksched(void)
{
    int policy;
    argint(0, &policy);
    if (policy != DISK_SCHED_FCFS && policy != DISK_SCHED_SSTF)
        return -1;
    disksched_set_policy(policy);
    return 0;
}
