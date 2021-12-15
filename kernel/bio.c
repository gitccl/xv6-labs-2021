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
  struct buf buf[NBUF];
} bcache;

struct bucket {
  struct spinlock lock;
  struct buf head;
};

#define NRBUCKET 13
struct bucket buckets[NRBUCKET];

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  for(int i = 0; i < NRBUCKET; ++ i) {
    initlock(&buckets[i].lock, "bcache.bucket");
    buckets[i].head.prev = &buckets[i].head;
    buckets[i].head.next = &buckets[i].head;
  }
  // Create linked list of buffers
  for(int i = 0, index; i < NBUF; i ++){
    index = i % NRBUCKET;
    b = &bcache.buf[i];
    b->next = buckets[index].head.next;
    b->prev = &buckets[index].head;
    initsleeplock(&b->lock, "buffer");
    buckets[index].head.next->prev = b;
    buckets[index].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int index = blockno % NRBUCKET;
  struct bucket *bt = &buckets[index];

  acquire(&bt->lock);

  for(b = bt->head.next; b != &bt->head; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bt->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bt->lock);

  acquire(&bcache.lock);

  // must scans the bucket again to prove there is at most one cached buffer per disk sector
  // since between release(&bt->lock) and acquire(&bcache.lock), there may exist another process 
  // call bget with same `dev` and `blockno`, thus cause that there exist two cached buffer with same sector
  acquire(&bt->lock);
  for(b = bt->head.next; b != &bt->head; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bt->lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bt->lock);
  
  struct bucket *bitem;
  for(int i = 0; i < NRBUCKET; i ++) {
    bitem = &buckets[i];
    acquire(&bitem->lock);

    for(b = bitem->head.prev; b != &bitem->head; b = b->prev) {
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        if (i != index) {
          // remove b from previous bucket
          b->next->prev = b->prev;
          b->prev->next = b->next;

          // insert b to new bucket
          acquire(&bt->lock);
          b->next = bt->head.next;
          b->prev = &bt->head;
          bt->head.next->prev = b;
          bt->head.next = b;
          release(&bt->lock);
        }

        release(&bitem->lock);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }

    release(&bitem->lock);
  }

  release(&bcache.lock);

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

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  struct bucket *bt = &buckets[b->blockno % NRBUCKET];
  acquire(&bt->lock);
  b->refcnt --;
  release(&bt->lock);
}

void
bpin(struct buf *b) {
  struct bucket *bt = &buckets[b->blockno % NRBUCKET];
  acquire(&bt->lock);
  b->refcnt++;
  release(&bt->lock);
}

void
bunpin(struct buf *b) {
  struct bucket *bt = &buckets[b->blockno % NRBUCKET];
  acquire(&bt->lock);
  b->refcnt--;
  release(&bt->lock);
}


