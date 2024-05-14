#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

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

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
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
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
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
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
  
  a = va;
  last = va + size - PGSIZE;
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
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
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
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
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
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
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
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
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
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
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
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

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
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
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
    pte = walk(pagetable, va0, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
       (*pte & PTE_W) == 0)
      return -1;
    pa0 = PTE2PA(*pte);
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
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
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

// [start, end) region.
void 
mark_freespace(int start, int end)
{
  struct proc *p;
  uint64 m;

  p = myproc();
  while (start < end) {
    m = 1 << (start % 64);
    p->mmapbitmap[start/64] |= m;
    ++start;
  }
}

uint64
alloc_freespace(uint size)
{
  struct proc *p;
  int start, end, n;
  uint64 m;

  if (size <= 0 || size % PGSIZE != 0 || 
      size > NMMAPPAGE*PGSIZE) {
    printf("find_freespace: size error\n");
    return 0;
  }

  p = myproc();
  n = size / PGSIZE;
  for (start = 0, end = 0; end < NMMAPPAGE; ++end) {
    m = 1 << (end % 64);
    if ((p->mmapbitmap[end/64] & m) != 0)
      start = end + 1;
    if (end - start + 1 == n) {
      mark_freespace(start, end + 1);
      return MMAPBASE + start * PGSIZE;
    }   
  }
  return 0;
}

int
dealloc_freespace(uint64 base, uint size)
{
  //printf("dealloc_freespace: %p, %d\n", base, size);
  uint64 m;
  int start, end;
  struct proc *p;

  p = myproc();
  if (base%PGSIZE || size%PGSIZE || base+size >= TRAPFRAME ||
      base+size <= base) {
    printf("dealloc_freespace error\n");
  }
  start = (base - MMAPBASE) / PGSIZE;
  end = start + size / PGSIZE;
  //printf("start=%d, end=%d\n", start, end);
  while (start < end) {
    m = 1 << (start % 64);
    if ((p->mmapbitmap[start/64] & m) == 0) {
      printf("dealloc_freespace error: free unalloced space\n");
      return -1;
    }
    p->mmapbitmap[start/64] &= ~m;
    ++start;
  }
  return 0;
}

uint64
sys_mmap(void)
{
  struct proc *p;
  struct file *f;
  uint64 mmap_addr;
  struct vma  *vp;
  //uint file_size;

  uint64 addr;
  size_t len;
  int prot, flags, fd;
  off_t offset;

  argaddr(0, &addr);
  argaddr(1, (uint64*)&len);
  argint(2, &prot);
  argint(3, (int *)&flags);
  argint(4, &fd);
  argaddr(5, (uint64*)&offset);

  printf("addr=%p, len=%p, prot=%d, flags=%d, fd=%d, offset=%d\n", addr, len, prot, flags, fd, offset);

  if (addr != 0) {
    printf("sys_mmap: addr != 0\n");
    return -1;
  }

  p = myproc();

  if ((f = p->ofile[fd]) == 0) {
    printf("sys_mmap: file isn't opened in current proc\n");
    return -1;
  }

  // check prot
  //printf("rd=%d, wr=%d\n", f->readable, f->writable);
  if (!f->writable && (prot&PROT_WRITE) && !(flags&MAP_PRIVATE)) {
    printf("sys_mmap: umatched R/W/X permission\n");
    return -1;
  }
  
  filedup(f);

  // alloc user space for mmap
  if ((mmap_addr = alloc_freespace(len)) == 0) {
    printf("sys_mmap: no enough space for mmap\n");
    goto bad;
  }
  printf("mmap_addr=%p\n", mmap_addr);

  vp = 0;
  for (int i = 0; i < NVMA; ++i) {
    if (p->vma[i].valid == 0) {
      vp = &p->vma[i];
      break;
    }
  }
  if (vp == 0) {
    printf("mmap: no empty vma\n");
    dealloc_freespace(mmap_addr, len);
    goto bad;
  }
  vp->addr = mmap_addr;
  vp->len = len;
  vp->prot = prot;
  vp->flags = flags;
  vp->f = f;
  vp->valid = 1;

  printf("after mmap (addr=%p, len=%d): ", addr, len);
  for (int i = 0; i < 4; ++i) {
    printf("%p ", p->mmapbitmap[i]);
  }
  printf("\n");

  return mmap_addr;

bad:
  fileclose(f);
  return -1;
}

void
free_vma(struct proc *p, struct vma *vp)
{
  uint64 addr, m, mmi;
  int clean = 1;
  for (addr = vp->addr; addr < vp->addr + vp->len; addr += PGSIZE) {
    if (addr < MMAPBASE || addr >= TRAPFRAME)
      panic("free_vma: range");
    mmi = (addr - MMAPBASE) / PGSIZE;
    m = 1 << (mmi % 64);
    if ((p->mmapbitmap[mmi/64] & m) != 0) {
      clean = 0;
      break;
    }
  }
  if (clean) {
    vp->valid = 0;
    fileclose(vp->f);
  }
}

uint64
sys_munmap(void)
{
  struct proc *p;
  struct inode *ip;
  struct vma  *vp;

  uint64 addr, va;
  size_t len, writelen, off;

  argaddr(0, &addr);
  argaddr(1, (uint64*)&len);

  if (addr < MMAPBASE || addr+len <= addr ||
      addr+len >= TRAPFRAME || addr % PGSIZE ||
      len % PGSIZE) {
    printf("sys_munmap: args error\n");
    return -1;
  }

  p = myproc();
  /*printf("sys_munmap: addr=%p, len=%d\n", addr, len);
  for (vp = p->vma; vp < &p->vma[NVMA]; ++vp) {
    if (vp->valid)
      printf("  addr=%p, len=%d\n", vp->addr, vp->len);
  }*/

  vp = 0;
  for (vp = p->vma; vp < &p->vma[NVMA]; ++vp) {
    if (vp->valid && addr >= vp->addr &&
        addr + len <= vp->addr + vp->len) {
      break;
    }
  }

  if (vp == &p->vma[NVMA]) {
    printf("sys_munmap: user addr %p not be mapped\n", addr);
    return -1;
  }

  dealloc_freespace(addr, len);
  
  // write back to disk;
  ip = vp->f->ip;
  if (vp->flags & MAP_SHARED) {
    begin_op();
    ilock(ip);
    for (off = addr - vp->addr; off<len && off < ip->size; off += PGSIZE) {
      writelen = min(PGSIZE, ip->size - off);
      //printf("writelen=%d\n", writelen);
      if (writei(ip, 1, addr + off, off, writelen) != writelen) {
        printf("sys_munmap: writei\n");
        goto bad;
      }
    }
    iunlock(ip);
    end_op();
  }

  /*printf("after munmap (addr=%p, len=%d): ", addr, len);
  for (int i = 0; i < 4; ++i) {
    printf("%p ", p->mmapbitmap[i]);
  }
  printf("\n");*/

  for (va = addr; va < addr + len; va+= PGSIZE) {
    if (walk(p->pagetable, va, 0))
      uvmunmap(p->pagetable, va, 1, 1);
  }
  free_vma(p, vp);

  printf("after munmap (addr=%p, len=%d): ", addr, len);
  for (int i = 0; i < 4; ++i) {
    printf("%p ", p->mmapbitmap[i]);
  }
  printf("\n");

  return 0;

bad:
  iunlock(ip);
  end_op();
  return -1;
}

// return 0 if success,
// -1, if failed,
// -2, if not mmap page fault.
int 
uvmcoe(uint64 va)
{
  struct proc *p;
  struct inode *ip;
  struct vma *vp;
  int mmi, copylen, readlen, off;
  uint64 m;
  char *mem;
  uint flags;

  printf("va=%p\n", va);

  p = myproc();
  va = PGROUNDDOWN(va);

  vp = 0;
  for (vp = p->vma; vp < &p->vma[NVMA]; ++vp) {
    if (vp->valid && va >= vp->addr &&
        va < vp->addr + vp->len) {
      break;
    }
  }
  if (vp == &p->vma[NVMA]) {
    return -2;
  }
  //printf("vp->addr=%p, vp->len=%d\n", vp->addr, vp->len);

  mmi = (va - MMAPBASE) % PGSIZE;
  m = 1 << (mmi % 64);
  if ((p->mmapbitmap[mmi/64] & m) == 0) {
    printf("uvmcoe: user addr %p should been existed\n", va);
    panic("hahaha");
    kill(p->pid);
    return -1;
  }

  if ((mem = kalloc()) == 0) {
    kill(p->pid);
    return -1;
  }
  
  ip = vp->f->ip;
  begin_op();
  ilock(ip);
  off = va - vp->addr;
  if (off >= ip->size) 
    copylen = 0;
  copylen = off >= ip->size ? 0 : min(ip->size - off, PGSIZE);
  printf("off=%p, copylen=%d\n", off, copylen);
  readlen = readi(ip, 0, (uint64)mem, off, copylen);
  if (readlen != copylen) {  
    printf("uvmcoe: readi error, readlen=%d\n", readlen);
    goto bad;
  }
  iunlock(ip);
  end_op();

  if (copylen < PGSIZE) {
    memset(mem + copylen, 0, PGSIZE - copylen);
  }

  flags = PTE_V | PTE_U | PTE_R;
  if (vp->prot & PROT_WRITE) flags |= PTE_W;
  if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, flags) == 1) {
    printf("uvmcoe: mappages error\n");
    goto bad;
  }

  p->mmapbitmap[mmi/64] |= m;

  return 0;

bad:
  kfree(mem);
  iunlock(ip);
  end_op();
  kill(p->pid);
  return -1;
}