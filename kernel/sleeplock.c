// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

// 初始化睡眠锁
void initsleeplock(struct sleeplock *lk, char *name)
{
    initlock(&lk->lk, "sleep lock"); // 初始化内部自旋锁
    lk->name = name;                 // 设置锁名称
    lk->locked = 0;                  // 初始状态为未锁定
    lk->pid = 0;                     // 初始化持有进程ID为0
}

// 获取睡眠锁
void acquiresleep(struct sleeplock *lk)
{
    acquire(&lk->lk);  // 获取内部自旋锁
    while (lk->locked) // 循环等待直到锁可用
    {
        sleep(lk, &lk->lk); // 释放自旋锁并进入睡眠，被唤醒后重新获取自旋锁
    }
    lk->locked = 1;          // 标记锁为已持有
    lk->pid = myproc()->pid; // 记录当前进程ID
    release(&lk->lk);        // 释放内部自旋锁
}

void releasesleep(struct sleeplock *lk)
{
    acquire(&lk->lk); // 获取内部自旋锁
    lk->locked = 0;   // 标记锁为未持有
    lk->pid = 0;      // 清空持有进程ID
    wakeup(lk);       // 唤醒所有等待此锁的进程
    release(&lk->lk); // 释放内部自旋锁
}

int holdingsleep(struct sleeplock *lk)
{
    int r;

    acquire(&lk->lk);                             // 获取内部自旋锁
    r = lk->locked && (lk->pid == myproc()->pid); // 验证持有状态和进程ID
    release(&lk->lk);                             // 释放内部自旋锁
    return r;
}
