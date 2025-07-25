// 长期睡眠锁结构体
struct sleeplock
{
    uint locked;        // 锁是否被持有：0-未持有，1-持有
    struct spinlock lk; // 保护此睡眠锁的自旋锁

    // 调试信息：
    char *name; // 锁的名称
    int pid;    // 持有锁的进程ID
};