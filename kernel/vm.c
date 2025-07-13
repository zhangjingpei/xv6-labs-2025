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

// 内核代码结束地址（由链接脚本kernel.ld定义）
extern char etext[];

// 蹦床页面地址（用于特权级切换）
extern char trampoline[];

/*
 * 初始化内核页表
 * 创建直接映射（恒等映射）的内核页表
 * 映射范围包括：外设寄存器、内核代码/数据区、物理内存和蹦床页面
 */
void kvminit()
{
    // 分配内核页表空间
    kernel_pagetable = (pagetable_t)kalloc();
    memset(kernel_pagetable, 0, PGSIZE);

    // 映射UART寄存器
    kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // 映射VirtIO磁盘接口
    kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // 映射CLINT（核心本地中断器）
    kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

    // 映射PLIC（平台级中断控制器）
    kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

    // 映射内核代码区（只读可执行）
    kvmmap(KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

    // 映射内核数据区及可用物理内存（可读写）
    kvmmap((uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

    // 映射蹦床代码（位于内核地址空间顶端，用于用户/内核切换）
    kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

/*
 * 激活内核页表
 * 将内核页表写入satp寄存器并刷新TLB
 * 每个核心启动时调用
 */
void kvminithart()
{
    // 设置SATP寄存器
    w_satp(MAKE_SATP(kernel_pagetable));
    // 刷新TLB
    sfence_vma();
}

/*
 * 页表遍历函数
 * 返回虚拟地址va对应的页表项(PTE)指针
 *
 * 参数：
 *   pagetable: 顶级页表指针
 *   va: 要查找的虚拟地址
 *   alloc: 是否分配缺失的页表
 *
 * 返回：PTE指针，失败返回0
 *
 * RISC-V Sv39三级页表结构：
 *   VA[39:63] = 0
 *   L2索引 = VA[30:38]
 *   L1索引 = VA[21:29]
 *   L0索引 = VA[12:20]
 *   页内偏移 = VA[0:11]
 */
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc)
{
    if (va >= MAXVA)
        panic("walk");

    // 逐级处理L2->L1->L0
    for (int level = 2; level > 0; level--)
    {
        // 获取当前层级的PTE
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V)
        {
            // 有效PTE：获取下一级页表物理地址
            pagetable = (pagetable_t)PTE2PA(*pte);
        }
        else
        {
            // 无效PTE且需要分配
            if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
                return 0;
            // 初始化新页表
            memset(pagetable, 0, PGSIZE);
            // 设置PTE（物理地址+有效位）
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    // 返回L0层级的PTE
    return &pagetable[PX(0, va)];
}

/*
 * 用户虚拟地址转物理地址
 * 仅用于用户空间地址转换
 *
 * 返回：物理地址，未映射或无效返回0
 */
uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;
    uint64 pa;

    if (va >= MAXVA)
        return 0;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        return 0;
    // 检查有效位
    if ((*pte & PTE_V) == 0)
        return 0;
    // 检查用户权限位
    if ((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    return pa;
}

/*
 * 内核页表映射函数
 * 仅在内核启动时使用，不刷新TLB
 *
 * 参数：
 *   va: 虚拟地址
 *   pa: 物理地址
 *   sz: 映射区域大小
 *   perm: 权限标志
 */
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
    if (mappages(kernel_pagetable, va, sz, pa, perm) != 0)
        panic("kvmmap");
}

/*
 * 内核虚拟地址转物理地址
 * 仅用于内核栈地址转换
 * 要求va按页对齐
 */
uint64 kvmpa(uint64 va)
{
    uint64 off = va % PGSIZE;
    pte_t *pte;
    uint64 pa;

    pte = walk(kernel_pagetable, va, 0);
    if (pte == 0)
        panic("kvmpa");
    if ((*pte & PTE_V) == 0)
        panic("kvmpa");
    pa = PTE2PA(*pte);
    return pa + off; // 添加页内偏移
}

/*
 * 创建页表映射/页表映射实现
 * 为虚拟地址范围[va, va+size)建立到物理地址[pa, pa+size)的映射
 *
 * 返回：0成功，-1失败（页表分配失败）
 */
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
    uint64 a, last;
    pte_t *pte;

    // 按页对齐地址  虚拟地址
    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;)
    {
        // 获取PTE指针
        if ((pte = walk(pagetable, a, 1)) == 0)
            return -1;
        // 检查是否已映射
        if (*pte & PTE_V)
            panic("remap");
        // 设置PTE（物理地址+权限+有效位）
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

/*
 * 取消页表映射
 * 移除虚拟地址[va, va+npages*PGSIZE)的映射
 *
 * 参数：
 *   do_free: 是否释放对应的物理页
 */
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
        // 确保是叶子PTE（非页目录项）
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        if (do_free)
        {
            uint64 pa = PTE2PA(*pte);
            kfree((void *)pa);
        }
        // 清除PTE
        *pte = 0;
    }
}

/*
 * 创建空用户页表
 * 返回：页表指针，内存不足返回0
 */
pagetable_t uvmcreate()
{
    pagetable_t pagetable;
    pagetable = (pagetable_t)kalloc();
    if (pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

/*
 * 初始化用户页表（用于第一个进程）
 * 将初始化代码加载到用户地址0处
 * 要求：sz < PGSIZE
 */
void uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
    char *mem;

    if (sz >= PGSIZE)
        panic("inituvm: more than a page");
    mem = kalloc();         // 分配一页物理内存
    memset(mem, 0, PGSIZE); // 清空页面内容

    // 映射到用户地址0，具有用户权限；将用户虚拟地址0映射到物理页mem
    mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);

    // 复制初始化代码；即将initcode复制到新分配的物理页
    memmove(mem, src, sz);
}

/*
 * 扩展用户内存空间
 * 从oldsz扩展到newsz，分配物理内存并建立映射
 *
 * 返回：新的大小，失败返回0
 */
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    char *mem;
    uint64 a;

    if (newsz < oldsz)
        return oldsz;

    oldsz = PGROUNDUP(oldsz);
    // 逐页分配
    for (a = oldsz; a < newsz; a += PGSIZE)
    {
        mem = kalloc();
        if (mem == 0)
        {
            // 失败时回滚
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        // 映射新页面（用户可访问）
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0)
        {
            kfree(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }
    return newsz;
}

/*
 * 收缩用户内存空间
 * 从oldsz缩减到newsz，释放多余物理页
 *
 * 返回：新的大小
 */
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    if (newsz >= oldsz)
        return oldsz;

    // 计算需要释放的页数
    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
    {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }

    return newsz;
}

/*
 * 递归释放页表
 * 要求：所有叶子映射必须已被移除
 */
void freewalk(pagetable_t pagetable)
{
    // 遍历页表中的512个条目
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = pagetable[i];
        // 有效且非叶子（页目录项）
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
        {
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child);
            pagetable[i] = 0;
        }
        else if (pte & PTE_V)
        {
            // 发现未移除的叶子映射
            panic("freewalk: leaf");
        }
    }
    // 释放当前页表
    kfree((void *)pagetable);
}

/*
 * 释放用户内存空间和页表
 */
void uvmfree(pagetable_t pagetable, uint64 sz)
{
    if (sz > 0)
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    freewalk(pagetable);
}

/*
 * 复制用户页表内容（fork使用）
 * 复制父进程页表内容到子进程页表
 *
 * 返回：0成功，-1失败（失败时会回滚）
 */
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    pte_t *pte;
    uint64 pa, i;
    uint flags;
    char *mem;

    // 逐页复制
    for (i = 0; i < sz; i += PGSIZE)
    {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        // 分配新物理页
        if ((mem = kalloc()) == 0)
            goto err;
        // 复制页面内容
        memmove(mem, (char *)pa, PGSIZE);
        // 映射到子进程
        if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
        {
            kfree(mem);
            goto err;
        }
    }
    return 0;

err:
    // 失败时释放已分配的资源
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
}

/*
 * 清除用户访问权限（用于exec的栈保护页）
 */
void uvmclear(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    // 清除用户权限位
    *pte &= ~PTE_U;
}

/*
 * 内核到用户空间拷贝（copyout）
 * 将内核数据复制到用户空间
 *
 * 返回：0成功，-1失败（地址无效）
 */
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
    uint64 n, va0, pa0;

    while (len > 0)
    {
        va0 = PGROUNDDOWN(dstva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        // 计算当前页可拷贝字节数
        n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        // 执行拷贝（内核地址->用户物理地址）
        memmove((void *)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

/*
 * 用户空间到内核拷贝（copyin）
 * 将用户数据复制到内核空间
 *
 * 返回：0成功，-1失败（地址无效）
 */
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
    uint64 n, va0, pa0;

    while (len > 0)
    {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > len)
            n = len;
        // 执行拷贝（用户物理地址->内核地址）
        memmove(dst, (void *)(pa0 + (srcva - va0)), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

/*
 * 用户空间字符串到内核拷贝（copyinstr）
 * 复制用户空间字符串到内核缓冲区，直到遇到空字符或达到max
 *
 * 返回：0成功，-1失败（地址无效或未找到空字符）
 */
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
    uint64 n, va0, pa0;
    int got_null = 0;

    while (got_null == 0 && max > 0)
    {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > max)
            n = max;

        char *p = (char *)(pa0 + (srcva - va0));
        while (n > 0)
        {
            if (*p == '\0')
            {
                *dst = '\0';
                got_null = 1;
                break;
            }
            else
            {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    // 根据是否遇到空字符返回结果
    if (got_null)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

// 递归打印页表信息
int pgtblprint(pagetable_t pagetable, int depth)
{
    for (int i = 0; i < 512; ++i)
    {
        pte_t pte = pagetable[i];
        if (pte & PTE_V) // 如果该页表中存放的地址有效  pte:0x00...地址
        {
            // 按格式打印页表项
            printf("..");
            for (int j = 0; j < depth; ++j)
            {
                printf("..");
            }
            // 第几级页表的第i个  页表中存放的地址
            printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));

            // 如果该页表中存放的地址pte不是叶子结点，递归打印其子节点
            if (pte & (PTE_R | PTE_W | PTE_X))
            {
                uint64 child = PTE2PA(pte);
                pgtblprint((pagetable_t)child, depth + 1);
            }
        }
    }
    return 0;
}

// vmprint
int vmprint(pagetable_t pagetable)
{
    printf("page table %p\n", pagetable);
    return pgtblprint(pagetable, 0);
}