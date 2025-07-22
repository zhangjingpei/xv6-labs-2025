#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__((aligned(16))) char stack0[4096 * NCPU];

// a scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[NCPU][5];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void start()
{
    // set M Previous Privilege mode to Supervisor, for mret.
    // mstatus (机器模式状态寄存器)
    unsigned long x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK; // MPP_MASK：清除之前的特权级设置（如 MPP=11 表示机器模式）
    x |= MSTATUS_MPP_S;     // MPP_S：设置新的特权级为监管者模式（MPP=01）
    w_mstatus(x);           // 执行 mret 后 CPU 将进入监管者模式

    // set M Exception Program Counter to main, for mret.
    // requires gcc -mcmodel=medany
    w_mepc((uint64)main); // 指定 mret 后跳转的目标地址

    // disable paging for now.
    w_satp(0); // 在初始化阶段禁用分页机制

    // delegate all interrupts and exceptions to supervisor mode.
    w_medeleg(0xffff);                               // 委托所有异常
    w_mideleg(0xffff);                               // 委托所有中断
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE); //// 使能关键中断

    // ask for clock interrupts.
    timerinit(); // 设置时钟中断以支持分时调度

    // keep each CPU's hartid in its tp register, for cpuid().
    int id = r_mhartid(); // mhartid：机器模式硬件线程 ID（只读）
    w_tp(id);             // tp (线程指针寄存器)：软件可写，用作核心 ID 存储

    // switch to supervisor mode and jump to main().
    asm volatile("mret");
}

// set up to receive timer interrupts in machine mode,
// which arrive at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void timerinit()
{
    // each CPU has a separate source of timer interrupts.
    int id = r_mhartid();

    // ask the CLINT for a timer interrupt.
    int interval = 1000000; // cycles; about 1/10th second in qemu.
    *(uint64 *)CLINT_MTIMECMP(id) = *(uint64 *)CLINT_MTIME + interval;

    // prepare information in scratch[] for timervec.
    // scratch[0..2] : space for timervec to save registers.
    // scratch[3] : address of CLINT MTIMECMP register.
    // scratch[4] : desired interval (in cycles) between timer interrupts.
    uint64 *scratch = &timer_scratch[id][0];
    scratch[3] = CLINT_MTIMECMP(id);
    scratch[4] = interval;
    w_mscratch((uint64)scratch);

    // set the machine-mode trap handler.
    w_mtvec((uint64)timervec);

    // enable machine-mode interrupts.
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // enable machine-mode timer interrupts.
    w_mie(r_mie() | MIE_MTIE);
}
