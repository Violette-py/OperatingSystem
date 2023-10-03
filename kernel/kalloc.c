// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
                   // FIXME: why char* ???

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  char lock_name[7];
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++) {
    snprintf(kmem[i].lock_name, sizeof(kmem[i].lock_name), "kmem_%d", i);
    initlock(&kmem[i].lock, kmem[i].lock_name);
  }
  freerange(end, (void*)PHYSTOP);      
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cpu_id = cpuid();
  // pop_off();

  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;
  kmem[cpu_id].freelist = r;
  release(&kmem[cpu_id].lock);

  pop_off();

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // NOTE: The function cpuid returns the current core number, but it's only safe to call it and use its result when interrupts are turned off. You should use push_off() and pop_off() to turn interrupts off and on.

  push_off();
  int cpu_id = cpuid();
  // pop_off();

  // FIXME: 当2个cpu试图互相借用时，可能会出现死锁 -- 当两个锁的调用和释放有交叉时，注死锁的问题
  // FIXME: 释放自己的锁再去偷页，把偷的页头指针保存起来，再申请本cpu的锁来更新空闲链表？

  acquire(&kmem[cpu_id].lock);
  r = kmem[cpu_id].freelist;

  if(r) {
    kmem[cpu_id].freelist = r->next;
    release(&kmem[cpu_id].lock);

  } else {
    // current cpu has no freelist, so lend from other cpu

    release(&kmem[cpu_id].lock);

    int free_cpu;
    
    // FIXME: 目前只是顺序访问，没有使用tricks
    for(int i = 0; i < NCPU; i++) {
      free_cpu = (cpu_id + i) % NCPU;
      if(kmem[free_cpu].freelist) {
        acquire(&kmem[free_cpu].lock);
        r = kmem[free_cpu].freelist;
        kmem[free_cpu].freelist = r->next;
        release(&kmem[free_cpu].lock);
        break;
      }
    }
  }
  
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
