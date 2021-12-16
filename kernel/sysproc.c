#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

#define NRVM 16
#define STARTADDR (PGSIZE * 16)

static struct {
  struct spinlock lock;
  struct vm_area_struct vmarr[NRVM];
}vm;

void mmapinit() {
  initlock(&vm.lock, "mmap");
  memset(vm.vmarr, 0, sizeof(vm.vmarr));
}

struct vm_area_struct *vmalloc() {
  struct vm_area_struct *r = 0;
  acquire(&vm.lock);
  for(int i = 0; i < NRVM; ++ i) {
    if(!vm.vmarr[i].used) {
      vm.vmarr[i].used = 1;
      r = &vm.vmarr[i];
      break;
    }
  }
  release(&vm.lock);
  if(r == 0) 
    panic("no more vm_area_struct");
  return r;
}

void freevm(struct vm_area_struct *vmarea) {
  acquire(&vm.lock);
  vmarea->used = 0;
  release(&vm.lock);
}

void vmmap(pagetable_t pagetable, uint64 start, uint64 end)
{
  pte_t *pte;
  uint64 i;

  for(i = start; i < end; i += PGSIZE){
    if((pte = walk(pagetable, i, 1)) == 0)
      panic("vmmap: walk failed");

    if((*pte & PTE_V) != 0)
      panic("vmmap: remap");

    *pte = PTE_V; // just PTE_V
  }
}

void
vmunmap(pagetable_t pagetable, uint64 va, uint64 npages)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("vmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("vmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("vmunmap: not mapped");
    uint64 pa = PTE2PA(*pte);
    if(pa)
      kfree((void*)pa);
    *pte = 0;
  }
}

// void *mmap(void *addr, uint64 length, int prot, int flags,
//           int fd, uint64 offset);
uint64
sys_mmap(void)
{
  uint64 length, offset;
  int prot, flags, fd;
  struct proc *p;
  struct vm_area_struct *vmarea;
  struct file *f;

  if(argaddr(1, &length) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0
    || argint(4, &fd) < 0 || argaddr(5, &offset) < 0) {
    return -1;
  }
  
  if(offset % PGSIZE) 
    return -1;

  p = myproc();
  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0) 
    return -1;

  if((prot & PROT_WRITE) && (flags & MAP_SHARED) && !f->writable) 
    return -1;
  if(((prot & PROT_READ) || (prot & PROT_EXEC)) && !f->readable)
    return -1;

  ilock(f->ip);
  if(offset >= f->ip->size) 
    return -1;
  iunlock(f->ip);

  if(p->mmap == 0) {
    p->maxva = p->sz > STARTADDR ? PGROUNDUP(p->sz) : STARTADDR;
  }

  uint64 start = p->maxva;
  uint64 end = start + length;
  if(end >= CLINT) { // memlayout.h
    start = p->maxva = 0x20000000;
    end = start + length;
  }
  
  if(end >= KERNBASE) {
    printf("mmap out of KERNBASE\n");
    return -1;
  }

  vmarea = vmalloc();
  
  vmarea->vm_start = start;
  vmarea->vm_end = end;
  vmarea->vm_prot = prot;
  vmarea->vm_flags = flags;
  vmarea->vm_off = offset;
  vmarea->vm_file = filedup(f);
  vmarea->vm_next = p->mmap;
  p->mmap = vmarea;
  p->maxva = PGROUNDUP(end); 
  vmmap(p->pagetable, start, end);
  return start;
}

void writetodisk(struct vm_area_struct *vmarea, uint64 start, uint64 length) {

  if((vmarea->vm_flags & MAP_PRIVATE) || !(vmarea->vm_prot & PROT_WRITE))
    return;
  
  struct inode *ip = vmarea->vm_file->ip;
  begin_op();
  ilock(ip);
  // TODO: check whether virtual address has been mapped to pa
  writei(ip, 1, start, vmarea->vm_off + start - vmarea->vm_start, length);
  iunlock(ip);
  end_op();
}

int vmareacopy(struct proc *parent, struct proc *son) {
  struct vm_area_struct *vmarea = parent->mmap, *vma;

  while (vmarea) {
    vma = vmalloc();
    memmove(vma, vmarea, sizeof(*vma));
    vma->vm_file = filedup(vmarea->vm_file);
    vmmap(son->pagetable, vma->vm_start, vma->vm_end);

    vma->vm_next = son->mmap;
    son->mmap = vma;

    vmarea = vmarea->vm_next;
  }
  return 0;
}

void unmapall() {
  struct proc *p = myproc();
  struct vm_area_struct *vmarea;
  uint64 length;
  while ((vmarea = p->mmap)) {
    length = vmarea->vm_end - vmarea->vm_start;
    writetodisk(vmarea, vmarea->vm_start, length);
    vmunmap(p->pagetable, vmarea->vm_start, PGROUNDUP(length) / PGSIZE);
    freevm(vmarea);
    p->mmap = p->mmap->vm_next;
  }
}

uint64
sys_munmap(void)
{
  uint64 addr, length;
  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0) {
    return -1;
  }

  if(addr % PGSIZE)
    return -1;

  struct proc *p = myproc();
  struct vm_area_struct *vmarea;

  for(vmarea = p->mmap; vmarea; vmarea = vmarea->vm_next) {
    if(addr >= vmarea->vm_start && addr + length <= vmarea->vm_end) {
      break;
    }
  }
  if(vmarea == 0)
    return -1;

  writetodisk(vmarea, addr, length);
  vmunmap(p->pagetable, addr, PGROUNDUP(length) / PGSIZE);
  if(vmarea->vm_start == addr && vmarea->vm_end == addr + length) {
    struct vm_area_struct **q = &p->mmap;
    // delete vmarea
    while(*q && *q != vmarea) {
      q = &(*q)->vm_next;
    }
    (*q) = (*q)->vm_next;
    freevm(vmarea);
  } else if(vmarea->vm_start == addr) {
    if(length % PGSIZE) {
      printf("munmap, start addr not align PGSIZE\n");
    }
    vmarea->vm_start = addr + length;
    vmarea->vm_off += length;

  } else {
    panic("munmap to be done");
  }
  return 0;
}

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
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
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
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

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
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

  if(argint(0, &pid) < 0)
    return -1;
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
