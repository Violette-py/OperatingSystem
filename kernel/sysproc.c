#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
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


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // TODO: lab pgtbl: your code here.

  // 从用户进程获取系统调用参数

  uint64 base, maskVA;
  argaddr(0, &base); // 被检查的第一个用户页的起始虚拟地址
  argaddr(2, &maskVA); // 存放的结果
  
  int len;
  argint(1, &len);   // 被检查页面的数量
  if(len > MAXSCAN)
    return -1;

  struct proc *p = myproc();
  pte_t *pte;
  uint64 va = base;
  uint64 mask = 0;

  for(uint64 i = 0; i < len; i++, va += PGSIZE){
    if((pte = walk(p->pagetable, va, 0)) == 0)
      panic("pgaccess: walk failed");
    
    if(*pte & PTE_A){
      mask |= 1 << i; // 设置mask对应位
      *pte &= ~PTE_A; // 将PTE_A清空
    }
  }

  if(copyout(p->pagetable, maskVA, (char*)&mask, sizeof(mask)) < 0)
    return -1;

  return 0;
}
#endif

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
