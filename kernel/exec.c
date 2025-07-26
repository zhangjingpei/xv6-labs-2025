#include "types.h"     // 包含基本类型定义
#include "param.h"     // 包含系统参数定义
#include "memlayout.h" // 包含内存布局定义
#include "riscv.h"     // 包含RISC-V架构相关定义
#include "spinlock.h"  // 包含自旋锁定义
#include "proc.h"      // 包含进程控制块定义
#include "defs.h"      // 包含通用定义
#include "elf.h"       // 包含ELF格式定义

// 声明段加载函数
static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);

// 执行新程序（替换当前进程镜像）
// 参数：path - 可执行文件路径，argv - 命令行参数数组
// 返回值：成功返回参数个数，失败返回-1
int exec(char *path, char **argv)
{
    char *s, *last;
    int i, off;
    uint64 argc, sz = 0, sp, ustack[MAXARG + 1], stackbase; // sz: 当前分配内存大小
    struct elfhdr elf;                                      // ELF文件头
    struct inode *ip;                                       // 文件inode指针
    struct proghdr ph;                                      // 程序段头
    pagetable_t pagetable = 0, oldpagetable;                // 新/旧页表
    struct proc *p = myproc();                              // 获取当前进程指针

    // 开始文件系统操作（避免并发冲突）
    begin_op();

    // 通过路径查找文件inode
    if ((ip = namei(path)) == 0)
    {
        end_op();
        return -1; // 文件不存在
    }
    ilock(ip); // 锁定inode

    // 读取并验证ELF文件头
    if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
        goto bad; // 读取失败
    if (elf.magic != ELF_MAGIC)
        goto bad; // 非有效ELF文件

    // 为进程创建新页表
    if ((pagetable = proc_pagetable(p)) == 0)
        goto bad;

    // 加载所有程序段到内存
    for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph))
    {
        // 读取程序段头
        if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
            goto bad;
        if (ph.type != ELF_PROG_LOAD) // 只加载可加载段
            continue;
        // 验证段元数据
        if (ph.memsz < ph.filesz) // 内存大小不能小于文件大小
            goto bad;
        if (ph.vaddr + ph.memsz < ph.vaddr) // 地址溢出检查
            goto bad;

        // 分配虚拟内存空间
        uint64 sz1;
        if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
            goto bad;
        sz = sz1; // 更新已分配内存大小

        // 验证虚拟地址对齐
        if (ph.vaddr % PGSIZE != 0)
            goto bad;

        // 加载段内容到内存
        if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
            goto bad;
    }
    iunlockput(ip); // 释放文件inode
    end_op();       // 结束文件系统操作
    ip = 0;         // 清空inode指针

    // 重新获取当前进程（可能已切换）
    p = myproc();
    uint64 oldsz = p->sz; // 保存旧内存大小

    // 分配用户栈空间（2页）
    sz = PGROUNDUP(sz); // 按页对齐
    uint64 sz1;
    if ((sz1 = uvmalloc(pagetable, sz, sz + 2 * PGSIZE)) == 0)
        goto bad;
    sz = sz1;
    uvmclear(pagetable, sz - 2 * PGSIZE); // 清除栈页标志
    sp = sz;                              // 栈指针
    stackbase = sp - PGSIZE;              // 栈基址（第二页）

    // 将命令行参数压入用户栈
    for (argc = 0; argv[argc]; argc++)
    {
        if (argc >= MAXARG) // 超过最大参数数
            goto bad;
        // 计算参数字符串位置（16字节对齐）
        sp -= strlen(argv[argc]) + 1;
        sp -= sp % 16;
        if (sp < stackbase) // 栈溢出检查
            goto bad;
        // 复制参数字符串到用户空间
        if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
            goto bad;
        ustack[argc] = sp; // 记录参数地址
    }
    ustack[argc] = 0; // 参数列表结尾标记

    // 压入参数指针数组
    sp -= (argc + 1) * sizeof(uint64);
    sp -= sp % 16; // 保持16字节对齐
    if (sp < stackbase)
        goto bad;
    if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0)
        goto bad;

    // 设置用户main函数参数
    p->trapframe->a1 = sp; // argv指针存入a1寄存器

    // 提取程序名用于调试
    for (last = s = path; *s; s++)
        if (*s == '/')
            last = s + 1;                       // 跳过路径保留文件名
    safestrcpy(p->name, last, sizeof(p->name)); // 复制到进程名

    // 内核页表处理
    uvmunmap(p->kpgtbl, 0, PGROUNDUP(oldsz) / PGSIZE, 0); // 取消旧映射
    // 将新用户页表复制到内核页表
    if ((u2kvmcopy(pagetable, p->kpgtbl, 0, sz)) < 0)
    {
        goto bad;
    }

    // 提交新进程镜像
    oldpagetable = p->pagetable;   // 保存旧页表
    p->pagetable = pagetable;      // 更新进程页表
    p->sz = sz;                    // 更新进程内存大小
    p->trapframe->epc = elf.entry; // 设置入口地址（main函数）
    p->trapframe->sp = sp;         // 设置栈指针

    // 释放旧页表及相关资源
    proc_freepagetable(oldpagetable, oldsz);

    // 特殊处理：如果是第一个进程则打印页表
    if (p->pid == 1)
        vmprint(p->pagetable);

    return argc; // 返回值存入a0寄存器（main函数argc参数）

// 错误处理标签
bad:
    if (pagetable)
        proc_freepagetable(pagetable, sz); // 释放新建页表
    if (ip)
    {
        iunlockput(ip); // 释放文件inode
        end_op();       // 结束文件系统操作
    }
    return -1; // 返回错误
}

// 加载程序段到页表
// 参数：pagetable - 目标页表，va - 虚拟地址，ip - 文件inode，offset - 文件偏移，sz - 段大小
// 返回：成功0，失败-1
static int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
    uint i, n;
    uint64 pa;

    // 验证地址页对齐
    if ((va % PGSIZE) != 0)
        panic("loadseg: va must be page aligned");

    // 逐页加载内容
    for (i = 0; i < sz; i += PGSIZE)
    {
        // 获取物理地址
        pa = walkaddr(pagetable, va + i);
        if (pa == 0)
            panic("loadseg: address should exist"); // 地址未映射

        // 计算当前页实际加载大小
        if (sz - i < PGSIZE)
            n = sz - i; // 最后一页可能不满
        else
            n = PGSIZE;

        // 从文件读取内容到物理内存
        if (readi(ip, 0, (uint64)pa, offset + i, n) != n)
            return -1; // 读取失败
    }
    return 0; // 成功返回
}