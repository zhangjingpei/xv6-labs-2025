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
    // 检查trap原因
    int which_dev = 0;

    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

    // send interrupts and exceptions to kerneltrap(),
    // since we're now in the kernel.
    // 后续内核中的 trap 将直接跳转到kernelvec，而非用户空间的uservec。
    w_stvec((uint64)kernelvec);

    struct proc *p = myproc();

    // save user program counter.
    // 用户程序被 trap打断时，sepc寄存器保存了被打断的指令地址（如系统调用的ecall指令地址）。
    // usertrap将其保存到进程的trapframe中，供后续返回时恢复：
    p->trapframe->epc = r_sepc();

    if (r_scause() == 8)
    {
        // system call

        if (p->killed)
            exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        //// 调整epc到ecall的下一条指令
        p->trapframe->epc += 4;

        // an interrupt will change sstatus &c registers,
        // so don't enable until done with those registers.
        // // 允许中断（处理完关键寄存器后）
        intr_on();

        syscall();
    }
    else if ((which_dev = devintr()) != 0)
    {
        // 中断已被devintr处理（如UART数据读取、定时器计数）
        // ok
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
    // 若 trap 是定时器中断（which_dev == 2），
    // 则调用yield()主动让出 CPU，触发进程调度：
    if (which_dev == 2)
        yield();

    // 所有处理完成后，调用usertrapret()完成从内核到用户空间的切换。
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
    // 内核返回用户空间的过程中，trap 处理入口正在切换（从kernelvec切换到uservec），此时需要关闭中断
    // 关闭中断，避免返回过程中被干扰
    intr_off();

    // send syscalls, interrupts, and exceptions to trampoline.S
    // 修改stvec寄存器，让未来用户空间的 trap 重新指向uservec（位于 trampoline 页）：
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
    // 调整SSTATUS寄存器，准备切换回用户模式：
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode // 清除SPP标志（表示下次返回用户模式）
    x |= SSTATUS_SPIE; // enable interrupts in user mode  // 允许用户模式下的中断（返回后生效）
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    // 将sepc寄存器设置为之前保存的用户程序计数器（p->trapframe->epc），确保返回后从被打断的位置继续执行：
    w_sepc(p->trapframe->epc);

    // tell trampoline.S the user page table to switch to.
    // 最后，调用 trampoline 页中的userret汇编代码，完成页表切换和寄存器恢复：
    uint64 satp = MAKE_SATP(p->pagetable); // 用户页表的satp值

    // jump to trampoline.S at the top of memory, which
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    uint64 fn = TRAMPOLINE + (userret - trampoline); // userret在trampoline页的地址
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp); // 调用userret
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
