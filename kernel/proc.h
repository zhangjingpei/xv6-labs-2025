// 保存内核上下文切换所需的寄存器
struct context
{
    uint64 ra; // 返回地址寄存器 (return address)
    uint64 sp; // 栈指针寄存器 (stack pointer)

    // 被调用者保存寄存器 (callee-saved registers)
    // 这些寄存器在函数调用中必须由被调用者保存和恢复
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

// 每个CPU的状态
struct cpu
{
    struct proc *proc;      // 当前CPU上运行的进程，空表示空闲
    struct context context; // 切换到此上下文以进入调度器
    int noff;               // push_off() 嵌套深度（中断禁用计数）
    int intena;             // 在 push_off() 前是否启用了中断
};

extern struct cpu cpus[NCPU]; // 系统所有CPU的数组

// 用于trampoline.S中陷阱处理代码的进程级数据结构
// 位于用户页表中专用页（紧邻蹦床页下方），在内核页表中无特殊映射
// sscratch寄存器指向此结构
// trampoline.S中的uservec将用户寄存器保存到trapframe中
// 然后从trapframe加载内核栈指针、CPU ID、内核页表等寄存器
// 并跳转到kernel_trap
// usertrapret()和trampoline.S中的userret会设置
// trapframe中的内核信息，恢复用户寄存器
// 切换回用户页表，进入用户空间
// trapframe包含s0-s11等被调用者保存寄存器，因为
// 通过usertrapret()返回用户空间的路径不经过完整的内核调用栈
struct trapframe
{
    /*   0 */ uint64 kernel_satp;   // 内核页表地址
    /*   8 */ uint64 kernel_sp;     // 进程内核栈顶地址
    /*  16 */ uint64 kernel_trap;   // usertrap()函数地址
    /*  24 */ uint64 epc;           // 保存的用户程序计数器
    /*  32 */ uint64 kernel_hartid; // 保存的内核线程指针 (CPU ID)

    // 以下保存用户空间寄存器状态
    /*  40 */ uint64 ra; // 返回地址
    /*  48 */ uint64 sp; // 栈指针
    /*  56 */ uint64 gp; // 全局指针
    /*  64 */ uint64 tp; // 线程指针
    /*  72 */ uint64 t0; // 临时寄存器
    /*  80 */ uint64 t1;
    /*  88 */ uint64 t2;
    /*  96 */ uint64 s0; // 保存寄存器
    /* 104 */ uint64 s1;
    /* 112 */ uint64 a0; // 函数参数/返回值
    /* 120 */ uint64 a1;
    /* 128 */ uint64 a2;
    /* 136 */ uint64 a3;
    /* 144 */ uint64 a4;
    /* 152 */ uint64 a5;
    /* 160 */ uint64 a6;
    /* 168 */ uint64 a7;
    /* 176 */ uint64 s2;
    /* 184 */ uint64 s3;
    /* 192 */ uint64 s4;
    /* 200 */ uint64 s5;
    /* 208 */ uint64 s6;
    /* 216 */ uint64 s7;
    /* 224 */ uint64 s8;
    /* 232 */ uint64 s9;
    /* 240 */ uint64 s10;
    /* 248 */ uint64 s11;
    /* 256 */ uint64 t3; // 临时寄存器
    /* 264 */ uint64 t4;
    /* 272 */ uint64 t5;
    /* 280 */ uint64 t6;
};

// 进程状态枚举
enum procstate
{
    UNUSED,   // 未使用
    SLEEPING, // 睡眠中（等待事件）
    RUNNABLE, // 可运行（等待CPU）
    RUNNING,  // 正在运行
    ZOMBIE    // 僵尸状态（已终止但未回收资源）
};

// 进程控制块 (PCB)
struct proc
{
    struct spinlock lock; // 进程锁（保护以下字段）

    // 使用以下字段时必须持有 p->lock:
    enum procstate state; // 进程状态
    struct proc *parent;  // 父进程指针
    void *chan;           // 非零表示进程在此地址上睡眠
    int killed;           // 非零表示进程已被杀死
    int xstate;           // 退出状态（返回给父进程wait()）
    int pid;              // 进程ID

    // 以下字段为进程私有，访问时不需持有 p->lock:
    uint64 kstack;               // 内核栈虚拟地址
    uint64 sz;                   // 进程内存大小（字节）
    pagetable_t pagetable;       // 用户页表指针
    struct trapframe *trapframe; // 陷阱帧指针（用户-内核切换时使用）
    struct context context;      // 进程上下文（切换时保存寄存器）
    struct file *ofile[NOFILE];  // 打开的文件数组
    struct inode *cwd;           // 当前工作目录
    char name[16];               // 进程名（调试用）
};