// kernel/riscv.h
//
// RISC-V 架构相关寄存器操作和内存管理定义
// 提供机器模式(M-mode)和监管者模式(S-mode)下控制状态寄存器(CSR)的读写接口
// 定义页表管理相关宏和页表项结构
//
// 该文件包含：
//  1. 核心寄存器读取函数（如mhartid, mstatus等）
//  2. 中断控制相关寄存器操作
//  3. 异常处理寄存器配置
//  4. 虚拟内存管理（SATP, 页表项操作）
//  5. 基本汇编指令封装（如TLB刷新）
//
// 使用场景：操作系统内核开发，特权级切换，中断处理和虚拟内存管理
// 遵循RISC-V特权架构规范（Privileged ISA）

// 读取当前硬件线程ID（hart ID）
static inline uint64 r_mhartid()
{
    uint64 x;
    asm volatile("csrr %0, mhartid" : "=r"(x)); // 读取mhartid CSR
    return x;
}

// 机器模式状态寄存器 (mstatus) 相关定义
#define MSTATUS_MPP_MASK (3L << 11) // 前特权级模式掩码
#define MSTATUS_MPP_M (3L << 11)    // M模式（机器模式）
#define MSTATUS_MPP_S (1L << 11)    // S模式（监管者模式）
#define MSTATUS_MPP_U (0L << 11)    // U模式（用户模式）
#define MSTATUS_MIE (1L << 3)       // 机器模式中断使能

// 读取机器模式状态寄存器
static inline uint64 r_mstatus()
{
    uint64 x;
    asm volatile("csrr %0, mstatus" : "=r"(x));
    return x;
}

// 写入机器模式状态寄存器
static inline void w_mstatus(uint64 x)
{
    asm volatile("csrw mstatus, %0" : : "r"(x));
}

// 设置机器模式异常程序计数器（返回地址）
static inline void w_mepc(uint64 x)
{
    asm volatile("csrw mepc, %0" : : "r"(x));
}

// 监管者模式状态寄存器 (sstatus) 相关定义
#define SSTATUS_SPP (1L << 8)  // 前特权级模式：1=S模式，0=U模式
#define SSTATUS_SPIE (1L << 5) // 进入异常前S模式中断使能状态
#define SSTATUS_UPIE (1L << 4) // 进入异常前U模式中断使能状态
#define SSTATUS_SIE (1L << 1)  // S模式中断使能
#define SSTATUS_UIE (1L << 0)  // U模式中断使能

// 读取监管者模式状态寄存器
static inline uint64 r_sstatus()
{
    uint64 x;
    asm volatile("csrr %0, sstatus" : "=r"(x));
    return x;
}

// 写入监管者模式状态寄存器
static inline void w_sstatus(uint64 x)
{
    asm volatile("csrw sstatus, %0" : : "r"(x));
}

// 读取监管者模式中断等待寄存器
static inline uint64 r_sip()
{
    uint64 x;
    asm volatile("csrr %0, sip" : "=r"(x));
    return x;
}

// 写入监管者模式中断等待寄存器
static inline void w_sip(uint64 x)
{
    asm volatile("csrw sip, %0" : : "r"(x));
}

// 监管者模式中断使能寄存器位定义
#define SIE_SEIE (1L << 9) // 外部中断使能
#define SIE_STIE (1L << 5) // 定时器中断使能
#define SIE_SSIE (1L << 1) // 软件中断使能

// 读取监管者模式中断使能寄存器
static inline uint64 r_sie()
{
    uint64 x;
    asm volatile("csrr %0, sie" : "=r"(x));
    return x;
}

// 写入监管者模式中断使能寄存器
static inline void w_sie(uint64 x)
{
    asm volatile("csrw sie, %0" : : "r"(x));
}

// 机器模式中断使能寄存器位定义
#define MIE_MEIE (1L << 11) // 外部中断使能
#define MIE_MTIE (1L << 7)  // 定时器中断使能
#define MIE_MSIE (1L << 3)  // 软件中断使能

// 读取机器模式中断使能寄存器
static inline uint64 r_mie()
{
    uint64 x;
    asm volatile("csrr %0, mie" : "=r"(x));
    return x;
}

// 写入机器模式中断使能寄存器
static inline void w_mie(uint64 x)
{
    asm volatile("csrw mie, %0" : : "r"(x));
}

// 设置监管者模式异常程序计数器
static inline void w_sepc(uint64 x)
{
    asm volatile("csrw sepc, %0" : : "r"(x));
}

// 读取监管者模式异常程序计数器
static inline uint64 r_sepc()
{
    uint64 x;
    asm volatile("csrr %0, sepc" : "=r"(x));
    return x;
}

// 读取机器模式异常委托寄存器
static inline uint64 r_medeleg()
{
    uint64 x;
    asm volatile("csrr %0, medeleg" : "=r"(x));
    return x;
}

// 写入机器模式异常委托寄存器
static inline void w_medeleg(uint64 x)
{
    asm volatile("csrw medeleg, %0" : : "r"(x));
}

// 读取机器模式中断委托寄存器
static inline uint64 r_mideleg()
{
    uint64 x;
    asm volatile("csrr %0, mideleg" : "=r"(x));
    return x;
}

// 写入机器模式中断委托寄存器
static inline void w_mideleg(uint64 x)
{
    asm volatile("csrw mideleg, %0" : : "r"(x));
}

// 设置监管者模式异常向量基地址
static inline void w_stvec(uint64 x)
{
    asm volatile("csrw stvec, %0" : : "r"(x));
}

// 读取监管者模式异常向量基地址
static inline uint64 r_stvec()
{
    uint64 x;
    asm volatile("csrr %0, stvec" : "=r"(x));
    return x;
}

// 设置机器模式中断向量基地址
static inline void w_mtvec(uint64 x)
{
    asm volatile("csrw mtvec, %0" : : "r"(x));
}

// Sv39页表模式定义
#define SATP_SV39 (8L << 60) // SATP模式选择：Sv39

// 构造SATP寄存器值（设置模式+页表物理地址）
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// 设置监管者模式地址转换寄存器
static inline void w_satp(uint64 x)
{
    asm volatile("csrw satp, %0" : : "r"(x));
}

// 读取监管者模式地址转换寄存器
static inline uint64 r_satp()
{
    uint64 x;
    asm volatile("csrr %0, satp" : "=r"(x));
    return x;
}

// 设置监管者模式临时寄存器（用于trampoline中的早期异常处理）
static inline void w_sscratch(uint64 x)
{
    asm volatile("csrw sscratch, %0" : : "r"(x));
}

// 设置机器模式临时寄存器
static inline void w_mscratch(uint64 x)
{
    asm volatile("csrw mscratch, %0" : : "r"(x));
}

// 读取监管者模式异常原因寄存器
static inline uint64 r_scause()
{
    uint64 x;
    asm volatile("csrr %0, scause" : "=r"(x));
    return x;
}

// 读取监管者模式异常值寄存器
static inline uint64 r_stval()
{
    uint64 x;
    asm volatile("csrr %0, stval" : "=r"(x));
    return x;
}

// 写入机器模式计数器使能寄存器
static inline void w_mcounteren(uint64 x)
{
    asm volatile("csrw mcounteren, %0" : : "r"(x));
}

// 读取机器模式计数器使能寄存器
static inline uint64 r_mcounteren()
{
    uint64 x;
    asm volatile("csrr %0, mcounteren" : "=r"(x));
    return x;
}

// 读取机器模式时间计数器
static inline uint64 r_time()
{
    uint64 x;
    asm volatile("csrr %0, time" : "=r"(x));
    return x;
}

// 启用设备中断（设置SSTATUS_SIE位）
static inline void intr_on()
{
    w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// 禁用设备中断（清除SSTATUS_SIE位）
static inline void intr_off()
{
    w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// 检查设备中断是否启用
static inline int intr_get()
{
    uint64 x = r_sstatus();
    return (x & SSTATUS_SIE) != 0;
}

// 读取栈指针寄存器
static inline uint64 r_sp()
{
    uint64 x;
    asm volatile("mv %0, sp" : "=r"(x)); // 读取sp寄存器
    return x;
}

// 读取线程指针寄存器（通常存储核心ID）
static inline uint64 r_tp()
{
    uint64 x;
    asm volatile("mv %0, tp" : "=r"(x)); // 读取tp寄存器
    return x;
}

// 写入线程指针寄存器
static inline void w_tp(uint64 x)
{
    asm volatile("mv tp, %0" : : "r"(x));
}

// 读取返回地址寄存器
static inline uint64 r_ra()
{
    uint64 x;
    asm volatile("mv %0, ra" : "=r"(x)); // 读取ra寄存器
    return x;
}

// 刷新TLB（转换后备缓冲器）
static inline void sfence_vma()
{
    // 刷新所有TLB条目
    asm volatile("sfence.vma zero, zero");
}

// 页大小定义
#define PGSIZE 4096 // 页大小（字节）
#define PGSHIFT 12  // 页内偏移位数

// 地址对齐宏
#define PGROUNDUP(sz) (((sz) + PGSIZE - 1) & ~(PGSIZE - 1)) // 向上对齐到页边界
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))              // 向下对齐到页边界

// 页表项标志位定义
#define PTE_V (1L << 0) // 有效位
#define PTE_R (1L << 1) // 可读位
#define PTE_W (1L << 2) // 可写位
#define PTE_X (1L << 3) // 可执行位
#define PTE_U (1L << 4) // 用户可访问位

// 物理地址到页表项转换
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

// 页表项到物理地址转换
#define PTE2PA(pte) (((pte) >> 10) << 12)

// 提取页表项标志位
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// 虚拟地址分解宏
#define PXMASK 0x1FF                                                // 9位页索引掩码
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))                    // 各级页表偏移计算
#define PX(level, va) ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK) // 获取指定层级索引

// 最大虚拟地址（Sv39规范）
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1)) // 256GB地址空间

// 页表项类型定义
typedef uint64 pte_t;
// 页表类型（指向512个页表项的指针）
typedef uint64 *pagetable_t;