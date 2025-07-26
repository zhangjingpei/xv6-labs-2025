#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"
#include "file.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void)
{
    initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void)
{
    w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void)
{
    int which_dev = 0;

    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

    // send interrupts and exceptions to kerneltrap(),
    // since we're now in the kernel.
    w_stvec((uint64)kernelvec);

    struct proc *p = myproc();

    // save user program counter.
    p->trapframe->epc = r_sepc();

    if (r_scause() == 8)
    {
        // system call

        if (p->killed)
            exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        p->trapframe->epc += 4;

        // an interrupt will change sstatus &c registers,
        // so don't enable until done with those registers.
        intr_on();

        syscall();
    }
    else if ((which_dev = devintr()) != 0)
    {
        // ok
    }
    else if (r_scause() == 13 || r_scause() == 15)
    {
        // 缺页异常：13=加载页故障（读未映射地址），15=存储页故障（写未映射地址）
        uint64 va = r_stval(); // 获取故障地址（stval寄存器存储故障虚拟地址）
        struct vma *vma = 0;   // 用于查找对应的vma结构

        // 检查故障地址是否合法：必须在用户空间范围内（小于进程大小且高于栈顶）
        if (va >= p->sz || va < p->trapframe->sp)
        {
            goto killing; // 地址非法，终止进程
        }

        // 在进程vmas数组中查找包含va的vma（遍历所有vma）
        for (int i = 0; i < NVMA; i++)
        {
            // 检查va是否在当前vma的映射范围内：[vma.addr, vma.addr + vma.len)
            if (va >= p->vmas[i].addr && va < p->vmas[i].addr + p->vmas[i].len)
            {
                vma = &p->vmas[i]; // 找到匹配的vma
                break;
            }
        }

        if (!vma) // 未找到对应的vma（地址未映射），终止进程
            goto killing;

        va = PGROUNDDOWN(va); // 将故障地址向下对齐到页边界（获取页起始地址）

        // 分配物理页（用于存储从文件读取的数据）
        char *mem = kalloc();
        if (mem == 0) // 内存分配失败，终止进程
            goto killing;
        memset(mem, 0, PGSIZE); // 初始化物理页为0

        // 从文件读取数据到物理页：读取vma对应的文件内容
        ilock(vma->file->ip); // 锁定文件inode（确保文件数据不被并发修改）
        // 读取范围：从文件偏移(vma->offset + va - vma->addr)开始，读取PGSIZE字节
        readi(vma->file->ip, 0, (uint64)mem, vma->offset + (va - vma->addr), PGSIZE);
        iunlock(vma->file->ip); // 解锁文件inode

        // 根据vma保护权限设置页表项标志（PTE_U表示用户可访问）
        int flags = PTE_U; // 基本标志：用户可访问
        if (vma->prot & PROT_READ)
            flags |= PTE_R; // 读权限
        if (vma->prot & PROT_WRITE)
            flags |= PTE_W; // 写权限
        if (vma->prot & PROT_EXEC)
            flags |= PTE_X; // 执行权限

        // 建立页表映射：将虚拟地址va映射到物理地址mem，权限为flags
        if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, flags) != 0)
        {
            // 映射失败，释放物理页
            kfree(mem);
            goto killing;
        }
        goto rest; // 映射成功，继续执行

    freeing: // 释放物理页（错误处理标签）
        kfree(mem);
    killing:           // 终止进程（错误处理标签）
        p->killed = 1; // 标记进程为"已终止"
    rest:;             // 继续执行标签
    }
    else
    {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        p->killed = 1;
    }

    if (p->killed)
        exit(-1);

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2)
        yield();

    usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
    struct proc *p = myproc();

    // we're about to switch the destination of traps from
    // kerneltrap() to usertrap(), so turn off interrupts until
    // we're back in user space, where usertrap() is correct.
    intr_off();

    // send syscalls, interrupts, and exceptions to trampoline.S
    w_stvec(TRAMPOLINE + (uservec - trampoline));

    // set up trapframe values that uservec will need when
    // the process next re-enters the kernel.
    p->trapframe->kernel_satp = r_satp();         // kernel page table
    p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
    p->trapframe->kernel_trap = (uint64)usertrap;
    p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE; // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(p->trapframe->epc);

    // tell trampoline.S the user page table to switch to.
    uint64 satp = MAKE_SATP(p->pagetable);

    // jump to trampoline.S at the top of memory, which
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    uint64 fn = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap()
{
    int which_dev = 0;
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");
    if (intr_get() != 0)
        panic("kerneltrap: interrupts enabled");

    if ((which_dev = devintr()) == 0)
    {
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
        yield();

    // the yield() may have caused some traps to occur,
    // so restore trap registers for use by kernelvec.S's sepc instruction.
    w_sepc(sepc);
    w_sstatus(sstatus);
}

void clockintr()
{
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr()
{
    uint64 scause = r_scause();

    if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9)
    {
        // this is a supervisor external interrupt, via PLIC.

        // irq indicates which device interrupted.
        int irq = plic_claim();

        if (irq == UART0_IRQ)
        {
            uartintr();
        }
        else if (irq == VIRTIO0_IRQ)
        {
            virtio_disk_intr();
        }
        else if (irq)
        {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // the PLIC allows each device to raise at most one
        // interrupt at a time; tell the PLIC the device is
        // now allowed to interrupt again.
        if (irq)
            plic_complete(irq);

        return 1;
    }
    else if (scause == 0x8000000000000001L)
    {
        // software interrupt from a machine-mode timer interrupt,
        // forwarded by timervec in kernelvec.S.

        if (cpuid() == 0)
        {
            clockintr();
        }

        // acknowledge the software interrupt by clearing
        // the SSIP bit in sip.
        w_sip(r_sip() & ~2);

        return 2;
    }
    else
    {
        return 0;
    }
}
