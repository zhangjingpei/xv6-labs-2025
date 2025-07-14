// kernel/vm.c
//
// 虚拟内存管理实现
// 提供内核和用户空间的页表管理、地址映射、内存分配与释放等功能
//
// 主要功能包括：
//  1. 内核页表初始化与映射
//  2. 用户页表管理（创建、释放、复制）
//  3. 物理内存到虚拟地址空间的映射
//  4. 用户空间与内核空间之间的数据拷贝
//  5. 页表遍历和操作工具函数
//
// 该文件是操作系统内存管理的核心组件，实现了：
//  - 内核静态地址映射
//  - 用户进程的地址空间管理
//  - 物理内存的分配与回收
//  - 页表操作基础函数

#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * 内核页表（全局变量）
 * 所有核心共享的顶级页表，用于内核空间地址映射
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

// 蹦床页面地址（用于特权级切换）
extern char trampoline[];

/*
 * 初始化内核页表
 * 创建直接映射（恒等映射）的内核页表
 * 映射范围包括：外设寄存器、内核代码/数据区、物理内存和蹦床页面
 */
void kvminit()
{
    kernel_pagetable = (pagetable_t)kalloc();
    vminit(kernel_pagetable);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
{
    w_satp(MAKE_SATP(kernel_pagetable));
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
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc)
{
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V)
        {
            pagetable = (pagetable_t)PTE2PA(*pte);
        }
        else
        {
            if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
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
uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;
    uint64 pa;

    if (va >= MAXVA)
        return 0;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        return 0;
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
    if (mappages(kernel_pagetable, va, sz, pa, perm) != 0)
        panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64 kvmpa(pagetable_t pagetable, uint64 va)
{
    uint64 off = va % PGSIZE;
    pte_t *pte;
    uint64 pa;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("kvmpa");
    if ((*pte & PTE_V) == 0)
        panic("kvmpa");
    pa = PTE2PA(*pte);
    return pa + off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
    uint64 a, last;
    pte_t *pte;

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;)
    {
        if ((pte = walk(pagetable, a, 1)) == 0)
            return -1;
        if (*pte & PTE_V)
            panic("remap");
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
    uint64 a;
    pte_t *pte;

    if ((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");

    for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
    {
        if ((pte = walk(pagetable, a, 0)) == 0)
            panic("uvmunmap: walk");
        if ((*pte & PTE_V) == 0)
            panic("uvmunmap: not mapped");
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        if (do_free)
        {
            uint64 pa = PTE2PA(*pte);
            kfree((void *)pa);
        }
        *pte = 0;
    }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate()
{
    pagetable_t pagetable;
    pagetable = (pagetable_t)kalloc();
    if (pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
    char *mem;

    if (sz >= PGSIZE)
        panic("inituvm: more than a page");
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
    memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    char *mem;
    uint64 a;

    if (newsz < oldsz)
        return oldsz;

    oldsz = PGROUNDUP(oldsz);
    for (a = oldsz; a < newsz; a += PGSIZE)
    {
        mem = kalloc();
        if (mem == 0)
        {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0)
        {
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
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    if (newsz >= oldsz)
        return oldsz;

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
    {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }

    return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
        {
            // this PTE points to a lower-level page table.
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child);
            pagetable[i] = 0;
        }
        else if (pte & PTE_V)
        {
            panic("freewalk: leaf");
        }
    }
    kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
    if (sz > 0)
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    pte_t *pte;
    uint64 pa, i;
    uint flags;
    char *mem;

    for (i = 0; i < sz; i += PGSIZE)
    {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        if ((mem = kalloc()) == 0)
            goto err;
        memmove(mem, (char *)pa, PGSIZE);
        if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
        {
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
void uvmclear(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
    uint64 n, va0, pa0;

    while (len > 0)
    {
        va0 = PGROUNDDOWN(dstva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (dstva - va0);
        if (n > len)
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
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
    // uint64 n, va0, pa0;

    // while (len > 0)
    // {
    //     va0 = PGROUNDDOWN(srcva);
    //     pa0 = walkaddr(pagetable, va0);
    //     if (pa0 == 0)
    //         return -1;
    //     n = PGSIZE - (srcva - va0);
    //     if (n > len)
    //         n = len;
    //     memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    //     len -= n;
    //     dst += n;
    //     srcva = va0 + PGSIZE;
    // }
    // return 0;
    return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
    // uint64 n, va0, pa0;
    // int got_null = 0;

    // while (got_null == 0 && max > 0)
    // {
    //     va0 = PGROUNDDOWN(srcva);
    //     pa0 = walkaddr(pagetable, va0);
    //     if (pa0 == 0)
    //         return -1;
    //     n = PGSIZE - (srcva - va0);
    //     if (n > max)
    //         n = max;

    //     char *p = (char *)(pa0 + (srcva - va0));
    //     while (n > 0)
    //     {
    //         if (*p == '\0')
    //         {
    //             *dst = '\0';
    //             got_null = 1;
    //             break;
    //         }
    //         else
    //         {
    //             *dst = *p;
    //         }
    //         --n;
    //         --max;
    //         p++;
    //         dst++;
    //     }

    //     srcva = va0 + PGSIZE;
    // }
    // if (got_null)
    // {
    //     return 0;
    // }
    // else
    // {
    //     return -1;
    // }
    return copyinstr_new(pagetable, dst, srcva, max);
}
void vmprint_recursive(pagetable_t pagetable, int level)
{

    // 按照 xv6-book 书上的规则，三级页表名称，从高地址到低地址（从左到右）依次排列为：L2（又称顶级/根页表）、L1、L0
    static int levels[] = {0, 2, 1, 0};

    // 每个 PTE 占用 8 字节，共 512 个 PTE
    for (int i = 0; i < PGSIZE / 8; i++)
    {
        pte_t pte = pagetable[i];
        // 只处理有效的 PTE
        if (pte & PTE_V)
        {
            // 打印缩进
            for (int j = 0; j < level; j++)
                printf(" ..");

            // 打印 PTE 的下标值，和其对应的物理地址
            uint64 pa = PTE2PA(pte);
            printf("%d: pte %p pa %p\n", i, pte, pa);

            // 额外打印，证明在 xv6 中，第二级和第一级页表的 PTE 都是指向下一级页表的地址，只有最后一个页表的 PTE
            // 指向的是物理内存地址
            printf("level= %d, is it pgtbl? %s\n", levels[level],
                   ((pte & (PTE_R | PTE_W | PTE_X)) == 0) ? "true" : "false");

            // 如果这个 PTE 指向的是下一级页表，递归打印
            if ((pte & (PTE_R | PTE_W | PTE_X)) == 0)
            {
                // 这是一个指向下一级页表的 PTE
                vmprint_recursive((pagetable_t)pa, level + 1);
            } // xv6 中无需考虑第二级和第一级页表项指向物理地址的情况
        }
    }
}

void vmprint(pagetable_t pagetable)
{
    printf("page table %p\n", pagetable);
    vmprint_recursive(pagetable, 1);
}

void vminit(pagetable_t pagetable)
{
    memset(pagetable, 0, PGSIZE);

    // uart registers
    vmmap(pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface
    vmmap(pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // CLINT
    if(pagetable == kernel_pagetable)
        vmmap(pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

    // PLIC
    vmmap(pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

    // map kernel text executable and read-only.
    vmmap(pagetable, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

    // map kernel data and the physical RAM we'll make use of.
    vmmap(pagetable, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    vmmap(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// 新增函数
int vmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
    if (mappages(pagetable, va, sz, pa, perm) != 0)
    {
        if (pagetable == kernel_pagetable)
        {
            panic("vmmap");
        }
        return -1;
    }
    return 0;
}

// 创建一个用户进程的内核页表并完成初始化工作
pagetable_t createukpgtbl()
{
    pagetable_t ukpgtbl = (pagetable_t)kalloc();
    vminit(ukpgtbl);
    return ukpgtbl;
}

// 释放三级页表占用的内存，共 3*4kb
void freeukpgtbl(pagetable_t pagetable)
{
    // 只释放页表占用内存，但不释放页表项里面的物理内存（因为是共享的）
    for (int i = 0; i < 512; i++)
    {
        // level-1 page table entry
        pte_t l1pte = pagetable[i];
        if ((l1pte & PTE_V) && (l1pte & (PTE_R | PTE_W | PTE_X)) == 0)
        {
            uint64 l1ptepa = PTE2PA(l1pte);
            // 释放最后一级页表
            for (int j = 0; j < 512; j++)
            {
                // level-0 page table entry
                pte_t l0pte = ((pagetable_t)l1ptepa)[j];
                if ((l0pte & PTE_V) && (l0pte & (PTE_R | PTE_W | PTE_X)) == 0)
                {
                    kfree((void *)PTE2PA(l0pte)); // 释放 l0 页表
                }
            }
            kfree((void *)l1ptepa); // 释放 l1 页表
        }
    }
    // level-2 page table
    kfree(pagetable); // 释放 l2 顶级页表
}

int u2kvmcopy(pagetable_t upgtbl, pagetable_t kpgtbl, uint64 begin, uint64 end)
{
    pte_t *pte; // 指向页表条目
    uint64 pa, i;
    uint flags;
    for (i = begin; i < end; i += PGSIZE)
    {
        if ((pte = walk(upgtbl, i, 0)) == 0)
        {
            panic("uvmmap_copy: pte should exist");
        }
        if ((*pte & PTE_V) == 0)
        {
            panic("uvmmap_copy: page not present");
        }

        pa = PTE2PA(*pte); // 将页表条目转换为物理地址 *pte是用户空间页表中的内容

        // 映射的时候需要去除页表项中的PTE_U标志
        flags = PTE_FLAGS(*pte) & (~PTE_U);
        // 用户内核空间与用户空间页表映射相同的内容
        if (mappages(kpgtbl, i, PGSIZE, pa, flags) != 0)
        {
            uvmunmap(kpgtbl, 0, i / PGSIZE, 0);
            return -1;
        }
    }
    return 0;
}