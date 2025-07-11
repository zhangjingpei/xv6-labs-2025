// #include "types.h"
// #include "param.h"
// #include "memlayout.h"
// #include "riscv.h"
// #include "spinlock.h"
// #include "proc.h"
// #include "defs.h"

// struct cpu cpus[NCPU];

// struct proc proc[NPROC];

// struct proc *initproc;

// int nextpid = 1;
// struct spinlock pid_lock;

// extern void forkret(void);
// static void wakeup1(struct proc *chan);
// static void freeproc(struct proc *p);

// extern char trampoline[]; // trampoline.S

// // initialize the proc table at boot time.
// void procinit(void)
// {
//     struct proc *p;

//     initlock(&pid_lock, "nextpid");
//     for (p = proc; p < &proc[NPROC]; p++)
//     {
//         initlock(&p->lock, "proc");

//         // Allocate a page for the process's kernel stack.
//         // Map it high in memory, followed by an invalid
//         // guard page.
//         char *pa = kalloc();
//         if (pa == 0)
//             panic("kalloc");
//         uint64 va = KSTACK((int)(p - proc));
//         kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
//         p->kstack = va;
//     }
//     kvminithart();
// }

// // Must be called with interrupts disabled,
// // to prevent race with process being moved
// // to a different CPU.
// int cpuid()
// {
//     int id = r_tp();
//     return id;
// }

// // Return this CPU's cpu struct.
// // Interrupts must be disabled.
// struct cpu *mycpu(void)
// {
//     int id = cpuid();
//     struct cpu *c = &cpus[id];
//     return c;
// }

// // Return the current struct proc *, or zero if none.
// struct proc *myproc(void)
// {
//     push_off();
//     struct cpu *c = mycpu();
//     struct proc *p = c->proc;
//     pop_off();
//     return p;
// }

// int allocpid()
// {
//     int pid;

//     acquire(&pid_lock);
//     pid = nextpid;
//     nextpid = nextpid + 1;
//     release(&pid_lock);

//     return pid;
// }

// // Look in the process table for an UNUSED proc.
// // If found, initialize state required to run in the kernel,
// // and return with p->lock held.
// // If there are no free procs, or a memory allocation fails, return 0.
// static struct proc *allocproc(void)
// {
//     struct proc *p;

//     for (p = proc; p < &proc[NPROC]; p++)
//     {
//         acquire(&p->lock);
//         if (p->state == UNUSED)
//         {
//             goto found;
//         }
//         else
//         {
//             release(&p->lock);
//         }
//     }
//     return 0;

// found:
//     p->pid = allocpid();

//     // Allocate a trapframe page.
//     if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
//     {
//         release(&p->lock);
//         return 0;
//     }

//     // An empty user page table.
//     p->pagetable = proc_pagetable(p);
//     if (p->pagetable == 0)
//     {
//         freeproc(p);
//         release(&p->lock);
//         return 0;
//     }

//     // Set up new context to start executing at forkret,
//     // which returns to user space.
//     memset(&p->context, 0, sizeof(p->context));
//     p->context.ra = (uint64)forkret;
//     p->context.sp = p->kstack + PGSIZE;

//     return p;
// }

// // free a proc structure and the data hanging from it,
// // including user pages.
// // p->lock must be held.
// static void freeproc(struct proc *p)
// {
//     if (p->trapframe)
//         kfree((void *)p->trapframe);
//     p->trapframe = 0;
//     if (p->pagetable)
//         proc_freepagetable(p->pagetable, p->sz);
//     p->pagetable = 0;
//     p->sz = 0;
//     p->pid = 0;
//     p->parent = 0;
//     p->name[0] = 0;
//     p->chan = 0;
//     p->killed = 0;
//     p->xstate = 0;
//     p->state = UNUSED;
// }

// // Create a user page table for a given process,
// // with no user memory, but with trampoline pages.
// pagetable_t proc_pagetable(struct proc *p)
// {
//     pagetable_t pagetable;

//     // An empty page table.
//     pagetable = uvmcreate();
//     if (pagetable == 0)
//         return 0;

//     // map the trampoline code (for system call return)
//     // at the highest user virtual address.
//     // only the supervisor uses it, on the way
//     // to/from user space, so not PTE_U.
//     if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0)
//     {
//         uvmfree(pagetable, 0);
//         return 0;
//     }

//     // map the trapframe just below TRAMPOLINE, for trampoline.S.
//     if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
//     {
//         uvmunmap(pagetable, TRAMPOLINE, 1, 0);
//         uvmfree(pagetable, 0);
//         return 0;
//     }

//     return pagetable;
// }

// // Free a process's page table, and free the
// // physical memory it refers to.
// void proc_freepagetable(pagetable_t pagetable, uint64 sz)
// {
//     uvmunmap(pagetable, TRAMPOLINE, 1, 0);
//     uvmunmap(pagetable, TRAPFRAME, 1, 0);
//     uvmfree(pagetable, sz);
// }

// // a user program that calls exec("/init")
// // od -t xC initcode
// uchar initcode[] = {0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02, 0x97, 0x05, 0x00, 0x00, 0x93,
//                     0x85, 0x35, 0x02, 0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00, 0x93, 0x08,
//                     0x20, 0x00, 0x73, 0x00, 0x00, 0x00, 0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e,
//                     0x69, 0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// // Set up first user process.
// void userinit(void)
// {
//     struct proc *p;

//     p = allocproc();
//     initproc = p;

//     // allocate one user page and copy init's instructions
//     // and data into it.
//     uvminit(p->pagetable, initcode, sizeof(initcode));
//     p->sz = PGSIZE;

//     // prepare for the very first "return" from kernel to user.
//     p->trapframe->epc = 0;     // user program counter
//     p->trapframe->sp = PGSIZE; // user stack pointer

//     safestrcpy(p->name, "initcode", sizeof(p->name));
//     p->cwd = namei("/");

//     p->state = RUNNABLE;

//     release(&p->lock);
// }

// // Grow or shrink user memory by n bytes.
// // Return 0 on success, -1 on failure.
// int growproc(int n)
// {
//     uint sz;
//     struct proc *p = myproc();

//     sz = p->sz;
//     if (n > 0)
//     {
//         if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
//         {
//             return -1;
//         }
//     }
//     else if (n < 0)
//     {
//         sz = uvmdealloc(p->pagetable, sz, sz + n);
//     }
//     p->sz = sz;
//     return 0;
// }

// // Create a new process, copying the parent.
// // Sets up child kernel stack to return as if from fork() system call.
// int fork(void)
// {
//     int i, pid;
//     struct proc *np;
//     struct proc *p = myproc();

//     // Allocate process.
//     if ((np = allocproc()) == 0)
//     {
//         return -1;
//     }

//     // Copy user memory from parent to child.
//     if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
//     {
//         freeproc(np);
//         release(&np->lock);
//         return -1;
//     }
//     np->sz = p->sz;

//     np->parent = p;

//     // copy saved user registers.
//     *(np->trapframe) = *(p->trapframe);

//     // Cause fork to return 0 in the child.
//     np->trapframe->a0 = 0;

//     // increment reference counts on open file descriptors.
//     for (i = 0; i < NOFILE; i++)
//         if (p->ofile[i])
//             np->ofile[i] = filedup(p->ofile[i]);
//     np->cwd = idup(p->cwd);

//     safestrcpy(np->name, p->name, sizeof(p->name));

//     pid = np->pid;

//     np->state = RUNNABLE;

//     release(&np->lock);

//     return pid;
// }

// // Pass p's abandoned children to init.
// // Caller must hold p->lock.
// void reparent(struct proc *p)
// {
//     struct proc *pp;

//     for (pp = proc; pp < &proc[NPROC]; pp++)
//     {
//         // this code uses pp->parent without holding pp->lock.
//         // acquiring the lock first could cause a deadlock
//         // if pp or a child of pp were also in exit()
//         // and about to try to lock p.
//         if (pp->parent == p)
//         {
//             // pp->parent can't change between the check and the acquire()
//             // because only the parent changes it, and we're the parent.
//             acquire(&pp->lock);
//             pp->parent = initproc;
//             // we should wake up init here, but that would require
//             // initproc->lock, which would be a deadlock, since we hold
//             // the lock on one of init's children (pp). this is why
//             // exit() always wakes init (before acquiring any locks).
//             release(&pp->lock);
//         }
//     }
// }

// // Exit the current process.  Does not return.
// // An exited process remains in the zombie state
// // until its parent calls wait().
// void exit(int status)
// {
//     struct proc *p = myproc();

//     if (p == initproc)
//         panic("init exiting");

//     // Close all open files.
//     for (int fd = 0; fd < NOFILE; fd++)
//     {
//         if (p->ofile[fd])
//         {
//             struct file *f = p->ofile[fd];
//             fileclose(f);
//             p->ofile[fd] = 0;
//         }
//     }

//     begin_op();
//     iput(p->cwd);
//     end_op();
//     p->cwd = 0;

//     // we might re-parent a child to init. we can't be precise about
//     // waking up init, since we can't acquire its lock once we've
//     // acquired any other proc lock. so wake up init whether that's
//     // necessary or not. init may miss this wakeup, but that seems
//     // harmless.
//     acquire(&initproc->lock);
//     wakeup1(initproc);
//     release(&initproc->lock);

//     // grab a copy of p->parent, to ensure that we unlock the same
//     // parent we locked. in case our parent gives us away to init while
//     // we're waiting for the parent lock. we may then race with an
//     // exiting parent, but the result will be a harmless spurious wakeup
//     // to a dead or wrong process; proc structs are never re-allocated
//     // as anything else.
//     acquire(&p->lock);
//     struct proc *original_parent = p->parent;
//     release(&p->lock);

//     // we need the parent's lock in order to wake it up from wait().
//     // the parent-then-child rule says we have to lock it first.
//     acquire(&original_parent->lock);

//     acquire(&p->lock);

//     // Give any children to init.
//     reparent(p);

//     // Parent might be sleeping in wait().
//     wakeup1(original_parent);

//     p->xstate = status;
//     p->state = ZOMBIE;

//     release(&original_parent->lock);

//     // Jump into the scheduler, never to return.
//     sched();
//     panic("zombie exit");
// }

// // Wait for a child process to exit and return its pid.
// // Return -1 if this process has no children.
// int wait(uint64 addr)
// {
//     struct proc *np;
//     int havekids, pid;
//     struct proc *p = myproc();

//     // hold p->lock for the whole time to avoid lost
//     // wakeups from a child's exit().
//     acquire(&p->lock);

//     for (;;)
//     {
//         // Scan through table looking for exited children.
//         havekids = 0;
//         for (np = proc; np < &proc[NPROC]; np++)
//         {
//             // this code uses np->parent without holding np->lock.
//             // acquiring the lock first would cause a deadlock,
//             // since np might be an ancestor, and we already hold p->lock.
//             if (np->parent == p)
//             {
//                 // np->parent can't change between the check and the acquire()
//                 // because only the parent changes it, and we're the parent.
//                 acquire(&np->lock);
//                 havekids = 1;
//                 if (np->state == ZOMBIE)
//                 {
//                     // Found one.
//                     pid = np->pid;
//                     if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate, sizeof(np->xstate)) < 0)
//                     {
//                         release(&np->lock);
//                         release(&p->lock);
//                         return -1;
//                     }
//                     freeproc(np);
//                     release(&np->lock);
//                     release(&p->lock);
//                     return pid;
//                 }
//                 release(&np->lock);
//             }
//         }

//         // No point waiting if we don't have any children.
//         if (!havekids || p->killed)
//         {
//             release(&p->lock);
//             return -1;
//         }

//         // Wait for a child to exit.
//         sleep(p, &p->lock); // DOC: wait-sleep
//     }
// }

// // Per-CPU process scheduler.
// // Each CPU calls scheduler() after setting itself up.
// // Scheduler never returns.  It loops, doing:
// //  - choose a process to run.
// //  - swtch to start running that process.
// //  - eventually that process transfers control
// //    via swtch back to the scheduler.
// void scheduler(void)
// {
//     struct proc *p;
//     struct cpu *c = mycpu();

//     c->proc = 0;
//     for (;;)
//     {
//         // Avoid deadlock by ensuring that devices can interrupt.
//         intr_on();

//         int found = 0;
//         for (p = proc; p < &proc[NPROC]; p++)
//         {
//             acquire(&p->lock);
//             if (p->state == RUNNABLE)
//             {
//                 // Switch to chosen process.  It is the process's job
//                 // to release its lock and then reacquire it
//                 // before jumping back to us.
//                 p->state = RUNNING;
//                 c->proc = p;
//                 swtch(&c->context, &p->context);

//                 // Process is done running for now.
//                 // It should have changed its p->state before coming back.
//                 c->proc = 0;

//                 found = 1;
//             }
//             release(&p->lock);
//         }
//         if (found == 0)
//         {
//             intr_on();
//             asm volatile("wfi");
//         }
//     }
// }

// // Switch to scheduler.  Must hold only p->lock
// // and have changed proc->state. Saves and restores
// // intena because intena is a property of this
// // kernel thread, not this CPU. It should
// // be proc->intena and proc->noff, but that would
// // break in the few places where a lock is held but
// // there's no process.
// void sched(void)
// {
//     int intena;
//     struct proc *p = myproc();

//     if (!holding(&p->lock))
//         panic("sched p->lock");
//     if (mycpu()->noff != 1)
//         panic("sched locks");
//     if (p->state == RUNNING)
//         panic("sched running");
//     if (intr_get())
//         panic("sched interruptible");

//     intena = mycpu()->intena;
//     swtch(&p->context, &mycpu()->context);
//     mycpu()->intena = intena;
// }

// // Give up the CPU for one scheduling round.
// void yield(void)
// {
//     struct proc *p = myproc();
//     acquire(&p->lock);
//     p->state = RUNNABLE;
//     sched();
//     release(&p->lock);
// }

// // A fork child's very first scheduling by scheduler()
// // will swtch to forkret.
// void forkret(void)
// {
//     static int first = 1;

//     // Still holding p->lock from scheduler.
//     release(&myproc()->lock);

//     if (first)
//     {
//         // File system initialization must be run in the context of a
//         // regular process (e.g., because it calls sleep), and thus cannot
//         // be run from main().
//         first = 0;
//         fsinit(ROOTDEV);
//     }

//     usertrapret();
// }

// // Atomically release lock and sleep on chan.
// // Reacquires lock when awakened.
// void sleep(void *chan, struct spinlock *lk)
// {
//     struct proc *p = myproc();

//     // Must acquire p->lock in order to
//     // change p->state and then call sched.
//     // Once we hold p->lock, we can be
//     // guaranteed that we won't miss any wakeup
//     // (wakeup locks p->lock),
//     // so it's okay to release lk.
//     if (lk != &p->lock)
//     {                      // DOC: sleeplock0
//         acquire(&p->lock); // DOC: sleeplock1
//         release(lk);
//     }

//     // Go to sleep.
//     p->chan = chan;
//     p->state = SLEEPING;

//     sched();

//     // Tidy up.
//     p->chan = 0;

//     // Reacquire original lock.
//     if (lk != &p->lock)
//     {
//         release(&p->lock);
//         acquire(lk);
//     }
// }

// // Wake up all processes sleeping on chan.
// // Must be called without any p->lock.
// void wakeup(void *chan)
// {
//     struct proc *p;

//     for (p = proc; p < &proc[NPROC]; p++)
//     {
//         acquire(&p->lock);
//         if (p->state == SLEEPING && p->chan == chan)
//         {
//             p->state = RUNNABLE;
//         }
//         release(&p->lock);
//     }
// }

// // Wake up p if it is sleeping in wait(); used by exit().
// // Caller must hold p->lock.
// static void wakeup1(struct proc *p)
// {
//     if (!holding(&p->lock))
//         panic("wakeup1");
//     if (p->chan == p && p->state == SLEEPING)
//     {
//         p->state = RUNNABLE;
//     }
// }

// // Kill the process with the given pid.
// // The victim won't exit until it tries to return
// // to user space (see usertrap() in trap.c).
// int kill(int pid)
// {
//     struct proc *p;

//     for (p = proc; p < &proc[NPROC]; p++)
//     {
//         acquire(&p->lock);
//         if (p->pid == pid)
//         {
//             p->killed = 1;
//             if (p->state == SLEEPING)
//             {
//                 // Wake process from sleep().
//                 p->state = RUNNABLE;
//             }   
//             release(&p->lock);
//             return 0;
//         }
//         release(&p->lock);
//     }
//     return -1;
// }

// // Copy to either a user address, or kernel address,
// // depending on usr_dst.
// // Returns 0 on success, -1 on error.
// int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
// {
//     struct proc *p = myproc();
//     if (user_dst)
//     {
//         return copyout(p->pagetable, dst, src, len);
//     }
//     else
//     {
//         memmove((char *)dst, src, len);
//         return 0;
//     }
// }

// // Copy from either a user address, or kernel address,
// // depending on usr_src.
// // Returns 0 on success, -1 on error.
// int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
// {
//     struct proc *p = myproc();
//     if (user_src)
//     {
//         return copyin(p->pagetable, dst, src, len);
//     }
//     else
//     {
//         memmove(dst, (char *)src, len);
//         return 0;
//     }
// }

// // Print a process listing to console.  For debugging.
// // Runs when user types ^P on console.
// // No lock to avoid wedging a stuck machine further.
// void procdump(void)
// {
//     static char *states[] = {
//         [UNUSED] "unused", [SLEEPING] "sleep ", [RUNNABLE] "runble", [RUNNING] "run   ", [ZOMBIE] "zombie"};
//     struct proc *p;
//     char *state;

//     printf("\n");
//     for (p = proc; p < &proc[NPROC]; p++)
//     {
//         if (p->state == UNUSED)
//             continue;
//         if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
//             state = states[p->state];
//         else
//             state = "???";
//         printf("%d %s %s", p->pid, state, p->name);
//         printf("\n");
//     }
// }

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 全局数据结构定义
struct cpu cpus[NCPU];          // CPU 结构数组
struct proc proc[NPROC];        // 进程表（进程控制块数组）
struct proc *initproc;          // 指向第一个用户进程的指针

int nextpid = 1;                // 下一个可用的进程ID
struct spinlock pid_lock;       // 保护nextpid的自旋锁

// 外部函数声明
extern void forkret(void);      // 首次切换到用户空间的函数
static void wakeup1(struct proc *chan);  // 内部唤醒函数
static void freeproc(struct proc *p);    // 释放进程资源

extern char trampoline[];       // 来自trampoline.S的跳板代码

// 初始化进程表（在系统启动时调用）
void procinit(void)
{
    struct proc *p;

    initlock(&pid_lock, "nextpid");  // 初始化进程ID锁
    // 遍历所有进程槽
    for (p = proc; p < &proc[NPROC]; p++)
    {
        initlock(&p->lock, "proc");  // 初始化每个进程的自旋锁

        // 为进程的内核栈分配物理页
        char *pa = kalloc();
        if (pa == 0)
            panic("kalloc");
        // 计算内核栈的虚拟地址（每个进程有独立的内核栈区域）
        uint64 va = KSTACK((int)(p - proc));
        // 映射虚拟地址到物理页（可读写，无用户权限）
        kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
        p->kstack = va;  // 保存内核栈虚拟地址
    }
    kvminithart();  // 刷新TLB
}

// 获取当前CPU ID（必须在中断禁用下调用）
int cpuid()
{
    int id = r_tp();  // 读取线程指针寄存器
    return id;
}

// 获取当前CPU的结构体指针（必须在中断禁用下调用）
struct cpu *mycpu(void)
{
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// 获取当前运行的进程指针（若没有则返回0）
struct proc *myproc(void)
{
    push_off();            // 禁用中断
    struct cpu *c = mycpu();
    struct proc *p = c->proc;  // 获取当前CPU上运行的进程
    pop_off();             // 恢复中断状态
    return p;
}

// 分配唯一的进程ID
int allocpid()
{
    int pid;

    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;  // 递增下一个可用PID
    release(&pid_lock);

    return pid;
}

// 在进程表中分配一个UNUSED状态的进程槽
// 成功时返回带锁的proc指针，失败返回0
static struct proc *allocproc(void)
{
    struct proc *p;

    // 扫描进程表
    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);  // 获取进程锁
        if (p->state == UNUSED)
            goto found;     // 找到空闲槽
        else
            release(&p->lock);
    }
    return 0;  // 无可用进程槽

found:
    p->pid = allocpid();  // 分配PID

    // 分配陷阱帧页面（用于保存用户寄存器）
    if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
    {
        release(&p->lock);
        return 0;
    }

    // 创建初始页表（包含跳板代码和陷阱帧映射）
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0)
    {
        freeproc(p);      // 分配失败则清理
        release(&p->lock);
        return 0;
    }

    // 设置上下文：返回到forkret（该函数最终进入用户空间）
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;   // 返回地址
    p->context.sp = p->kstack + PGSIZE; // 内核栈指针

    return p;
}

// 释放进程资源（必须在持有p->lock时调用）
static void freeproc(struct proc *p)
{
    if (p->trapframe)
        kfree((void *)p->trapframe);  // 释放陷阱帧
    p->trapframe = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);  // 释放页表
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;  // 清除进程名
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;  // 标记为未使用
}

// 创建进程页表（映射跳板代码和陷阱帧）
pagetable_t proc_pagetable(struct proc *p)
{
    pagetable_t pagetable;

    // 创建空页表
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // 映射跳板代码（位于用户地址空间顶部）
    // 权限：可读可执行（非用户可访问）
    if (mappages(pagetable, TRAMPOLINE, PGSIZE, 
                 (uint64)trampoline, PTE_R | PTE_X) < 0)
    {
        uvmfree(pagetable, 0);
        return 0;
    }

    // 映射陷阱帧（位于跳板代码下方）
    if (mappages(pagetable, TRAPFRAME, PGSIZE,
                 (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
    {
        // 出错时解除跳板映射并释放页表
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// 释放进程页表和相关物理内存
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
    // 解除跳板和陷阱帧的映射
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    // 释放用户内存和页表本身
    uvmfree(pagetable, sz);
}

// 初始化程序的二进制代码（/init）
// 格式：十六进制表示的RISC-V机器码
uchar initcode[] = {0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02, 0x97, 0x05, 0x00, 0x00, 0x93,
                    0x85, 0x35, 0x02, 0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00, 0x93, 0x08,
                    0x20, 0x00, 0x73, 0x00, 0x00, 0x00, 0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e,
                    0x69, 0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// 创建第一个用户进程
void userinit(void)
{
    struct proc *p;

    p = allocproc();  // 分配进程
    initproc = p;     // 设置为初始进程

    // 将初始化代码加载到用户内存
    uvminit(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;  // 设置进程内存大小

    // 设置陷阱帧：用户程序入口和栈指针
    p->trapframe->epc = 0;      // 用户程序计数器（从0开始）
    p->trapframe->sp = PGSIZE;  // 用户栈指针（位于页面顶部）

    safestrcpy(p->name, "initcode", sizeof(p->name)); // 设置进程名
    p->cwd = namei("/");         // 设置当前工作目录

    p->state = RUNNABLE;         // 设置为可运行状态

    release(&p->lock);
}

// 调整进程内存大小（n字节）
// 成功返回0，失败返回-1
int growproc(int n)
{
    uint sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0)  // 扩展内存
    {
        if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
            return -1;
    }
    else if (n < 0)  // 收缩内存
    {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;  // 更新进程大小
    return 0;
}

// 创建子进程（复制父进程）
int fork(void)
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();  // 获取当前进程

    // 分配子进程结构体
    if ((np = allocproc()) == 0)
        return -1;

    // 复制用户内存（页表内容）
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
    {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;  // 继承内存大小

    np->parent = p;  // 设置父进程

    // 复制陷阱帧（寄存器状态）
    *(np->trapframe) = *(p->trapframe);

    // 子进程fork返回0（设置a0寄存器）
    np->trapframe->a0 = 0;

    // 复制打开的文件描述符（增加引用计数）
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);  // 复制当前工作目录

    safestrcpy(np->name, p->name, sizeof(p->name)); // 复制进程名

    pid = np->pid;

    np->state = RUNNABLE;  // 设置为可运行

    release(&np->lock);

    return pid;  // 父进程返回子进程PID
}

// 将进程p的孤儿进程重新分配给init进程
// 调用者必须持有p->lock
void reparent(struct proc *p) //p是父进程
{
    struct proc *pp;

    // 遍历所有进程
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
        // 注意：这里在未持有pp->lock的情况下访问pp->parent
        if (pp->parent == p)  // 找到子进程
        {
            acquire(&pp->lock);
            pp->parent = initproc;  // 重新设置父进程为init
            release(&pp->lock);
        }
    }
}

// 退出当前进程（不返回）
// 进程变为僵尸状态直到父进程wait()
void exit(int status)
{
    struct proc *p = myproc();

    if (p == initproc)
        panic("init exiting");  // init进程退出会导致系统恐慌

    // 关闭所有打开的文件
    for (int fd = 0; fd < NOFILE; fd++)
        if (p->ofile[fd])
        {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }

    // 文件系统操作：释放当前目录
    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    // 唤醒init进程（可能重新设置父进程）
    acquire(&initproc->lock);
    wakeup1(initproc);
    release(&initproc->lock);

    // 保存原始父进程指针（避免竞争）
    acquire(&p->lock);
    struct proc *original_parent = p->parent;
    release(&p->lock);

    // 获取父进程锁（遵循父进程先于子进程加锁的规则）
    acquire(&original_parent->lock);
    acquire(&p->lock);

    // 将子进程重新分配给init
    reparent(p);

    // 唤醒可能在wait()中睡眠的父进程
    wakeup1(original_parent);

    // 设置退出状态并转为僵尸状态
    p->xstate = status;
    p->state = ZOMBIE;

    release(&original_parent->lock);

    // 永久切换到调度器
    sched();
    panic("zombie exit");  // 不应执行到这里
}

// 等待任意子进程退出
// 返回子进程PID，无子进程返回-1
int wait(uint64 addr)
{
    struct proc *np;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&p->lock);  // 持有父进程锁

    for (;;)
    {
        havekids = 0;
        // 扫描查找退出的子进程
        for (np = proc; np < &proc[NPROC]; np++)
        {
            // 注意：这里在未持有np->lock的情况下访问np->parent
            if (np->parent == p)
            {
                acquire(&np->lock);
                havekids = 1;
                if (np->state == ZOMBIE)  // 找到僵尸进程
                {
                    pid = np->pid;
                    // 将退出状态复制到用户空间（如果addr非0）
                    if (addr != 0 && copyout(p->pagetable, addr, 
                         (char *)&np->xstate, sizeof(np->xstate)) < 0)
                    {
                        release(&np->lock);
                        release(&p->lock);
                        return -1;
                    }
                    freeproc(np);  // 释放子进程资源
                    release(&np->lock);
                    release(&p->lock);
                    return pid;  // 返回子进程PID
                }
                release(&np->lock);
            }
        }

        // 无子进程或进程被杀死
        if (!havekids || p->killed)
        {
            release(&p->lock);
            return -1;
        }

        // 等待子进程退出（原子释放锁并睡眠）
        sleep(p, &p->lock);
    }
}

// CPU调度器（永不返回）
void scheduler(void)
{
    struct proc *p;
    struct cpu *c = mycpu();

    c->proc = 0;  // 初始时无运行进程
    for (;;)
    {
        intr_on();  // 允许中断

        int found = 0;
        // 查找可运行进程
        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                found = 1;
                p->state = RUNNING;  // 标记为运行中
                c->proc = p;        // 设置当前CPU运行的进程
                // 切换到目标进程（上下文切换）
                swtch(&c->context, &p->context);

                // 返回调度器时（进程让出CPU）
                c->proc = 0;  // 清除当前进程
            }
            release(&p->lock);
        }
        // 无进程可运行时执行WFI指令节能
        if (found == 0)
        {
            intr_on();
            asm volatile("wfi");
        }
    }
}

// 切换到调度器（必须持有p->lock）
void sched(void)
{
    int intena;
    struct proc *p = myproc();

    // 安全检查
    if (!holding(&p->lock)) panic("sched p->lock");
    if (mycpu()->noff != 1) panic("sched locks");
    if (p->state == RUNNING) panic("sched running");
    if (intr_get()) panic("sched interruptible");

    intena = mycpu()->intena;  // 保存中断使能状态
    swtch(&p->context, &mycpu()->context);  // 上下文切换
    mycpu()->intena = intena;  // 恢复中断使能状态
}

// 主动让出CPU
void yield(void)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;  // 标记为可运行
    sched();              // 切换到调度器
    release(&p->lock);
}

// fork返回路径（首次进入用户空间）
void forkret(void)
{
    static int first = 1;

    release(&myproc()->lock);  // 释放进程锁

    if (first)
    {
        first = 0;
        fsinit(ROOTDEV);  // 首次运行时初始化文件系统
    }

    usertrapret();  // 返回到用户空间
}

// 在指定通道上睡眠（原子释放锁并睡眠）
void sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    // 确保持有进程锁
    if (lk != &p->lock)
    {
        acquire(&p->lock);
        release(lk);  // 释放传入的锁
    }

    // 设置睡眠状态
    p->chan = chan;
    p->state = SLEEPING;

    sched();  // 切换到调度器

    // 唤醒后清理
    p->chan = 0;

    // 重新获取原始锁
    if (lk != &p->lock)
    {
        release(&p->lock);
        acquire(lk);
    }
}

// 唤醒在指定通道上睡眠的所有进程
void wakeup(void *chan)
{
    struct proc *p;

    // 遍历所有进程
    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        // 匹配睡眠状态和通道
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;  // 设置为可运行
        release(&p->lock);
    }
}

// 内部唤醒函数（用于exit中唤醒父进程）
static void wakeup1(struct proc *p)
{
    if (!holding(&p->lock)) panic("wakeup1");
    // 特殊唤醒条件：进程在自己的锁上睡眠
    if (p->chan == p && p->state == SLEEPING)
        p->state = RUNNABLE;
}

// 终止指定PID的进程
int kill(int pid)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->pid == pid)
        {
            p->killed = 1;  // 设置终止标志
            // 若进程在睡眠则立即唤醒
            if (p->state == SLEEPING)
                p->state = RUNNABLE;
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;  // 未找到进程
}

// 根据目标类型（内核/用户）复制数据
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
    if (user_dst)
        return copyout(myproc()->pagetable, dst, src, len);
    else
    {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// 根据源类型（内核/用户）复制数据
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
    if (user_src)
        return copyin(myproc()->pagetable, dst, src, len);
    else
    {
        memmove(dst, (char *)src, len);
        return 0;
    }
}

// 调试用：打印所有进程状态
void procdump(void)
{
    static char *states[] = {
        [UNUSED]    "unused",   // 未使用
        [SLEEPING]  "sleep ",   // 睡眠中
        [RUNNABLE]  "runble",   // 可运行
        [RUNNING]   "run   ",   // 运行中
        [ZOMBIE]    "zombie"    // 僵尸
    };
    struct proc *p;
    char *state;

    printf("\n");
    for (p = proc; p < &proc[NPROC]; p++)
    {
        if (p->state == UNUSED) continue;
        // 获取状态字符串
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d %s %s", p->pid, state, p->name);
        printf("\n");
    }
}