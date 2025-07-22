#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* Possible states of a thread: */
#define FREE 0x0
#define RUNNING 0x1
#define RUNNABLE 0x2

#define STACK_SIZE 8192
#define MAX_THREAD 4

struct context
{
    uint64 ra;
    uint64 sp;
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

struct thread
{
    char stack[STACK_SIZE]; /* the thread's stack */
    int state;              /* FREE, RUNNING, RUNNABLE */
    struct context context; // 线程上下文
};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;
extern void thread_switch(uint64, uint64);

void thread_init(void)
{
    // main() is thread 0, which will make the first invocation to
    // thread_schedule().  it needs a stack so that the first thread_switch() can
    // save thread 0's state.  thread_schedule() won't run the main thread ever
    // again, because its state is set to RUNNING, and thread_schedule() selects
    // a RUNNABLE thread.
    /*
    main()是线程0，它将第一次调用thread_schedule()。它需要一个堆栈，
    以便第一个thread_switch()可以保存线程0的状态。
    thread_schedule()不会再运行主线程，因为它的状态被设置为RUNNING，而thread_schedule()选择了一个可运行的线程。
    */
    current_thread = &all_thread[0];
    current_thread->state = RUNNING;
}

void thread_schedule(void)
{
    struct thread *t, *next_thread;

    /* Find another runnable thread. */
    next_thread = 0;
    t = current_thread + 1;
    // 循环遍历所有线程（最多MAX_THREAD次）
    for (int i = 0; i < MAX_THREAD; i++)
    {
        // 若超出数组末尾，回到数组开头（环形搜索）
        if (t >= all_thread + MAX_THREAD)
            t = all_thread;
        // 找到可运行线程，跳出循环
        if (t->state == RUNNABLE)
        {

            next_thread = t;
            break;
        }
        t = t + 1;
    }

    if (next_thread == 0)
    {
        printf("thread_schedule: no runnable threads\n");
        exit(-1);
    }

    // 如果当前线程与下一个线程不同，则进行切换
    if (current_thread != next_thread)
    {
        /* switch threads?  */
        next_thread->state = RUNNING; // 新线程设为运行状态

        t = current_thread;           // 保存当前线程指针
        current_thread = next_thread; // 更新当前线程指针
        /* YOUR CODE HERE
         * Invoke thread_switch to switch from t to next_thread:
         * thread_switch(??, ??);
         */
        /*
         * 执行上下文切换：
         * 将当前寄存器保存到t->context
         * 从next_thread->context恢复寄存器
         * 参数1：保存旧线程上下文的地址
         * 参数2：加载新线程上下文的地址
         */
        // 调用 thread_switch 进行上下文切换
        printf("thread_schedule:next_thread context = :%p\n", next_thread->context.ra);
        thread_switch((uint64)&t->context, (uint64)&next_thread->context);
        
    }
    else
        next_thread = 0;
}

void thread_create(void (*func)())
{
    struct thread *t;
    /* 在全局线程表中寻找空闲槽位 */
    for (t = all_thread; t < all_thread + MAX_THREAD; t++)
    {
        if (t->state == FREE)
            break; // 找到空闲线程块
    }
    t->state = RUNNABLE; // 设置新线程为就绪状态
    // YOUR CODE HERE
    // 设置线程的上下文
    memset(&t->context, 0, sizeof(t->context));
    t->context.ra = (uint64)func;                  // 返回地址设置为线程函数
    t->context.sp = (uint64)t->stack + STACK_SIZE; // 指向栈顶
}

/* 线程主动让出CPU */
void thread_yield(void)
{
    current_thread->state = RUNNABLE;
    thread_schedule();
}

volatile int a_started, b_started, c_started; // 线程启动标志
volatile int a_n, b_n, c_n;                   // 线程计数器

/* 测试线程A */
void thread_a(void)
{
    int i;
    printf("thread_a started\n");
    a_started = 1; // 标记线程A已启动

    // 等待其他线程启动（通过yield让出CPU）
    while (b_started == 0 || c_started == 0)
    {
        printf("thread_a will give up CPU\n");
        thread_yield();
        printf("thread a ret\n");
    }

    printf("start thread a ++\n");
    // 循环100次，每次打印并增加计数器
    for (i = 0; i < 100; i++)
    {
        printf("thread_a %d\n", i);
        a_n += 1;       // 增加私有计数器
        thread_yield(); // 主动让出CPU
    }
    printf("thread_a: exit after %d\n", a_n);

    current_thread->state = FREE; // 释放线程资源
    thread_schedule();            // 重新调度（永不返回）
}

void thread_b(void)
{
    int i;
    printf("thread_b started\n");
    b_started = 1;
    while (a_started == 0 || c_started == 0)
    {
        printf("thread_b will give up CPU\n");
        thread_yield();
    }

    for (i = 0; i < 100; i++)
    {
        printf("thread_b %d\n", i);
        b_n += 1;
        thread_yield();
    }
    printf("thread_b: exit after %d\n", b_n);

    current_thread->state = FREE;
    thread_schedule();
}

void thread_c(void)
{
    int i;
    printf("thread_c started\n");
    c_started = 1;
    while (a_started == 0 || b_started == 0)
    {
        printf("thread_c will give up CPU\n");
        thread_yield();
    }

    for (i = 0; i < 100; i++)
    {
        printf("thread_c %d\n", i);
        c_n += 1;
        printf("thread_c will give up CPU and call yiled\n");
        thread_yield();
    }
    printf("thread_c: exit after %d\n", c_n);

    current_thread->state = FREE;
    thread_schedule();
}

/* 主函数（线程0） */
int main(int argc, char *argv[])
{
    a_started = b_started = c_started = 0;
    a_n = b_n = c_n = 0;
    thread_init();
    printf("main:thread_create a will be exec\n");
    thread_create(thread_a);
    printf("main:thread_create b will be exec\n");
    thread_create(thread_b);
    printf("main:thread_create c will be exec\n");
    thread_create(thread_c);
    // 启动线程调度（永不返回）
    thread_schedule();
    // 理论上不会执行到这里
    exit(0);
}
