#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// 系统调用：exit - 终止当前进程
// 参数：退出状态码
// 返回值：永不返回（进程终止）
uint64 sys_exit(void)
{
    int n;
    // argint(0, &n): 从用户空间获取第0个整数参数（退出状态码）
    if (argint(0, &n) < 0)
        return -1; // 参数获取失败
    exit(n);       // 调用进程退出函数
    return 0;      // 实际不会执行到这里（进程已终止）
}

// 系统调用：getpid - 获取当前进程ID
// 返回值：当前进程的PID
uint64 sys_getpid(void)
{
    // myproc(): 获取当前进程的进程控制块（PCB）
    return myproc()->pid; // 返回PID字段
}

// 系统调用：fork - 创建子进程
// 返回值：子进程返回0，父进程返回子进程PID，错误返回-1
uint64 sys_fork(void)
{
    return fork(); // 调用进程复制函数
}

// 系统调用：wait - 等待子进程退出
// 参数：指向用户空间status变量的指针
// 返回值：成功返回子进程PID，错误返回-1
uint64 sys_wait(void)
{
    uint64 p;
    // argaddr(0, &p): 获取第0个参数作为用户空间地址
    if (argaddr(0, &p) < 0)
        return -1;
    return wait(p); // p是用户空间用于存储退出状态的地址
}

// 系统调用：sbrk - 增加进程内存空间
// 参数：要增加的内存字节数（可为负数）
// 返回值：扩容前的堆顶地址（旧break指针）
uint64 sys_sbrk(void)
{
    int addr;
    int n;

    // 获取要调整的内存大小
    if (argint(0, &n) < 0)
        return -1;

    // 获取当前进程的内存大小
    addr = myproc()->sz;

    // growproc(n): 调整进程内存空间
    // 成功返回0，失败返回-1（如内存不足）
    if (growproc(n) < 0)
        return -1;

    return addr; // 返回扩容前的堆顶地址
}

// 系统调用：sleep - 使进程休眠指定时间
// 参数：休眠的时钟滴答数
// 返回值：成功返回0，被中断返回-1
uint64 sys_sleep(void)
{
    int n;
    uint ticks0; // 起始滴答数

    // 获取休眠时长参数
    if (argint(0, &n) < 0)
        return -1;

    // 获取全局ticks锁（保护ticks变量）
    acquire(&tickslock);

    // 记录当前全局时钟滴答数
    ticks0 = ticks;

    // 循环等待直到达到指定时长
    while (ticks - ticks0 < n)
    {
        // 检查进程是否被标记为终止
        if (myproc()->killed)
        {
            release(&tickslock);
            return -1; // 被终止则提前返回
        }
        // sleep(&ticks, &tickslock): 使进程休眠
        // 会自动释放tickslock并在唤醒时重新获取
        sleep(&ticks, &tickslock);
    }

    // 释放ticks锁
    release(&tickslock);
    return 0; // 正常休眠完成
}

// 系统调用：kill - 向指定进程发送终止信号
// 参数：目标进程PID
// 返回值：成功返回0，失败返回-1
uint64 sys_kill(void)
{
    int pid;
    // 获取目标PID参数
    if (argint(0, &pid) < 0)
        return -1;
    return kill(pid); // 调用进程终止函数
}

// 系统调用：uptime - 获取系统启动后的时钟滴答数
// 返回值：自系统启动以来的时钟中断次数
uint64 sys_uptime(void)
{
    uint xticks;

    // 保护ticks变量的原子访问
    acquire(&tickslock);
    xticks = ticks; // 读取全局时钟计数器
    release(&tickslock);
    return xticks;
}