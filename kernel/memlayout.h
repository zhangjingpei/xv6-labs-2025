/*
 * memlayout.h
 * This file defines the physical and virtual memory layout for xv6 on RISC-V.
 * It includes the addresses of hardware devices, kernel and user memory layout,
 * and macros for stack/trampoline/trapframe locations.
 * 该文件定义了xv6在RISC-V上的物理和虚拟内存布局，
 * 包括硬件设备地址、内核和用户内存布局，以及栈、trampoline、trapframe等宏定义。
 */

// Physical memory layout
// 物理内存布局

//
// qemu -machine virt的内存布局，参考qemu的hw/riscv/virt.c：
// 00001000 -- 启动ROM，由qemu提供
// 02000000 -- CLINT（本地中断控制器）
// 0C000000 -- PLIC（可编程中断控制器）
// 10000000 -- uart0串口
// 10001000 -- virtio磁盘
// 80000000 -- 启动ROM在machine模式跳转到这里，内核加载到这里
// 80000000之后为未使用的RAM

// 内核使用物理内存的方式：
// 80000000 -- entry.S，然后是内核代码和数据
// end -- 内核页分配区的起始
// PHYSTOP -- 内核可用RAM的结束

// qemu将UART寄存器映射到物理内存的这个位置
// QEMU virt 机器内存映射 (参考 hw/riscv/virt.c)
// 设备地址由 QEMU 硬编码，操作系统必须遵守这些地址才能访问硬件
#define UART0 0x10000000L // 串口设备基地址
#define UART0_IRQ 10      // 串口中断号

#define VIRTIO0 0x10001000 // VirtIO 磁盘设备基地址
#define VIRTIO0_IRQ 1      // 磁盘中断号

// virtio磁盘的MMIO接口
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1 // 磁盘中断号

// 核心本地中断控制器 (Core Local Interruptor)
// 处理定时器和核间中断
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8 * (hartid)) // 定时器比较寄存器
#define CLINT_MTIME (CLINT + 0xBFF8)                           // 系统时间计数器 (自启动起的时钟周期数)

// qemu将可编程中断控制器映射到这里
// 处理外部设备中断
// hart = hardware thread = 硬件线程/CPU核心编号
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)                               // 中断优先级寄存器
#define PLIC_PENDING (PLIC + 0x1000)                             // 中断等待状态寄存器
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart) * 0x100)      // 机器模式中断使能
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart) * 0x100)      // 监管模式中断使能
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart) * 0x2000) // 机器模式优先级阈值
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart) * 0x2000) // 监管模式优先级阈值
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart) * 0x2000)    // 机器模式中断声明
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart) * 0x2000)    // 监管模式中断声明

// 内核期望从物理地址0x80000000到PHYSTOP之间有RAM，供内核和用户页使用
#define KERNBASE 0x80000000L                   // 内核起始物理地址 (QEMU 启动后跳转地址) 2GB
#define PHYSTOP (KERNBASE + 128 * 1024 * 1024) // 内核可用内存上限 (128MB) 2Gb+128MB;

// 将trampoline页映射到最高地址，在用户和内核空间都可见
#define TRAMPOLINE (MAXVA - PGSIZE)  // 256GB - 4096B
/*
 * 为什么 TRAMPOLINE 放在最高地址？
 * 1. 所有进程页表共享相同映射：用户空间和内核空间都能访问
 * 2. 避免与用户内存冲突：用户空间不可能使用如此高的地址
 * 3. 安全隔离：下方有保护页防止越界访问
 */

// 内核栈映射在trampoline之下，每个栈两侧有无效保护页
#define KSTACK(p) (TRAMPOLINE - ((p) + 1) * 2 * PGSIZE)
/*
 * 内核栈设计解析：
 * - 每个进程的内核栈大小：2页 (8KB)
 * - 实际使用：1页 (4KB栈空间) + 1页 (保护页)
 * - 排列方式：从高地址向低地址排列
 *   [TRAMPOLINE]
 *   [进程N-1 保护页] -> 不可访问 (防止栈溢出)
 *   [进程N-1 栈页]
 *   [进程N-2 保护页]
 *   [进程N-2 栈页]
 *   ...
 *
 * 优点：
 * 1. 保护页触发页错误，防止栈溢出破坏其他栈
 * 2. 固定地址计算，便于内核管理
 */

// 用户内存布局：
// 从地址0开始：
//   程序代码段
//   原始数据和bss段
//   固定大小的栈
//   可扩展的堆
//   ...
//   TRAPFRAME（p->trapframe，trampoline使用）
//   TRAMPOLINE（与内核中的trampoline页相同）
#define TRAPFRAME (TRAMPOLINE - PGSIZE) // 位于trampoline上方

/*
 * 用户空间内存布局（从低地址到高地址）：
 * 0x0000_0000: ┌─────────────┐
 *              │    text     │ 程序代码
 *              ├─────────────┤
 *              │   data/bss  │ 初始化/未初始化数据
 *              ├─────────────┤
 *              │    stack    │ 向下增长 (固定大小)
 *              ├─────────────┤
 *              │     heap    │ 向上增长 (动态分配)
 *              │      ↓      │
 *              │      ↑      │
 *              ├─────────────┤
 *              │   (free)    │ 未使用空间
 *              ├─────────────┤
 * 0x3FFF_E000: │  TRAPFRAME  │ 陷入帧 (保存用户寄存器状态) 4096
 * 0x3FFF_F000: │  TRAMPOLINE │ 陷入/返回跳板代码
 * 0x4000_0000: └─────────────┘ (MAXVA, Sv39: 0x3F_FFFF_FFFF)
 *
 * 设计原理：
 * 1. TRAMPOLINE 固定地址：所有进程共享相同虚拟地址，切换时无需刷新TLB
 * 2. TRAPFRAME 紧随其后：保存用户上下文，便于内核访问
 * 3. 用户空间隔离：低地址区域相互隔离，高地址区域统一映射
 */