// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[PERBUF];
} bcache[NBUCKET];

void
binit(void)
{ 
  for (int i = 0; i < NBUCKET; ++i) {
    initlock(&bcache[i].lock, "bcache");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *blank;
  uint plain, idx;
  
  blank = 0;
  plain = blockno % NBUCKET;
  idx = plain;

  do {
    acquire(&bcache[idx].lock);
    
    // Is the block already cached?
    for (b = bcache[idx].buf; b != bcache[idx].buf + PERBUF; ++b) {
      if (b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        release(&bcache[idx].lock);
        acquiresleep(&b->lock);
        return b;
      }
      if (blank == 0 && b->refcnt == 0) {
        blank = b;
      }
    }

    if (blank) {
      blank->dev = dev;
      blank->blockno = blockno;
      blank->valid = 0;
      blank->refcnt = 1;
      release(&bcache[idx].lock);
      acquiresleep(&blank->lock);
      return blank;
    }

    release(&bcache[idx].lock);

    ++idx;
    if (idx == NBUCKET)
      idx = 0;
  } while (idx != plain);

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

uint findbucket(struct buf *b) {
  struct buf *iterb;
  uint tgt, idx;

  // No race will occur even if we don't use spinlock, 
  // because the refcnt field of b isn't 0. We will
  // never change some context of a struct buf if it's
  // refcnt is non-zero.
  tgt = b->blockno % NBUCKET;
  idx = tgt;
  
  do {
    // We need this spinlock, because other process may 
    // write some dev block info to a free cache block.
    // If we read this cache block when it is being wrote,
    // we will get wrong dev info which may equal to b. 
    acquire(&bcache[idx].lock);
    for (iterb = bcache[idx].buf; iterb != bcache[idx].buf + PERBUF; ++iterb) {
      if (iterb->refcnt > 0 && iterb->dev == b->dev && iterb->blockno == b->blockno) {
        release(&bcache[idx].lock);
        return idx;
      }
    }
    release(&bcache[idx].lock);
    ++idx;
    if (idx == NBUCKET)
      idx = 0;
  } while (idx != tgt);
  
  panic("findbucket: can not find the block in buffer.");
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  uint idx;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  idx = findbucket(b);
  acquire(&bcache[idx].lock);
  b->refcnt--;
  release(&bcache[idx].lock);
}

void
bpin(struct buf *b) {
  uint idx = findbucket(b);
  acquire(&bcache[idx].lock);
  b->refcnt++;
  release(&bcache[idx].lock);
}

void
bunpin(struct buf *b) {
  uint idx = findbucket(b);
  acquire(&bcache[idx].lock);
  b->refcnt--;
  release(&bcache[idx].lock);
}


