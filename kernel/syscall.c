#include "types.h"     // 基本类型定义
#include "param.h"     // 系统参数（如最大进程数）
#include "memlayout.h" // 内存布局定义
#include "riscv.h"     // RISC-V 架构相关定义
#include "spinlock.h"  // 自旋锁实现
#include "proc.h"      // 进程控制块(PCB)定义
#include "syscall.h"   // 系统调用号定义
#include "defs.h"      // 通用定义

/*
 * 从当前进程地址空间中获取指定地址的64位值
 * @param addr 用户空间虚拟地址
 * @param ip   存储结果的指针
 * @return 成功返回0，失败返回-1（地址无效或复制失败）
 */
int fetchaddr(uint64 addr, uint64 *ip)
{
    struct proc *p = myproc(); // 获取当前进程

    // 检查地址是否在进程内存范围内
    if (addr >= p->sz || addr + sizeof(uint64) > p->sz)
        return -1;

    // 从用户页表复制数据到内核空间
    if (copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
        return -1;

    return 0;
}

/*
 * 从当前进程地址空间获取以null结尾的字符串
 * @param addr 用户空间虚拟地址
 * @param buf  存储结果的缓冲区
 * @param max  缓冲区最大长度
 * @return 成功返回字符串长度（不含null），失败返回-1
 */
int fetchstr(uint64 addr, char *buf, int max)
{
    struct proc *p = myproc();
    // 从用户空间复制字符串
    int err = copyinstr(p->pagetable, buf, addr, max);
    if (err < 0)
        return err;

    // 返回实际字符串长度
    return strlen(buf);
}

/*
 * 直接从陷阱帧中获取原始系统调用参数
 * @param n 参数索引（0-5）
 * @return 参数值
 */
static uint64 argraw(int n)
{
    struct proc *p = myproc(); // 获取当前进程
    switch (n)
    {
    case 0:
        return p->trapframe->a0; // 第1个参数
    case 1:
        return p->trapframe->a1; // 第2个参数
    case 2:
        return p->trapframe->a2; // 第3个参数
    case 3:
        return p->trapframe->a3; // 第4个参数
    case 4:
        return p->trapframe->a4; // 第5个参数
    case 5:
        return p->trapframe->a5; // 第6个参数
    }
    panic("argraw"); // 索引越界触发内核崩溃
    return -1;
}

/*
 * 获取整数类型的系统调用参数
 * @param n  参数索引
 * @param ip 存储结果的指针
 * @return 总是返回0（简化接口）
 */
int argint(int n, int *ip)
{
    *ip = argraw(n); // 获取原始值并转为int
    return 0;
}

/*
 * 获取指针类型的系统调用参数
 * @param n  参数索引
 * @param ip 存储地址的指针
 * @return 总是返回0
 * 注意：不检查地址合法性，后续copyin/copyout会处理
 */
int argaddr(int n, uint64 *ip)
{
    *ip = argraw(n); // 获取原始地址值
    return 0;
}

/*
 * 获取字符串类型的系统调用参数
 * @param n    参数索引
 * @param buf  存储结果的缓冲区
 * @param max  缓冲区最大长度
 * @return 成功返回字符串长度（含null），失败返回-1
 */
int argstr(int n, char *buf, int max)
{
    uint64 addr;
    // 先获取字符串地址
    if (argaddr(n, &addr) < 0)
        return -1;

    // 从用户空间获取字符串
    return fetchstr(addr, buf, max);
}

/********************************************
 * 系统调用处理函数声明（实现在sys*.c文件中）syscall.c sysfile.c sysproc.c
 ********************************************/
extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);

/*
 * 系统调用跳转表---函数指针数组
 * 使用系统调用号作为索引，映射到对应的处理函数
 */
static uint64 (*syscalls[])(void) = {
    [SYS_fork] sys_fork,     // 0: 创建进程
    [SYS_exit] sys_exit,     // 1: 终止进程
    [SYS_wait] sys_wait,     // 2: 等待子进程
    [SYS_pipe] sys_pipe,     // 3: 创建管道
    [SYS_read] sys_read,     // 4: 读取文件
    [SYS_kill] sys_kill,     // 5: 终止进程
    [SYS_exec] sys_exec,     // 6: 执行程序
    [SYS_fstat] sys_fstat,   // 7: 获取文件状态
    [SYS_chdir] sys_chdir,   // 8: 更改目录
    [SYS_dup] sys_dup,       // 9: 复制文件描述符
    [SYS_getpid] sys_getpid, // 10: 获取进程ID
    [SYS_sbrk] sys_sbrk,     // 11: 调整堆大小
    [SYS_sleep] sys_sleep,   // 12: 进程休眠
    [SYS_uptime] sys_uptime, // 13: 获取运行时间
    [SYS_open] sys_open,     // 14: 打开文件
    [SYS_write] sys_write,   // 15: 写入文件
    [SYS_mknod] sys_mknod,   // 16: 创建设备文件
    [SYS_unlink] sys_unlink, // 17: 删除文件
    [SYS_link] sys_link,     // 18: 创建硬链接
    [SYS_mkdir] sys_mkdir,   // 19: 创建目录
    [SYS_close] sys_close,   // 20: 关闭文件
};

/*
 * 系统调用入口函数
 * 从用户态陷入内核时由trap.c调用
 */
void syscall(void)
{
    int num;
    struct proc *p = myproc(); // 获取当前进程

    // 从陷阱帧的a7寄存器获取系统调用号
    num = p->trapframe->a7;

    // 检查系统调用号是否有效
    if (num > 0 && num < NELEM(syscalls) && syscalls[num])
    {
        // 调用对应的系统调用处理函数
        // 返回值存储到陷阱帧的a0寄存器（用户态获取返回值的位置）
        p->trapframe->a0 = syscalls[num]();
    }
    else
    {
        // 无效系统调用处理
        printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
        p->trapframe->a0 = -1; // 设置错误返回值
    }
}   