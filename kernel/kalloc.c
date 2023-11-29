// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int counter[(PHYSTOP - KERNBASE) / PGSIZE]; // KERNBA以下的地址是外部设备，与COW无关  // NOTE: 记得同步修改PA2IDX
} refcnt;

void 
inc_refcnt(void *pa)
{
  if((uint64)pa < KERNBASE)
    return;
  // NOTE: 小心锁的嵌套造成死锁问题
  acquire(&refcnt.lock);
  refcnt.counter[PA2IDX(pa)]++;
  release(&refcnt.lock);
}

void 
dec_refcnt(void *pa)
{
  if((uint64)pa < KERNBASE)
    return;
  acquire(&refcnt.lock);
  refcnt.counter[PA2IDX(pa)]--;
  release(&refcnt.lock);
}

int
get_refcnt(void *pa)
{
  if((uint64)pa < KERNBASE)
    return -1;
  return refcnt.counter[PA2IDX(pa)];
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");

  // 初始化 refcnt结构体
  initlock(&refcnt.lock, "refcnt");
  for(int i = 0; i < (PHYSTOP - KERNBASE) / PGSIZE; i++){
    refcnt.counter[i] = 0;
  }

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    inc_refcnt(p); // NOTE: 由于kfree时会先减1，再判断，而初始化时引用计数全为0，故这里要先加1
    kfree(p);
  }
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

  // 页面引用减1，直到页面引用为0时，才能释放该页面
  dec_refcnt(pa);  // FIXME
  if(get_refcnt(pa) > 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk

    // 分配页面时，将页面的引用计数设为1
    inc_refcnt((void*)r);
  }
    
  return (void*)r;
}
