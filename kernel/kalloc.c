// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

int kmemref[PGNUMS];

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

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  for (int i = 0; i < PGNUMS; ++i)
    kmemref[i] = 1;
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

int inckmemref(uint64 pa) {
  int count;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("inckmemref");
  acquire(&kmem.lock);
  count = kmemref[PGIDX(pa)];
  ++count;
  kmemref[PGIDX(pa)] = count;
  release(&kmem.lock);
  return count;
}

int deckmemref(uint64 pa)
{
  int count;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("inckmemref");
  acquire(&kmem.lock);  
  count = kmemref[PGIDX(pa)];
  --count;
  kmemref[PGIDX(pa)] = count;
  release(&kmem.lock);
  return count;
}

int getkmemref(uint64 pa) {
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("inckmemref");
  //int count;
  //acquire(&kmem.lock);  
  //count = kmemref[PGIDX(pa)];
  //release(&kmem.lock);
  //return count;
  return kmemref[PGIDX(pa)];
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int refcnt;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  refcnt = kmemref[PGIDX((uint64) pa)];
  --refcnt;
  kmemref[PGIDX((uint64) pa)] = refcnt;
  if (refcnt == 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    r = (struct run*)pa;
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
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
  if(r) {
    kmem.freelist = r->next;
    kmemref[PGIDX((uint64) r)] = 1;
    memset((char*)r, 5, PGSIZE); // fill with junks
  }
  release(&kmem.lock);
  return (void*)r;
}
