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

// 空闲页链表节点结构
struct run
{
    struct run *next; // 指向下一个空闲页的指针
};

// 内核内存管理结构
struct
{
    struct spinlock lock; // 保护空闲链表的自旋锁
    struct run *freelist; // 空闲页链表头指针
} kmem;

/**
 * 初始化内核内存分配器
 * 1. 初始化自旋锁
 * 2. 释放[end, PHYSTOP]范围内的物理内存到空闲链表
 *
 * [内核代码][内核数据][end]...|空闲内存|...[PHYSTOP]
              ↑            ↑
          起始位置       结束位置
 */
void kinit()
{
    initlock(&kmem.lock, "kmem");    // 初始化保护kmem结构的锁
    freerange(end, (void *)PHYSTOP); // 释放内核结束到物理内存顶部的空间
}

/**
 * 释放指定范围内的物理内存页
 * @param pa_start 起始物理地址(不要求页对齐)
 * @param pa_end 结束物理地址
 */
void freerange(void *pa_start, void *pa_end)
{
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start); // 向上对齐到页边界
    // 遍历并释放每个完整页面
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

/**
 * 释放物理内存页
 * @param pa 要释放的物理地址(必须是页对齐的)
 * 1. 检查地址有效性: 页对齐、在[end, PHYSTOP]范围内
 * 2. 用垃圾数据填充页面(用于检测悬垂指针)
 * 3. 将页面加入空闲链表
 */
void kfree(void *pa)
{
    struct run *r;

    // 检查地址是否有效:
    // 1. 必须页对齐
    // 2. 必须在内核结束地址之后
    // 3. 不能超过物理内存上限
    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // 用垃圾数据(0x01)填充页面，用于检测后续非法访问
    memset(pa, 1, PGSIZE);

    // 将物理页转换为链表节点
    r = (struct run *)pa;

    /*
    释放前: A -> B -> C (头)
    释放P: P -> A -> B -> C (新头)
    */
    // 获取锁保护空闲链表
    acquire(&kmem.lock);
    // 将新释放的页插入链表头部
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

/**
 * 分配一个物理内存页
 * @return 分配页的虚拟地址，失败返回0
 * 1. 从空闲链表获取第一页
 * 2. 用垃圾数据(0x05)填充新分配的页
 * 3. 返回分配页的指针
 * 分配前: P -> A -> B -> C (头)
   分配后: A -> B -> C (新头)
    返回P
 */
void *kalloc(void)
{
    struct run *r;

    // 获取锁保护空闲链表
    acquire(&kmem.lock);
    r = kmem.freelist; // 获取空闲链表第一项
    if (r)
        kmem.freelist = r->next; // 更新链表头指针
    release(&kmem.lock);

    if (r)
        memset((char *)r, 5, PGSIZE); // 用垃圾数据(0x05)填充新分配的页
    return (void *)r;                 // 返回分配的内存页
}

/*
xv6中，空闲内存页的记录方式是，将空虚内存页本身直接用作链表节点，形成一个空闲页链表，每次需要分配，就把链表根部对应的页分配出去。
每次需要回收，就把这个页作为新的根节点，把原来的freelist 链表接到后面。
注意这里是直接使用空闲页本身作为链表节点，所以不需要使用额外空间来存储空闲页链表，
在 kalloc()里也可以看到，分配内存的最后一个阶段，是直接将 freelist 的根节点地址（物理地址）返回出去了：

*/
uint64 count_free_mem(void)
{
    acquire(&kmem.lock); // 必须先锁内存管理结构，防止竞态条件出现

    // 统计空闲页数，乘上页大小PGSIZE就是空闲的内存字节数
    uint64 membytes = 0;
    struct run *r = kmem.freelist;
    while (r)
    {
        membytes += PGSIZE;
        r = r->next;
    }
    release(&kmem.lock);
    return membytes;
}