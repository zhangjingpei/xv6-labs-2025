#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64 sys_exit(void)
{
    int n;
    if (argint(0, &n) < 0)
        return -1;
    exit(n);
    return 0; // not reached
}

uint64 sys_getpid(void)
{
    return myproc()->pid;
}

uint64 sys_fork(void)
{
    return fork();
}

uint64 sys_wait(void)
{
    uint64 p;
    if (argaddr(0, &p) < 0)
        return -1;
    return wait(p);
}

uint64 sys_sbrk(void)
{
    int addr;
    int n;

    if (argint(0, &n) < 0)
        return -1;
    addr = myproc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

uint64 sys_sleep(void)
{
    int n;
    uint ticks0;

    if (argint(0, &n) < 0)
        return -1;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n)
    {
        if (myproc()->killed)
        {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

uint64 sys_kill(void)
{
    int pid;

    if (argint(0, &pid) < 0)
        return -1;
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}

uint64 sys_trace(void)
{
    int mask;

    if (argint(0, &mask) < 0) // 通过读取进程的trapframe，获取mask参数
        return -1;
    myproc()->syscall_trace = mask; // 为该进程的syscall_trace赋值mask
    return 0;
}

uint64 sys_sysinfo(void)
{
    // 从用户态传入一个指针，作为存放sysinfo结构的缓冲区
    uint64 addr;
    if (argaddr(0, &addr) < 0)
    {
        return -1;
    }

    struct sysinfo sinfo;
    sinfo.freemem = count_free_mem();
    sinfo.nproc = count_process();

    // 使用copyout，结合当前进程的页表，获得进程传进来的指针（sysinfo*）（逻辑地址）从而计算出对应的物理地址
    // 然后将 &sinfo中的数据复制到该指针所指位置，供用户进程使用
    if (copyout(myproc()->pagetable, addr, (char *)&sinfo, sizeof(sinfo)) < 0)
        return -1;
    return 0;
}