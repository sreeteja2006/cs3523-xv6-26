#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"

pagetable_t kernel_pagetable;
int active_user_pages = 0;

extern char etext[];
extern char trampoline[];

struct spinlock frametable_lock;
#define MAX_FRAMES 64
struct sleeplock swap_lock;

struct frame_entry
{
  uint64 va;
  void *pa;
  pagetable_t owner;
  struct proc *owner_proc;
  int ref_bit;
  int in_use;
};
#define BLOCKS_PER_PAGE (PGSIZE / BSIZE)
#define SWAP_SLOTS (SWAP_DISK_BLOCKS / BLOCKS_PER_PAGE)

static struct frame_entry frame_table[MAX_FRAMES];
int swap_slot_usage[SWAP_SLOTS];
static int swap_slot_writing[SWAP_SLOTS];
int swap_slot_raid_mode[SWAP_SLOTS];

void frametable_init(void)
{
  initlock(&frametable_lock, "frametable");
  initsleeplock(&swap_lock, "swap");
  for (int i = 0; i < SWAP_SLOTS; i++)
  {
    swap_slot_usage[i] = 0;
    swap_slot_writing[i] = 0;
  }

  for (int i = 0; i < MAX_FRAMES; i++)
  {
    frame_table[i].in_use = 0;
    frame_table[i].ref_bit = 0;
  }
}

// Write one page to disk swap slot `slot`.
// Must be called without frametable_lock held.
static void swap_write_page(int slot, void *pa)
{
  int mode = raid_get_mode();
  swap_slot_raid_mode[slot] = mode;
  int start = SWAP_START_BLOCK + slot * BLOCKS_PER_PAGE;
  for (int i = 0; i < BLOCKS_PER_PAGE; i++)
    disksched_submit(start + i, 1, (char *)pa + i * BSIZE, myproc(), mode);
  disksched_run();
}

static void swap_read_page(int slot, void *pa)
{
  int mode = swap_slot_raid_mode[slot];
  int start = SWAP_START_BLOCK + slot * BLOCKS_PER_PAGE;
  for (int i = 0; i < BLOCKS_PER_PAGE; i++)
    disksched_submit(start + i, 0, (char *)pa + i * BSIZE, myproc(), mode);
  disksched_run();
}

static int free_user_page(void *pa)
{
  acquire(&frametable_lock);
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    if (frame_table[i].in_use && frame_table[i].pa == pa)
    {
      frame_table[i].in_use = 0;
      struct proc *p = frame_table[i].owner_proc;
      active_user_pages--;
      release(&frametable_lock);

      kfree(pa);

      if (p && p->state != ZOMBIE && p->state != UNUSED)
      {
        acquire(&p->lock);
        p->resident_pages--;
        release(&p->lock);
      }
      return 0;
    }
  }
  release(&frametable_lock);
  return -1;
}

static int free_unmap_page(void *pa)
{
  acquire(&frametable_lock);
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    if (frame_table[i].in_use && frame_table[i].pa == pa)
    {
      frame_table[i].in_use = 0;
      active_user_pages--;
      release(&frametable_lock);
      kfree(pa);
      return 0;
    }
  }
  release(&frametable_lock);
  kfree(pa);
  return -1;
}

static void touch_user_frame_by_pa(void *pa)
{
  acquire(&frametable_lock);
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    if (frame_table[i].in_use && frame_table[i].pa == pa)
    {
      frame_table[i].ref_bit = 1;
      release(&frametable_lock);
      return;
    }
  }
  release(&frametable_lock);
}

void evict_page(void)
{
  acquire(&frametable_lock);

  struct frame_entry *best_victim = 0;
  int best_victim_idx = -1;
  int best_level = -1;
  static int clock_hand = 0;

  // Pass 1: clock sweep, clear ref=1 and prefer ref=0 pages.
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    int idx = (clock_hand + i) % MAX_FRAMES;
    struct frame_entry *fe = &frame_table[idx];
    if (!fe->in_use)
      continue;

    pte_t *pte = walk(fe->owner, fe->va, 0);

    if (pte && (*pte & PTE_A))
    {
      fe->ref_bit = 1;
      *pte &= ~PTE_A;
    }

    if (fe->ref_bit == 0)
    {
      int lvl = get_pagetable_level(fe->owner);
      if (lvl > best_level)
      {
        best_victim = fe;
        best_victim_idx = idx;
        best_level = lvl;
      }
    }
    else
    {
      fe->ref_bit = 0;
    }
  }

  // Pass 2: if needed, pick among pages whose ref bit was just cleared.
  if (best_victim == 0)
  {
    int highest = -1;
    for (int i = 0; i < MAX_FRAMES; i++)
    {
      int idx = (clock_hand + i) % MAX_FRAMES;
      struct frame_entry *fe = &frame_table[idx];
      if (!fe->in_use)
        continue;
      int lvl = get_pagetable_level(fe->owner);
      if (lvl > highest)
      {
        highest = lvl;
        best_victim = fe;
        best_victim_idx = idx;
      }
    }
  }

  if (best_victim == 0)
  {
    release(&frametable_lock);
    panic("evict_page: No evictable pages");
  }

  clock_hand = (best_victim_idx + 1) % MAX_FRAMES;

  void *victim_pa = best_victim->pa;
  uint64 victim_va = best_victim->va;
  pagetable_t victim_owner = best_victim->owner;
  struct proc *victim_vp = best_victim->owner_proc;
  best_victim->in_use = 0;
  best_victim->pa = 0;
  active_user_pages--;

  int swap_slot = -1;
  for (int i = 0; i < SWAP_SLOTS; i++)
  {
    if (swap_slot_usage[i] == 0)
    {
      swap_slot = i;
      swap_slot_usage[i] = 1;
      swap_slot_writing[i] = 1;
      break;
    }
  }
  if (swap_slot == -1)
  {
    release(&frametable_lock);
    panic("evict_page: no disk swap slots");
  }

  pte_t *pte = walk(victim_owner, victim_va, 0);
  if (pte == 0)
  {
    release(&frametable_lock);
    panic("evict_page: pte missing");
  }
  uint flags = PTE_FLAGS(*pte) & ~PTE_V;
  *pte = PA2PTE(swap_slot * PGSIZE) | PTE_V_SWAP | flags;

  release(&frametable_lock);

  if (victim_vp && victim_vp->state != ZOMBIE && victim_vp->state != UNUSED)
  {
    acquire(&victim_vp->lock);
    victim_vp->pages_evicted++;
    victim_vp->pages_swapped_out++;
    victim_vp->resident_pages--;
    release(&victim_vp->lock);
  }

  int mode = raid_get_mode();
  swap_slot_raid_mode[swap_slot] = mode;
  int start = SWAP_START_BLOCK + swap_slot * BLOCKS_PER_PAGE;
  for (int i = 0; i < BLOCKS_PER_PAGE; i++)
    disksched_submit(start + i, 1, (char *)victim_pa + i * BSIZE,
                     myproc(), mode);
  disksched_run();

  acquire(&frametable_lock);
  swap_slot_writing[swap_slot] = 0;
  release(&frametable_lock);

  kfree(victim_pa);
}

static void *alloc_user_page(pagetable_t pagetable, uint64 va)
{
  void *pa;
  struct proc *owner_proc = 0;
  for (struct proc *p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state != UNUSED && p->pagetable == pagetable)
    {
      owner_proc = p;
      break;
    }
  }
  if (!owner_proc)
    owner_proc = myproc();

  for (;;)
  {
    int reserved_slot = -1;

    acquire(&frametable_lock);
    if (active_user_pages >= MAX_FRAMES)
    {
      release(&frametable_lock);
      evict_page();
      continue;
    }

    for (int i = 0; i < MAX_FRAMES; i++)
    {
      if (!frame_table[i].in_use)
      {
        frame_table[i].in_use = 1;
        active_user_pages++;
        reserved_slot = i;
        break;
      }
    }
    release(&frametable_lock);

    if (reserved_slot != -1)
    {
      pa = kalloc();
      if (pa == 0)
      {
        evict_page();
        pa = kalloc();
        if (pa == 0)
        {
          acquire(&frametable_lock);
          frame_table[reserved_slot].in_use = 0;
          active_user_pages--;
          release(&frametable_lock);
          return 0;
        }
      }

      acquire(&frametable_lock);
      frame_table[reserved_slot].va = va;
      frame_table[reserved_slot].pa = pa;
      frame_table[reserved_slot].owner = pagetable;
      frame_table[reserved_slot].owner_proc = owner_proc;
      frame_table[reserved_slot].ref_bit = 1;
      release(&frametable_lock);
      return pa;
    }
  }
}

pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  proc_mapstacks(kpgtbl);
  return kpgtbl;
}

void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

void kvminit(void)
{
  kernel_pagetable = kvmmake();
}

void kvminithart()
{
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--)
  {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V)
    {
      pagetable = (pagetable_t)PTE2PA(*pte);
    }
    else
    {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  touch_user_frame_by_pa((void *)pa);
  return pa;
}

int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");
  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");
  if (size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for (;;)
  {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if ((*pte & PTE_V) || (*pte & PTE_V_SWAP))
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
  {
    if ((pte = walk(pagetable, a, 0)) == 0)
      continue;
    if (*pte & PTE_V_SWAP)
    {
      if (do_free)
      {
        int slot = PTE2PA(*pte) / PGSIZE;
        acquire(&frametable_lock);
        swap_slot_usage[slot] = 0;
        release(&frametable_lock);
      }
      *pte = 0;
      continue;
    }
    if ((*pte & PTE_V) == 0)
      continue;
    if (do_free)
    {
      uint64 pa = PTE2PA(*pte);
      free_user_page((void *)pa);
    }
    *pte = 0;
  }
}

uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE)
  {
    mem = (char *)alloc_user_page(pagetable, a);
    if (mem == 0)
    {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    while (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0)
      evict_page();

    struct proc *p = myproc();
    if (p)
    {
      acquire(&p->lock);
      p->resident_pages++;
      release(&p->lock);
    }
  }
  return newsz;
}

uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }
  return newsz;
}

void freewalk(pagetable_t pagetable)
{
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walk(old, i, 0)) == 0)
      continue;

    if (*pte & PTE_V_SWAP)
    {
      int parent_slot = PTE2PA(*pte) / PGSIZE;
      int child_slot = -1;
      acquire(&frametable_lock);
      for (int j = 0; j < SWAP_SLOTS; j++)
      {
        if (swap_slot_usage[j] == 0)
        {
          child_slot = j;
          swap_slot_usage[j] = 1;
          break;
        }
      }
      release(&frametable_lock);
      if (child_slot == -1)
        panic("uvmcopy: no free swap slots for child");

      char *tmp = kalloc();
      if (tmp == 0)
        goto err;
      swap_read_page(parent_slot, tmp); // read parent's disk slot into RAM
      swap_write_page(child_slot, tmp); // write to child's disk slot
      kfree(tmp);

      flags = PTE_FLAGS(*pte);
      pte_t *child_pte = walk(new, i, 1);
      *child_pte = PA2PTE(child_slot * PGSIZE) | flags;
      continue;
    }

    if ((*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = alloc_user_page(new, i)) == 0)
      goto err;

    acquire(&frametable_lock);
    if (*pte & PTE_V_SWAP)
    {
      int swap_slot = PTE2PA(*pte) / PGSIZE;
      release(&frametable_lock);
      swap_read_page(swap_slot, mem);
    }
    else
    {
      memmove(mem, (char *)pa, PGSIZE);
      release(&frametable_lock);
    }
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
    {
      free_user_page(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
    {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0)
        return -1;
    }

    pte = walk(pagetable, va0, 0);
    if ((*pte & PTE_W) == 0)
      return -1;

    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
    {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0)
        return -1;
    }
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
    {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0)
        return -1;
    }
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0)
    {
      if (*p == '\0')
      {
        *dst = '\0';
        got_null = 1;
        break;
      }
      *dst = *p;
      --n;
      --max;
      p++;
      dst++;
    }
    srcva = va0 + PGSIZE;
  }
  return got_null ? 0 : -1;
}

uint64
vmfault(pagetable_t pagetable, uint64 va, int read)
{
  uint64 mem;
  struct proc *p = myproc();
  (void)read;

  va = PGROUNDDOWN(va);

  if (va >= p->sz)
    return 0;

  if (ismapped(pagetable, va))
    return PTE2PA(*walk(pagetable, va, 0));

  pte_t *pte = walk(pagetable, va, 0);

  if (pte != 0 && (*pte & PTE_V_SWAP))
  {
  retry_swapin:
    acquiresleep(&swap_lock);

    pte = walk(pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V_SWAP))
    {
      uint64 cur = (pte && (*pte & PTE_V)) ? PTE2PA(*pte) : 0;
      releasesleep(&swap_lock);
      return cur;
    }

    int swap_slot = PTE2PA(*pte) / PGSIZE;
    uint flags = PTE_FLAGS(*pte) & ~PTE_V_SWAP;

    acquire(&frametable_lock);
    int writing = swap_slot_writing[swap_slot];
    release(&frametable_lock);
    if (writing)
    {
      releasesleep(&swap_lock);
      yield();
      goto retry_swapin;
    }

    void *new_pa = alloc_user_page(pagetable, va);
    if (new_pa == 0)
    {
      releasesleep(&swap_lock);
      return 0;
    }

    swap_read_page(swap_slot, new_pa);

    *pte = PA2PTE((uint64)new_pa) | flags | PTE_V;
    acquire(&frametable_lock);
    swap_slot_usage[swap_slot] = 0;
    release(&frametable_lock);

    releasesleep(&swap_lock);

    acquire(&p->lock);
    p->resident_pages++;
    p->pages_swapped_in++;
    release(&p->lock);

    return (uint64)new_pa;
  }

  acquire(&p->lock);
  p->page_faults++;
  release(&p->lock);

  mem = (uint64)alloc_user_page(pagetable, va);
  if (mem == 0)
    return 0;

  memset((void *)mem, 0, PGSIZE);

  while (!ismapped(p->pagetable, va) &&
         mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0)
    evict_page();

  if (ismapped(p->pagetable, va) && PTE2PA(*walk(p->pagetable, va, 0)) != mem)
  {
    free_unmap_page((void *)mem);
    return PTE2PA(*walk(p->pagetable, va, 0));
  }

  acquire(&p->lock);
  p->resident_pages++;
  release(&p->lock);

  return mem;
}

int ismapped(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if (*pte & PTE_V)
    return 1;
  return 0;
}