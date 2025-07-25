/*
 * 文件：log.c
 * 功能：实现一个简单的日志系统，用于确保文件系统操作的原子性和崩溃一致性。
 * 核心机制：
 *   - 将多个文件系统操作组合成事务
 *   - 先写日志再写实际位置（Write-Ahead Logging）
 *   - 只有日志完全落盘后才应用更改
 *   - 支持崩溃后日志恢复
 */

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

/*
 * 支持并发文件系统调用的简单日志系统。
 *
 * 日志事务包含多个文件系统调用的更新。
 * 只有在没有活动文件系统调用时日志系统才会提交。
 * 因此无需考虑提交操作是否会将未提交的系统调用更新写入磁盘。
 *
 * 系统调用应使用 begin_op()/end_op() 标记其开始和结束。
 * 通常 begin_op() 仅增加进行中的文件系统调用计数。
 * 但如果日志空间不足，它会休眠直到最后一次 end_op() 提交。
 *
 * 日志是包含磁盘块的物理重做日志。
 * 磁盘日志格式：
 *   头块：包含块A、B、C...的块号
 *   块A
 *   块B
 *   块C
 *   ...
 * 日志追加操作是同步的。
 */

/*
 * 日志头结构（磁盘+内存）
 * 存储在日志区域的第一个块，记录事务包含的磁盘块号
 */
struct logheader
{
    int n;              // 日志中有效块的数量
    int block[LOGSIZE]; // 实际数据块的块号数组 LOGSIZE=30
};

// 日志控制结构
struct log
{
    struct spinlock lock; // 保护日志结构的自旋锁
    int start;            // 日志在磁盘上的起始块号 2
    int size;             // 日志总块数 30
    int outstanding;      // 当前正在执行的系统调用数
    int committing;       // 提交标志位（1表示正在提交）
    int dev;              // 关联的设备号
    struct logheader lh;  // 内存中的日志头
};

// 全局日志实例
struct log log;

// 前向声明
static void recover_from_log(void); // 从日志恢复
static void commit();               // 提交事务

// 初始化日志系统
void initlog(int dev, struct superblock *sb)
{
    // 检查日志头大小是否合法（必须小于磁盘块大小）
    if (sizeof(struct logheader) >= BSIZE)
        panic("initlog: too big logheader");

    initlock(&log.lock, "log"); // 初始化日志锁
    log.start = sb->logstart;   // 从超级块获取日志起始块
    log.size = sb->nlog;        // 从超级块获取日志总块数
    log.dev = dev;              // 记录关联设备号
    recover_from_log();         // 尝试从日志恢复
}

// 将已提交的块从日志拷贝到实际位置  日志区的数据->内存dbuf->写入磁盘数据区block
static void install_trans(int recovering)
{
    int tail; // 日志块索引

    // 遍历日志中的所有有效块
    for (tail = 0; tail < log.lh.n; tail++)
    {
        // 读取日志块（跳过头块：start+0是头，start+1开始是数据）
        struct buf *lbuf = bread(log.dev, log.start + tail + 1);
        // 读取目标数据块（从日志头获取实际块号）
        struct buf *dbuf = bread(log.dev, log.lh.block[tail]);
        // 复制整个块->目标数据块dbuf
        memmove(dbuf->data, lbuf->data, BSIZE);
        // 将块实际写入磁盘
        bwrite(dbuf);
        // 如果不是恢复模式，减少目标块的引用计数
        if (recovering == 0)
            bunpin(dbuf);
        // 释放缓冲区
        brelse(lbuf);
        brelse(dbuf);
    }
}

// 从磁盘读取日志头到内存
static void read_head(void)
{
    struct buf *buf = bread(log.dev, log.start);            // 读取日志区头块
    struct logheader *lh = (struct logheader *)(buf->data); // 读取头块中的数据

    int i;
    log.lh.n = lh->n; // 复制块数量
    for (i = 0; i < log.lh.n; i++)
    {
        // 复制块号数组
        log.lh.block[i] = lh->block[i];
    }
    brelse(buf); // 释放头块缓冲区
}

// 将内存中的日志头写入磁盘（事务提交点）  内存日志写入磁盘日志
static void write_head(void)
{
    struct buf *buf = bread(log.dev, log.start); // 读取头块
    struct logheader *hb = (struct logheader *)(buf->data);
    int i;
    hb->n = log.lh.n; // 写入块数量
    for (i = 0; i < log.lh.n; i++)
    {
        hb->block[i] = log.lh.block[i]; // 写入块号数组
    }
    bwrite(buf); // 同步写入磁盘（关键提交点）
    brelse(buf); // 释放缓冲区
}

// 从日志恢复未完成事务
static void recover_from_log(void)
{
    read_head();      // 读取磁盘日志头
    install_trans(1); // 安装事务（恢复模式）
    log.lh.n = 0;     // 重置内存日志头
    write_head();     // 清空磁盘日志（写入空头）
}

// 文件系统操作开始（获取事务许可）
void begin_op(void)
{
    acquire(&log.lock); // 获取日志锁
    while (1)
    {
        if (log.committing)
        {
            sleep(&log, &log.lock); // 等待提交完成
        }
        // 检查日志空间：当前日志块数 + (事务数+1)*最大操作块数
        else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE)
        {
            // this op might exhaust log space; wait for commit.
            sleep(&log, &log.lock); // 等待空间释放
        }
        else
        {
            log.outstanding += 1; // 增加活动事务计数
            release(&log.lock);   // 释放锁
            break;                // 退出循环
        }
    }
}

// 文件系统操作结束（可能触发提交）
void end_op(void)
{
    int do_commit = 0; // 提交标志

    acquire(&log.lock);
    log.outstanding -= 1; // 减少活动事务计数

    // 如果已处于提交状态（不应该发生）
    if (log.committing)
        panic("log.committing");

    // 检查是否需要提交（无活动事务时）
    if (log.outstanding == 0)
    {
        do_commit = 1;
        log.committing = 1; // 设置提交标志
    }
    else
    {
        // 唤醒等待日志空间的事务
        wakeup(&log);
    }
    release(&log.lock);

    // 执行提交（无锁状态避免死锁）
    if (do_commit)
    {
        // call commit w/o holding locks, since not allowed
        // to sleep with locks.
        commit(); // 实际提交
        acquire(&log.lock);
        log.committing = 0; // 清除提交标志
        wakeup(&log);       // 唤醒等待者
        release(&log.lock);
    }
}

// 将修改的缓存块写入日志区域
static void write_log(void)
{
    int tail;

    for (tail = 0; tail < log.lh.n; tail++)
    {
        // 获取日志块缓冲区
        struct buf *to = bread(log.dev, log.start + tail + 1); // log block
        // 获取待修改数据块缓冲区
        struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
        memmove(to->data, from->data, BSIZE);                  // 复制数据到日志区
        bwrite(to);                                            // 写入磁盘日志区
        brelse(from);
        brelse(to);
    }
}

// 提交事务核心操作
static void commit()
{
    // 仅当有需要提交的块时操作
    if (log.lh.n > 0)
    {
        write_log();      // 缓存块 → 日志区
        write_head();     // 写头块（事务提交点）
        install_trans(0); // 日志区 → 实际位置
        //printf("commit: %d block be wrote.\n", log.lh.block[0]);
        log.lh.n = 0;     // 重置日志头
        write_head();     // 清空日志
    }
}

/*
 * 调用方修改 b->data 后记录块
 * 记录块号并固定缓存（增加引用计数）
 * commit()/write_log() 会处理磁盘写入
 *
 * 典型用法：
 *   bp = bread(...)    // 获取块
 *   modify bp->data[]  // 修改数据
 *   log_write(bp)      // 记录日志
 *   brelse(bp)         // 释放块
 */
void log_write(struct buf *b)
{
    int i;

    // 安全检查：日志空间不足
    if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
        panic("too big a transaction");

    // 安全检查：不能在事务外调用
    if (log.outstanding < 1)
        panic("log_write outside of trans");

    acquire(&log.lock);

    // 日志吸收：检查块是否已在当前事务
    for (i = 0; i < log.lh.n; i++)
    {
        if (log.lh.block[i] == b->blockno) // log absorbtion
            break;
    }
    // 更新块号（覆盖或新增）
    log.lh.block[i] = b->blockno;
    if (i == log.lh.n)
    {
        // 如果是新块
        bpin(b);    // 固定缓存块（增加引用计数）
        log.lh.n++; // 增加日志块计数
    }
    release(&log.lock);
}
