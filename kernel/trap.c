/*
 * trap.c
 * 该文件主要负责xv6内核中与中断、异常和系统调用相关的处理逻辑。
 * 主要功能包括：
 * 1. 初始化trap相关的锁和中断向量表。
 * 2. 处理来自用户态和内核态的中断、异常和系统调用。
 * 3. 处理中断分发（如外部设备中断、定时器中断等）。
 * 4. 实现从内核态返回用户态的上下文切换。
 * 5. 维护时钟节拍ticks并唤醒等待ticks的进程。
 */

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

/*
 * 文件功能：中断、异常和系统调用处理核心
 * 包含以下主要功能：
 * 1. 初始化时钟中断锁 (trapinit)
 * 2. 设置内核陷阱向量 (trapinithart)
 * 3. 用户空间陷阱处理 (usertrap)
 * 4. 用户空间陷阱返回 (usertrapret)
 * 5. 内核空间陷阱处理 (kerneltrap)
 * 6. 时钟中断处理 (clockintr)
 * 7. 设备中断处理 (devintr)
 */

// 初始化时钟中断的自旋锁
void trapinit(void)
{
    initlock(&tickslock, "time");
}

// 设置当前CPU的陷阱向量地址为内核陷阱处理函数
void trapinithart(void)
{
    w_stvec((uint64)kernelvec); // 将stvec寄存器设置为kernelvec的地址
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
/* 用户空间陷阱处理函数 */
void usertrap(void)
{
    int which_dev = 0;

    // 检查是否来自用户模式（SPP位应为0）
    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

    // 将陷阱向量设置为内核处理函数（现在处于内核态）
    w_stvec((uint64)kernelvec);

    struct proc *p = myproc(); // 获取当前进程

    // 保存用户程序计数器
    p->trapframe->epc = r_sepc();

    // 根据陷阱原因(scause)处理不同类型的事件
    if (r_scause() == 8) // 系统调用
    {
        // 如果进程已被标记为终止，则退出
        if (p->killed)
            exit(-1);

        // 跳过ecall指令（4字节），返回到下一条指令
        p->trapframe->epc += 4;

        // 在完成关键寄存器操作后启用中断
        intr_on();

        syscall(); // 执行系统调用
    }
    else if ((which_dev = devintr()) != 0) // 设备中断
    {
        // 设备中断已处理
    }
    else // 未知陷阱类型
    {
        // 打印错误信息并终止进程
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        p->killed = 1;
    }

    // 如果进程被标记为终止，则退出
    if (p->killed)
        exit(-1);

    // 如果是时钟中断，让出CPU
    if (which_dev == 2)
        yield();

    // 返回用户空间
    usertrapret();
}

//
// return to user space
//
/* 准备并返回到用户空间 */
void usertrapret(void)
{
    struct proc *p = myproc();

    // 在切换陷阱向量前禁用中断
    intr_off();

    // 将陷阱向量设置为用户空间处理程序（位于trampoline页）
    w_stvec(TRAMPOLINE + (uservec - trampoline));

    // 设置下次进入内核时需要的陷阱帧信息
    p->trapframe->kernel_satp = r_satp();         // 内核页表
    p->trapframe->kernel_sp = p->kstack + PGSIZE; // 内核栈顶
    p->trapframe->kernel_trap = (uint64)usertrap; // 用户陷阱处理函数
    p->trapframe->kernel_hartid = r_tp();         // CPU核心ID

    // 配置sstatus寄存器用于返回到用户空间
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // 清除SPP位（0=用户模式）
    x |= SSTATUS_SPIE; // 在用户模式启用中断
    w_sstatus(x);

    // 设置返回地址为用户程序计数器
    w_sepc(p->trapframe->epc);

    // 生成用户页表的SATP值
    uint64 satp = MAKE_SATP(p->pagetable);

    // 跳转到trampoline中的userret函数
    uint64 fn = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp); // 参数：陷阱帧地址和页表
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
/* 内核空间陷阱处理函数 */
void kerneltrap()
{
    int which_dev = 0;
    // 保存关键寄存器状态
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    // 安全验证：必须来自监管模式且中断已禁用
    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");
    if (intr_get() != 0)
        panic("kerneltrap: interrupts enabled");

    // 尝试处理设备中断
    if ((which_dev = devintr()) == 0) // 未知陷阱
    {
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // 如果是时钟中断且进程在运行状态，让出CPU
    if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
        yield();

    // 恢复陷阱前的寄存器状态
    w_sepc(sepc);
    w_sstatus(sstatus);
}

/* 时钟中断处理函数 */
void clockintr()
{
    acquire(&tickslock); // 获取时钟锁
    ticks++;             // 增加全局时钟计数
    wakeup(&ticks);      // 唤醒等待时钟的进程
    release(&tickslock); // 释放时钟锁
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
/* 设备中断处理函数 */
int devintr()
{
    uint64 scause = r_scause();

    // 处理外部中断（最高位为1）
    if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9)
    {
        // 通过PLIC获取中断设备号
        int irq = plic_claim();

        // 根据设备号处理不同类型的中断
        if (irq == UART0_IRQ)
        {
            uartintr(); // 串口中断
        }
        else if (irq == VIRTIO0_IRQ)
        {
            virtio_disk_intr(); // 磁盘中断
        }
        else if (irq)
        {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // 通知PLIC已完成中断处理
        if (irq)
            plic_complete(irq);

        return 1; // 返回设备中断标识
    }
    // 处理软件中断（时钟中断）
    else if (scause == 0x8000000000000001L)
    {
        // 仅在CPU0上处理时钟中断
        if (cpuid() == 0)
        {
            clockintr();
        }

        // 清除软件中断挂起位
        w_sip(r_sip() & ~2);

        return 2; // 返回时钟中断标识
    }
    else
    {
        return 0; // 未知中断类型
    }
}