#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_hello(void)
{
  printf("Hello from the kernel!\n");
  return 0;
}

uint64
sys_getpid2(void)
{
  return myproc()->pid;
}

uint64
sys_getppid(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  uint64 ppid = p->parent ? p->parent->pid : -1;
  release(&p->lock);
  return ppid;
}


uint64
sys_getnumchild(void)
{
  struct proc *p = myproc();
  int count = 0;
  struct proc *proc_iter;

  for(proc_iter = proc; proc_iter < &proc[NPROC]; proc_iter++) {
    acquire(&proc_iter->lock);
    if(proc_iter->parent == p && proc_iter->state != ZOMBIE  && proc_iter->state != UNUSED) {
      count++;
    }
    release(&proc_iter->lock); 
  }
  return count;
} 

uint64
sys_getsyscount(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  uint64 c = p->syscount;
  release(&p->lock);
  return c;
}



uint64
sys_getchildsyscount(void)
{
  int chpid;
  argint(0, &chpid);
  if(chpid < 0)
    return -1;

  struct proc *p = myproc();

  for(struct proc *q = proc; q < &proc[NPROC]; q++){
    acquire(&q->lock);
    if(q->parent == p && q->pid == chpid){
      uint64 c = q->syscount;
      release(&q->lock);
      return c;
    }
    release(&q->lock);
  }
  return -1;
}
