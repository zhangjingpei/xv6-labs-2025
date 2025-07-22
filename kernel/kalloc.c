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

struct run
{
    struct run *next;
};

struct
{
    struct spinlock lock;
    struct run *freelist;
} kmem[NCPU]; // 每个CPU都有自己的可用内存链表

void kinit()
{
    for (int i = 0; i < NCPU; i++)
        initlock(&kmem[i].lock, "kmem");
    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

// 同一时间只能有一个 CPU 操作内存，效率太低，实验要求对这块代码进行优化：
void kfree(void *pa)
{
    struct run *r;
    int id = cpuid(); // 获取当前CPUid

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    // 原来的逻辑，加锁，然后把空闲页加到链表的头部，释放锁，搞定
    // acquire(&kmem.lock);
    // r->next = kmem.freelist;
    // kmem.freelist = r;
    // release(&kmem.lock);

    // 改为，把空闲页加入到正在执行的CPU链表中
    acquire(&kmem[id].lock); // 获取当前CPU的内存锁
    r->next = kmem[id].freelist;
    kmem[id].freelist = r;
    //printf("cpu %d free 1 page\n", id);
    release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 同一时间只能有一个 CPU 操作内存，效率太低，实验要求对这块代码进行优化：
void *kalloc(void)
{
    struct run *r = 0;

    // 原来的逻辑，加锁，然后从链表头取一个空闲页，释放锁
    //  acquire(&kmem.lock);
    //  r = kmem.freelist;
    //  if(r)
    //    kmem.freelist = r->next;
    //  release(&kmem.lock);

    // 改为：优先从当前CPU中获取一个空闲页，如果失败，则从当前CPU右手边开始遍历，知道找到一页，或者都没有可用内存
    /*
    优先尝试从 curid（当前 CPU）中获取一个空闲页。
    如果成功，把该页从链表头移除，不满足循环条件，退出循环。
    如果失败，从当前 CPU 右手边继续向下遍历，直到找到一页空闲内存，或者全部 CPU 都没有可用内存，退出循环。
    */
    for (int i = 0, curid = cpuid(); i < NCPU && !r; i++, curid++)
    {
        // i负责统计遍历的CPU数，一旦遍历完CPU还没有找到可用内存，则退出循环
        if (curid == NCPU)
            curid = 0; // 环形遍历
        acquire(&kmem[curid].lock);
        r = kmem[curid].freelist;
        if (r)
        {
            kmem[curid].freelist = r->next;
            //printf("cpu %d steal 1 page from cpu %d", cpuid(), curid);
        }
        release(&kmem[curid].lock);
    }

    if (r)
        memset((char *)r, 5, PGSIZE); // fill with junk
    return (void *)r;
}
