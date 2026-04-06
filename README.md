# CS3523 Programming Assignment - Page Replacement (PA-3)

**Date:** April 2026

**Course:** CS3523 - Operating Systems II

---

## Introduction

This project implements a demand-paging and swap-space management system in xv6. When physical memory fills up (capped at 64 frames), the kernel evicts old pages to a 120MB swap region on the disk using a Clock algorithm that favor keeping high-priority processes in memory. We also added a per-process accounting system for page faults and swaps, accessible via a new `getvmstats` syscall. There are test programs A through N to verify that everything works correctly.

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
- **user/A.c to N.c**: Test programs and stress tests
- **Makefile**: To include the new test programs in the build process

---

## Part A: Kernel Data Structures

### Frame Table (kernel/vm.c, lines 18-28)

```c
struct frame_entry {
  uint64 va;           // Virtual address
  void *pa;            // Memory address
  pagetable_t owner;   // Which pagetable owns this page
  struct proc *owner_proc; // Remember the exact process for stats
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

- 12: Instruction page fault (fetching from an invalid code page)
- 13: Load page fault (reading from an invalid data page)
- 15: Store page fault (writing to an invalid data page)

When a fault happens:

- Call vmfault() to handle it
- vmfault() returns success/failure

---

## Part D: Lazy Allocation

### How sbrklazy() Works

When user calls sbrklazy(size) (or sbrk if configured for lazy allocation):

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
   - The CPU sets the `PTE_A` bit in the page table when a page is accessed.
   - If `PTE_A` is 1, we update our `ref_bit` and then clear `PTE_A` so we can detect future reuse.
   - If `PTE_A` is 0 but our `ref_bit` is already 1, we clear the `ref_bit` and give it a second chance.
   - If both `PTE_A` and `ref_bit` are 0, it's a victim! 
3. **Skip Code Pages**: We skip pages with `PTE_X` (executable) set to avoid evicting the kernel or user binaries.
4. **Prefer low priority processes**: Processes with low priority level (lower priority in SC-MLFQ) are evicted first.
5. **Write to disk** and mark memory slot as free.
6. **Update counters**: Mark page as swapped out and decrease resident count.

### How We Write to Disk

```c
static inline char *swap_slot_addr(int slot) {
  return (char *)(USABLE_PHYSTOP + (uint64)slot * PGSIZE);
}
```

Disk space is located after regular memory. Each page gets saved at a fixed location.

### Update Counters After Eviction

When a page is evicted, the counters need to be updated. Since `exec()` temporarily changes the `pagetable` pointer before freeing the old memory, iterating over the process table to find the owner can miss the process entirely and mess up `resident_pages`. 

To fix this, `struct frame_entry` stores `owner_proc` directly. When evicting or freeing pages, we just use `owner_proc` to decrease the stats directly:

```c
  struct proc *vp = best_victim->owner_proc;
  if(vp && vp->state != ZOMBIE && vp->state != UNUSED) {
     acquire(&vp->lock);
     vp->pages_evicted++;
     vp->pages_swapped_out++;
     vp->resident_pages--;
     release(&vp->lock);
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
// Increment fault counter (zero-fill faults only)
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

### The Page Mapping Problem

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

### Eviction Race Condition

There was a race condition in `alloc_user_page` where:

1. Process A calls `evict_page()` and frees a frame
2. A context switch happens
3. Process B runs and steals that newly freed frame slot
4. Process A comes back and panics because there are no free frames left

**The Fix:**
I changed it so `alloc_user_page` reserves the slot `in_use = 1` first. Then it can safely drop locks and evict without worrying about someone else stealing the slot.

### The exec() Stats Bug
During `exec()`, `proc_freepagetable` clears the old memory after `p->pagetable` is updated. This means iterating over processes to find `oldpagetable` fails, and `resident_pages` never goes down.

**The Fix:**
I stored `struct proc *owner_proc` in the frame table when allocating pages. This lets us reliably decrement `resident_pages` using the pointer, avoiding issues when `exec` swaps pointers around.

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



## Acknowledgements

Used xv6 documentation, RISC-V ISA specifications, and OS textbooks to understand page replacement algorithms and implement this assignment.
