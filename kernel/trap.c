#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[];

void kernelvec();
extern int devintr();

static int
quantum_for_level(int level)
{
  if (level == 0)
    return 2;
  if (level == 1)
    return 4;
  if (level == 2)
    return 8;
  return 16;
}

static void
handle_mlfq_tick(struct proc *p)
{
  if (p == 0 || p->state != RUNNING)
    return;

  p->qticks++;
  p->ticks[p->level]++;

  if (p->qticks >= quantum_for_level(p->level))
  {
    int deltaS = p->syscount - p->initial_syscount;
    if (deltaS < p->qticks)
    {
      if (p->level < 3)
        p->level++;
    }
    p->qticks = 0;
    p->initial_syscount = p->syscount;
    yield();
  }
}
static void
priority_boost(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if ((p->state == RUNNABLE || p->state == RUNNING || p->state == SLEEPING) && p->level != 0)
    {
      p->level = 0;
      p->qticks = 0;
    }
    release(&p->lock);
  }
}
void trapinit(void)
{
  initlock(&tickslock, "time");
}

void trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

uint64
usertrap(void)
{
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  p->trapframe->epc = r_sepc();

  if (r_scause() == 8)
  {
    if (killed(p))
      kexit(-1);

    p->trapframe->epc += 4;
    intr_on();
    syscall();
  }
  else if ((which_dev = devintr()) != 0)
  {
  }
  else if ((r_scause() == 15 || r_scause() == 13 || r_scause() == 12) &&
           vmfault(p->pagetable, r_stval(), 0) != 0)
  {
  }
  else
  {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if (killed(p))
    kexit(-1);

  if (which_dev == 2)
    handle_mlfq_tick(p);

  prepare_return();

  return MAKE_SATP(p->pagetable);
}

void prepare_return(void)
{
  struct proc *p = myproc();

  intr_off();

  sfence_vma();

  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  p->trapframe->kernel_satp = r_satp();
  p->trapframe->kernel_sp = p->kstack + PGSIZE;
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();

  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP;
  x |= SSTATUS_SPIE;
  w_sstatus(x);

  w_sepc(p->trapframe->epc);
}

void kerneltrap(void)
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0)
  {
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  if (which_dev == 2)
    handle_mlfq_tick(myproc());

  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr(void)
{
  int do_boost = 0;

  if (cpuid() == 0)
  {
    acquire(&tickslock);
    ticks++;
    if (ticks % 128 == 0)
    {
      do_boost = 1;
    }
    wakeup(&ticks);
    release(&tickslock);

    if (do_boost)
      priority_boost();
  }

  w_stimecmp(r_time() + 1000000);
}

int devintr(void)
{
  uint64 scause = r_scause();

  if (scause == 0x8000000000000009L)
  {
    int irq = plic_claim();

    if (irq == UART0_IRQ)
    {
      uartintr();
    }
    else if (irq == VIRTIO0_IRQ)
    {
      virtio_disk_intr();
    }
    else if (irq)
    {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    if (irq)
      plic_complete(irq);

    return 1;
  }
  else if (scause == 0x8000000000000005L)
  {
    clockintr();
    return 2;
  }
  else
  {
    return 0;
  }
}