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

/*
 * page references
 */
static int pagerefs[(PHYSTOP - KERNBASE) >> PGSHIFT];
static struct spinlock reflock;

// must not held locks when call increfs
void increfs(uint64 pa) {
  acquire(&reflock);
  ++ pagerefs[PA2IDX(pa)];
  release(&reflock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&reflock, "pagerefs");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    pagerefs[PA2IDX(p)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&reflock);
  int refs = -- pagerefs[PA2IDX(pa)];
  release(&reflock);
  if (refs) {
    return ;
  }

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
  if(r) {
    kmem.freelist = r->next;
    pagerefs[PA2IDX(r)] = 1; // doesn't need lock
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

int handlecow(pagetable_t pagetable, uint64 va) {
  if(va >= MAXVA)
    return -1;
  pte_t *pte = walk(pagetable, va, 0);
  if(pte == 0) {
    return -1;
  }
  if((*pte & PTE_V) == 0) {
    printf("page fault @ %p, without PTE_V\n", va);
    return -1;
  }
  if((*pte & PTE_U) == 0){
    printf("page fault @ %p, without PTE_U\n", va);
    return -1;
  }
  if((*pte & PTE_COW) == 0) 
    return 0;
  
  // printf("page fault @%p\n", va);
  // next part can concurrently run with kfree, thus pagerefs[] == 2
  // so should reflock to protect.
  uint64 prevpa = PTE2PA(*pte);
  acquire(&reflock);
  if(pagerefs[PA2IDX(prevpa)] == 1) {
    // only one refs, just update pte flags
    *pte ^= PTE_COW;
    *pte |= PTE_W;
    release(&reflock);
    return 0;
  }
  
  // printf("page fault @ %p of thread: %d\n", r_stval(), myproc()->pid);

  void *pa = kalloc();
  if(pa == 0) {
    release(&reflock);
    return -1;
  }

  memmove(pa, (void *)prevpa, PGSIZE);
  -- pagerefs[PA2IDX(prevpa)];
  release(&reflock);
  
  uint64 newpte = PTE_FLAGS(*pte) | PTE_W | PA2PTE(pa);
  newpte ^= PTE_COW;
  *pte = newpte;
  return 0;
}