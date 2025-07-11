/*
 * kalloc.c
 *
 * 物理内存分配器，主要用于用户进程、内核栈、页表页和管道缓冲区的物理页分配与释放。
 * 该分配器以页为单位（每页4096字节）进行分配和回收，采用空闲链表管理所有可用物理页。
 *
 * 主要功能：
 * 1. 初始化物理内存分配器（kinit），将内核结束后的所有物理页加入空闲链表。
 * 2. 分配一个物理页（kalloc），返回可用物理页的指针，若无可用页则返回0。
 * 3. 释放一个物理页（kfree），将物理页重新加入空闲链表。
 * 4. 内部辅助函数freerange用于批量释放一段物理内存区域。
 *
 * 线程安全：通过自旋锁kmem.lock保护空闲链表，确保并发安全。
 *
 * 注意事项：
 * - 仅支持以页为单位的分配与释放。
 * - 释放的物理页会用特定值填充，便于调试悬挂引用问题。
 * - 分配的物理页同样会用特定值填充，便于发现未初始化的内存使用。
 */

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
} kmem;

void kinit()
{
    initlock(&kmem.lock, "kmem");
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
void kfree(void *pa)
{
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void)
{
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r)
        kmem.freelist = r->next;
    release(&kmem.lock);

    if (r)
        memset((char *)r, 5, PGSIZE); // fill with junk
    return (void *)r;
}
