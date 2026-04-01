# CS3523 Programming Assignment - Page Replacement (PA-3)

**Date:** April 2026

**Course:** CS3523 - Operating Systems II

---

## Introduction

This assignment adds memory management to xv6. When memory fills up (64 pages max), extra pages get written to disk (swap). The system tracks memory stats like page faults, evictions, and swaps per process. There are 5 test programs to check that everything works.

---

## Files Modified

- **kernel/vm.c**: Memory page tracking, page eviction, memory allocation, page fault handling
- **kernel/trap.c**: Catches page faults and calls vmfault()
- **kernel/proc.c**: Updates process memory counters
- **kernel/proc.h**: Added memory stats to struct proc
- **kernel/types.h**: Defined memory stats struct
- **kernel/defs.h**: Function declarations
- **kernel/syscall.h**: Syscall number for getvmstats
- **kernel/syscall.c**: Syscall routing
- **kernel/sysproc.c**: getvmstats implementation
- **user/user.h**: User-space syscall declaration
- **user/A.c, B.c, C.c, D.c, E.c**: Test programs

---

## Part A: Kernel Data Structures

### Frame Table (kernel/vm.c, lines 18-28)

```c
struct frame_entry {
  uint64 va;           // Virtual address
  void *pa;            // Memory address
  pagetable_t owner;   // Which process owns this page
  int ref_bit;         // Was this page recently used?
  int in_use;          // Is this slot used?
};

static struct frame_entry frame_table[MAX_FRAMES];  // 64 entries
int swap_slot_usage[SWAP_SLOTS];                     // 4096 bits
```

**Frame Table Globals** (kernel/vm.c):

- `int active_user_pages`: How many pages are currently in use (0 to 64)
- `struct spinlock frametable_lock`: Lock to prevent concurrent access
- `static int clock_hand`: Current position for scanning pages to evict

### Process Counters (kernel/proc.h, lines 126-130)

Added to `struct proc`:

```c
int page_faults;        // Count of page faults
int pages_evicted;      // Count of pages moved to disk
int pages_swapped_in;   // Count of pages restored from disk
int pages_swapped_out;  // Count of pages written to disk
int resident_pages;     // Pages currently in memory (0-64)
```

### vmstats Structure (kernel/types.h)

```c
typedef struct vmstats {
  int page_faults;
  int pages_evicted;
  int pages_swapped_in;
  int pages_swapped_out;
  int resident_pages;
} vmstats;
```

---

## Part B: getvmstats - Memory Stats Syscall

### How It's Registered

- Given syscall number 32 in kernel/syscall.h
- Added to syscall routing table in kernel/syscall.c

### Implementation (kernel/sysproc.c, lines 262-290)

Basically:

1. Get the process ID from the argument
2. Find that process in the process table
3. Copy its memory stats
4. Send the stats to user space
5. Return success (0) or failure (-1)

```c
sys_getvmstats(void) {
  int pid;
  uint64 uaddr;
  struct proc *p;
  vmstats info;

  argint(0, &pid);         // Read process ID argument
  argaddr(1, &uaddr);      // Read pointer argument

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED) {
      // Copy counters from kernel space
      info.page_faults = p->page_faults;
      info.pages_evicted = p->pages_evicted;
      info.pages_swapped_in = p->pages_swapped_in;
      info.pages_swapped_out = p->pages_swapped_out;
      info.resident_pages = p->resident_pages;
      release(&p->lock);

      // Copy to user space
      if (copyout(myproc()->pagetable, uaddr, (char *)&info, sizeof(info)) < 0)
        return -1;

      return 0;  // Success
    }
    release(&p->lock);
  }

  return -1;  // PID not found
}
```

- Returns 0 if found
- Returns -1 if process doesn't exist
- Holds a lock while copying (safe from concurrent access)

---

## Part C: Detecting Page Faults

### trap.c - When a Page Fault Happens

When the CPU tries to access a page that's not in memory:

```c
else if ((r_scause() == 15 || r_scause() == 13 || r_scause() == 12) &&
         vmfault(p->pagetable, r_stval(), 0) != 0)
{
  // Handle page fault
}
```

**Fault Types**:

- 12: Reading from invalid page
- 13: Loading from invalid page  
- 15: Storing to invalid page

When a fault happens:

- Call vmfault() to handle it
- vmfault() returns success/failure

---

## Part D: Lazy Allocation

### How sbrk() Works

When user calls sbrk(size):

1. Only say "you have that much memory"
2. Don't actually allocate memory yet
3. No page faults yet

### First Access Causes Fault

When user touches a page for first time:

1. CPU throws a page fault exception
2. trap.c catches it and calls vmfault()
3. vmfault() allocates a real memory frame
4. Returns the memory address
5. User code continues normally

---

## Part E: Evicting Pages to Disk

### When Does Eviction Happen?

When we run out of memory frames (64 max), we need to:

1. Pick a page to remove from memory
2. Write it to disk (swap space)
3. Free up that memory frame
4. Use the frame for new page

### Clock Algorithm - How We Pick a Victim

The algorithm scans pages in a circle:

1. **Start scanning** from where we left off last time
2. **Check each page**:
   - If it's been used recently (ref_bit=1): skip it, clear the bit
   - If it hasn't been used (ref_bit=0): evict this one!
3. **Prefer low priority processes**: Processes with low SC-MLFQ priority are evicted first
4. **Write to disk** and mark memory slot as free
5. **Update counters**: Mark page as swapped out

### How We Write to Disk

```c
static inline char *swap_slot_addr(int slot) {
  return (char *)(USABLE_PHYSTOP + (uint64)slot * PGSIZE);
}
```

Disk space is located after regular memory. Each page gets saved at a fixed location.

### Update Counters After Eviction

When a page is evicted:

```c
void increment_evictions(pagetable_t pt) {
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state != UNUSED && p->pagetable == pt) {
      acquire(&p->lock);
      p->pages_evicted++;      // Count it
      p->pages_swapped_out++;  // Count it
      p->resident_pages--;     // Decrease resident count
      release(&p->lock);
      break;
    }
  }
}
```

---

## Part F: Page Fault Handling - vmfault()

### vmfault() Function (kernel/vm.c, lines 680-738)

**Input**: pagetable, virtual address  
**Output**: Physical address of page (0 on error)

**Case 1: VA Outside Heap**

```c
if (va >= p->sz)
  return 0;  // Not a fault, shouldn't happen
```

**Case 2: Page Already Mapped**

```c
if (ismapped(pagetable, va))
  return PTE2PA(*walk(pagetable, va, 0));  // Already have it
```

**Case 3: Page in Swap (PTE_V_SWAP)**

```c
if (pte != 0 && (*pte & PTE_V_SWAP)) {
  int swap_slot = PTE2PA(*pte) / PGSIZE;  // Extract swap slot
  
  // Allocate fresh frame
  void *new_pa = alloc_user_page(pagetable, va);
  
  // Restore from swap
  memmove(new_pa, swap_slot_addr(swap_slot), PGSIZE);
  
  // Update PTE
  *pte = PA2PTE((uint64)new_pa) | flags | PTE_V;
  
  // Free swap slot
  swap_slot_usage[swap_slot] = 0;
  
  // Update counters
  acquire(&p->lock);
  p->resident_pages++;
  p->pages_swapped_in++;  // Count swap-in
  release(&p->lock);
  
  return (uint64)new_pa;
}
```

**Case 4: Fresh Allocation**

```c
// Increment fault counter
acquire(&p->lock);
p->page_faults++;
release(&p->lock);

// Allocate frame
mem = (uint64)alloc_user_page(pagetable, va);

// Zero the page
memset((void *)mem, 0, PGSIZE);

// Add page table entry (may trigger eviction)
while (!ismapped(p->pagetable, va) &&
       mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0)
  evict_page();

// Handle race condition
if (ismapped(p->pagetable, va) && PTE2PA(*walk(p->pagetable, va, 0)) != mem) {
  free_unmap_page((void *)mem);  // Another CPU got here first
  return PTE2PA(*walk(p->pagetable, va, 0));
}

// Successfully mapped
acquire(&p->lock);
p->resident_pages++;
release(&p->lock);
return mem;
```

---

## Part G: Allocating Memory Frames

### alloc_user_page() - Getting a Memory Slot

**Basic Steps**:

1. **Check if we're out of memory frames**
   - If yes: evict a page and make space

2. **Get memory from kalloc()**
   - If it fails: try evicting and allocating again

3. **Add to our lookup table**
   - Mark which process owns this page
   - Mark it as "recently used" (ref_bit=1)
   - Remember its virtual and physical addresses

---

## Part H: Handling Race Conditions

### The Problem

Two CPUs try to allocate the same page at the same time:

- CPU A allocates memory frame PA1 and tries to map it
- CPU B allocates memory frame PA2 and tries to map it
- CPU A succeeds at mapping (page is now in memory)
- CPU B fails at mapping (page already mapped!)
- CPU B has PA2 allocated but it's not actually used

### The Solution - free_unmap_page()

When CPU B needs to free PA2:

```c
static int free_unmap_page(void *pa) {
  frame_table[i].in_use = 0;      // Mark slot free
  active_user_pages--;            // Decrement count
  kfree(pa);                       // Free memory
  // Important: Don't decrement resident_pages!
  // Because this page was never actually in memory
}
```

**Why This Works**:

- `free_user_page()`: Used for pages being evicted (decrement everything)
- `free_unmap_page()`: Used for pages that failed to map (only decrement frame count)

---

## Part I: Memory Organization

### How Memory is Organized

From memlayout.h:

- **KERNBASE**: 0x80000000 (start of kernel)
- **PHYSTOP**: KERNBASE + 128MB = 0x88000000 (end of all RAM)
- **USABLE_PHYSTOP**: PHYSTOP - SWAP_SIZE = PHYSTOP - 120MB
- **Kernel code/data**: Located at start of memory
- **User pages**: Can allocate between end of kernel and USABLE_PHYSTOP (limited to 64 frames max)
- **Swap space**: USABLE_PHYSTOP to PHYSTOP (120MB total = 4096 slots × 4KB)

### Swapping Pages to Disk

- 4096 swap slots available (120MB ÷ 4KB)
- Swap space located after USABLE_PHYSTOP
- When page evicted: write to disk at `USABLE_PHYSTOP + (slot_index * PGSIZE)`
- When page faulted: read from same disk location back to memory
- Mark swap slot as free after restoring

---

## Counter Semantics

| Counter | Type | Range | When Incremented |
|---------|------|-------|------------------|
| page_faults | cumulative | 0+ | Fresh page alloc, swap-in restore |
| pages_evicted | cumulative | 0+ | Page written to swap |
| pages_swapped_in | cumulative | 0+ | Page restored from swap |
| pages_swapped_out | cumulative | 0+ | Page written to swap (= pages_evicted) |
| resident_pages | current | 0-64 | Process currently has N pages in memory |

---

## Synchronization

### frametable_lock (spinlock)

- Protects frame_table[] and swap_slot_usage[]
- Acquired in evict_page(), alloc_user_page(), touch_user_frame_by_pa()
- Never held during disk I/O (memmove to swap is done before/after)

### p->lock (spinlock per process)

- Protects process counters
- Acquired before increment: page_faults, resident_pages, pages_swapped_in  
- Acquired in increment_evictions()
- Acquired in sys_getvmstats()

### Critical Section Example

```c
acquire(&p->lock);
p->resident_pages++;
release(&p->lock);
```

---

## Design Philosophy

- **One Frame Table**: Single authoritative source for what's in memory
- **Simple Locking**: One lock per subsystem (frame table, process) avoids complexity
- **Clock Algorithm Fairness**: Each process gets fair chance to keep pages
- **Executable Pages Protected**: Never evict code (PTE_X=1)
- **Reference Bit Hierarchy**: Prefer evicting from larger processes
- **Race Condition Fix**: free_unmap_page() for allocate-but-not-mapped pages
- **Atomicity**: Counters updated atomically under locks

---

## Build & Run

```bash
cd /home/adios123/cs3523-xv6-26
make -j4        # Build kernel + all tests
make qemu        # Boot xv6

# Inside xv6 shell:
A              # Test getvmstats syscall
B              # Test page faults + lazy allocation
C              # Test eviction + clock algorithm  
D              # Test swap correctness + data integrity
E              # Test multi-process isolation
```

Each test prints `=== TEST [N]: PASS ===` on success or error details on failure.

---

## Acknowledgements

Used xv6 documentation, RISC-V ISA specifications, and OS textbooks to understand page replacement algorithms and implement this assignment.
