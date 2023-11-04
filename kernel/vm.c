#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // 注意到这段代码传递的参数va和pa一致，是因为是直接映射（前提：关闭了分页机制）

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC(Platform-Level Interrupt Controller)
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  // 将kvminit得到的内核页表根目录地址放入SATP寄存器，相当于打开了分页，此后VA必须要通过MMU翻译为PA
  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.

// 用软件来模拟硬件MMU查找页表的过程，返回以pagetable为根页表，经过多级索引之后va这个虚拟地址所对应的页表项
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  // 第一级根页表肯定是存在的，所以这个函数最多会创建两次新的页表页就已经到达了叶级页表
  for(int level = 2; level > 0; level--) {
    // 索引到对应的PTE项
    pte_t *pte = &pagetable[PX(level, va)];
    // 确认索引到的PTE项是否有效（valid位是否为1）
    if(*pte & PTE_V) {
      // 如果有效则接着下一层索引
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      // 如果无效，则说明对应页表没有分配
      // 则根据alloc标志位决定是否需要申请新的页表（alloc为0则不需要分配，否则要分配）
      // 当不需要分配 or 分配失败时（可能是freelist用尽，没有空页表了），函数返回0
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      // 将申请的页表用0填充
      memset(pagetable, 0, PGSIZE);
      // 将申请到的页表物理地址转化为PTE，并将有效位置为1，记录在当前级页表
      // 在下一次访问时，就可以直接索引到这个页表项
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// NOTE: Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  // 如果此PTE不存在，或者无效，或者用户无权访问，都统统返回0
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
   // 从PTE中截取下来物理地址页号字段，直接返回
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.

// 用于装载一个新的映射关系（可能不止一页）
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  // 虚拟地址和size没必要是页对齐的，在具体实现中会使用PGROUNDDOWN这个宏来执行自动对齐
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    // 调用walk()，返回当前地址a对应的PTE
    // 如果返回空指针，说明walk没能有效建立新的页表页，这可能是内存耗尽导致的
    // PTE在walk()中被创建
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    // 如果找到了页表项，但是有效位已经被置位，表示这块物理内存已经被使用
    // 这说明原本的虚拟地址va根本不足以支撑分配size这么多的连续空间
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory. （do_free）
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  // va必须是页对齐的
  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  // 通过遍历释放npages*PGSIZE大小的映射关系（如果需要，会进一步释放物理内存）
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    // 虚拟地址在索引过程中对应的中间页表页不存在（这里alloc=0，不会因为内存耗尽、无法分配页表而出错）
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    // 找到PTE但无效
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    // 找到PTE，但是只有valid位被设置，说明不是合法的叶级页表（可能发生了奇怪的错误）
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    // 以上情况都不存在，则说明是合法的PTE，可以解除映射关系
    // 如果do_free位被设置，则应该一并释放掉对应的物理内存
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    // 将PTE自身全部清空，就成功解除了映射关系
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");

  // 分配一页物理内存作为initcode的存放处，memset用来将当前页清空
  // NOTE: something interesting，这里并不用判断kalloc返回值是否为0（而在一般的函数中，如uvmcreate中，都是要判断的）
  // NOTE: 因为默认在启动时就会调用这个函数，此时当然不可能出现内存耗尽的情况（某种程度上也是契约的意义）
  mem = kalloc();
  memset(mem, 0, PGSIZE);

  // 在页表中加入一条 VA=0 <-> PA=mem 的映射，相当于将initcode成功映射到了虚拟地址0
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);

  // 将initcode的代码一个字节一个字节地搬运到mem地址
  // NOTE: memmove的实现也非常有意思，考虑到了潜在的src和dest的地址重叠问题
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  // 能用 a = oldsz 来标识将要分配内存的起始地址，是因为用户地址空间从0开始排布
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      // 内存耗尽，则释放之前分配的所有内存，并返回错误标志
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  // 如果新的内存大小比原先内存还要大，那么什么也不用做，直接返回oldsz即可
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    // 要释放的页面数量
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    // 调用uvmunmap，清空叶级页表的PTE并释放物理内存
    // 因为使用了PGROUNDUP来取整页面数量，所以这里可以保证va是页对齐的
    // 因为用户地址空间是从地址0开始紧密排布的， 所以PGROUNDUP(newsz)对应着新内存大小的结束位置
    // 注意do_free置为1，表示一并回收物理内存
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// NOTE: All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    // NOTE: 通过标志位的设置来判断是否到达了叶级页表
    // 如果有效位为1，且读位、写位、可执行位都是0，说明这是一个高级别(非叶级)页表项，且此项未被释放，则应递归释放
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // 如果有效位为1，且读位、写位、可执行位至少有一位为1，表示这是一个叶级PTE，且未经释放，会陷入panic
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  // 释放所有的叶级页表映射关系和物理页
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  // 释放页表页
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  // 用户地址空间从0开始
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    // 复制物理内存（重新分配一块新的并copy）
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    // 为新的页表建立映射关系：va=i <-> pa=mem
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.

// NOTE: 这里的用户地址用uint64，而内核地址用指针char*
// 因为copyin的代码运行在内核态，其中所有指针引用处都会通过MMU查询内核页表，翻译成PA，这对于内核VA而言是正确的
// 而对于用户态下的VA，就无法使用MMU来翻译了，因为kernel mode下的address space是kernel的而非user的
// 只能够通过软件来模拟MMU的查找过程，所以需要在copyin函数中调用walkaddr
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva); // srcva对应页的起始虚拟地址
    pa0 = walkaddr(pagetable, va0); // NOTE: 运行在kernel mode的代码，通过手动模拟MMU的查找过程，来查找user space的映射关系
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n); // 前面被PGROUNDDOWN忽略的偏移在此处加上

    len -= n;
    dst += n;
    // NOTE: 隐式强制对齐，处理了复制过程中非页对齐的情况
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
