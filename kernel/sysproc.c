#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
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
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
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
  return kill(pid);
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

// retrieve the number of active processes 
uint64
sys_procnum(void)
{
  // Collect the number of active processes
  int count = 0; 
  for (int i = 0; i < NPROC; i++) {
    // There is no need to lock proc[i] because we are only reading data, not performing write operations
    if (proc[i].state != UNUSED) {
      count++;  
    }
  }

  // Get the user-supplied memory address from the first argument, here is the address of num
  uint64 addr;        
  argaddr(0, &addr);  

  // Copy the count result from kernel to user space
  struct proc *p = myproc();  
  if (copyout(p->pagetable, addr, (char*)&count, sizeof(count)) < 0) {
    return -1;  
  }

  return 0; 
}