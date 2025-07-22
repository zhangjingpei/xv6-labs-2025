// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13 // 哈希桶数量，一般用质数
#define NBUFFER                                                                                                        \
    ((NBUF + NBUCKET - 1) /                                                                                            \
     NBUCKET) // 每个桶的buf数量，NBUF默认为30，这里向上取整后结果应该是 3，即每个桶里面有 3 个 buf

// 全局缓冲区数组
struct buf buffers[NBUF];

// 缓冲区缓存全局管理结构
struct
{
    struct spinlock lock; // 自旋锁，保护整个bcache结构

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    // 双向循环链表，按LRU（最近最少使用）顺序管理所有缓冲区：
    // head.next指向最近使用的缓冲区，head.prev指向最久未使用的缓冲区

    struct buf *head; // 桶内链表头指针
} bcache[NBUCKET];    // 哈希桶数组，一共13个桶， 每个桶里面有三个buf数组

// 修改：使用64位时间戳
static uint64_t global_timestamp;

// 移动缓冲区（确保调用时不持有任何桶锁）
void move_buf(struct buf *b, uint new_bucket)
{
    uint old_bucket = b->home_bucket; // 缓冲区原先所属哈希桶
    if (old_bucket == new_bucket)
        return;
    // 按顺序加锁避免死锁
    if (old_bucket < new_bucket)
    {
        acquire(&bcache[old_bucket].lock);
        acquire(&bcache[new_bucket].lock);
    }
    else if (old_bucket > new_bucket)
    {
        acquire(&bcache[new_bucket].lock);
        acquire(&bcache[old_bucket].lock);
    }

    // 从原桶链表溢出
    if (b->prev)
        b->prev->next = b->next;
    if (b->next)
        b->next->prev = b->prev;
    if (b == bcache[old_bucket].head)
        bcache[old_bucket].head = b->next;

    // 添加到新桶链表头部
    b->next = bcache[new_bucket].head;
    b->prev = 0;
    if (bcache[new_bucket].head)
    {
        bcache[new_bucket].head->prev = b;
    }
    bcache[new_bucket].head = b;

    b->home_bucket = new_bucket;

    // 释放锁（按相反顺序）
    if (old_bucket < new_bucket)
    {
        release(&bcache[new_bucket].lock);
        release(&bcache[old_bucket].lock);
    }
    else
    {
        release(&bcache[old_bucket].lock);
        release(&bcache[new_bucket].lock);
    }
}

// 初始化缓冲区缓存系统
void binit(void)
{

    // 初始化桶锁和链表
    for (int i = 0; i < NBUCKET; i++)
    {
        initlock(&bcache[i].lock, "bcache");
        bcache[i].head = 0;
    }

    /// 初始化缓冲区并分配到桶
    for (int i = 0; i < NBUF; i++)
    {
        struct buf *b = &buffers[i];
        b->refcnt = 0;
        b->home_bucket = i % NBUCKET; // 均匀分配
        b->prev = b->next = 0;
        initsleeplock(&b->lock, "buffer");

        uint bucket = b->home_bucket;
        acquire(&bcache[bucket].lock);
        b->next = bcache[bucket].head;
        if (bcache[bucket].head)
            bcache[bucket].head->prev = b;
        bcache[bucket].head = b;
        release(&bcache[bucket].lock);
    }
}
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// 获取指定设备的磁盘块对应的缓冲区（如果不存在则分配）
static struct buf *bget(uint dev, uint blockno)
{
    uint target_bucket = hash(blockno);
    struct buf *b;

    // 阶段1：目标桶内查找
    acquire(&bcache[target_bucket].lock);
    for (b = bcache[target_bucket].head; b; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            update_timestamp(b);
            release(&bcache[target_bucket].lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // 阶段2：目标桶内找空闲缓冲区
    struct buf *candidate = 0;
    uint64 min_ts = UINT64_MAX;

    for (b = bcache[target_bucket].head; b; b = b->next)
    {
        if (b->refcnt == 0)
        {
            if (!candidate || b->timestamp < min_ts)
            {
                candidate = b;
                min_ts = b->timestamp;
            }
        }
    }

    if (candidate)
    {
        candidate->dev = dev;
        candidate->blockno = blockno;
        candidate->valid = 0;
        candidate->refcnt = 1;
        update_timestamp(candidate);
        release(&bcache[target_bucket].lock);
        acquiresleep(&candidate->lock);
        return candidate;
    }
    release(&bcache[target_bucket].lock);

    // 阶段3：全局搜索空闲缓冲区
    struct buf *global_candidate = 0;
    uint64 global_min_ts = UINT64_MAX;
    

    for (uint i = 0; i < NBUCKET; i++)
    {
        if (i == target_bucket)
            continue;

        acquire(&bcache[i].lock);
        for (b = bcache[i].head; b; b = b->next)
        {
            if (b->refcnt == 0 && b->timestamp < global_min_ts)
            {
                global_candidate = b;
                global_min_ts = b->timestamp;
                
            }
        }
        release(&bcache[i].lock);
    }

    if (!global_candidate)
        panic("bget: no free buffer");

    // 移动缓冲区到目标桶
    move_buf(global_candidate, target_bucket);

    acquire(&bcache[target_bucket].lock);
    global_candidate->dev = dev;
    global_candidate->blockno = blockno;
    global_candidate->valid = 0;
    global_candidate->refcnt = 1;
    update_timestamp(global_candidate);
    release(&bcache[target_bucket].lock);

    acquiresleep(&global_candidate->lock);
    return global_candidate;
}

// Return a locked buf with the contents of the indicated block.
// 读取磁盘块到缓冲区（返回已锁定的有效缓冲区）
struct buf *bread(uint dev, uint blockno)
{
    struct buf *b;

    b = bget(dev, blockno); // 获取缓冲区（可能已缓存或新分配）
    if (!b->valid)          // 如果数据无效
    {
        virtio_disk_rw(b, 0); // 从磁盘读取数据（0表示读操作）
        b->valid = 1;         // 标记数据有效
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
// 将缓冲区内容写入磁盘（缓冲区必须已被锁定）
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock)) // 检查当前进程是否持有睡眠锁
        panic("bwrite");         // 未持有锁则报错
    virtio_disk_rw(b, 1);        // 向磁盘写入数据（1表示写操作）
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
// 释放锁定的缓冲区（调整其在LRU链表中的位置）
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock)) // 检查是否持有锁
        panic("brelse");

    int bucket_id = hash(b->blockno);
    acquire(&bcache[bucket_id].lock);
    b->refcnt--;
    release(&bcache[bucket_id].lock);

    releasesleep(&b->lock); // 释放睡眠锁--> 唤醒等待者

    // acquire(&bcache.lock); // 获取bcache全局锁
    // b->refcnt--;           // 减少引用计数
    // if (b->refcnt == 0)    // 如果无其他引用者
    // {
    //     // no one is waiting for it.
    //     // 从当前位置移除缓冲区
    //     b->next->prev = b->prev;
    //     b->prev->next = b->next;

    //     // 将缓冲区插入链表头部（标记为最近使用）
    //     b->next = bcache.head.next;
    //     b->prev = &bcache.head;
    //     bcache.head.next->prev = b;
    //     bcache.head.next = b;
    // }

    // release(&bcache.lock); // 释放全局锁
}

// 增加缓冲区引用计数（"固定"缓冲区防止被回收）
void bpin(struct buf *b)
{
    int bucket_id = hash(b->blockno);
    acquire(&bcache[bucket_id].lock);
    // acquire(&bcache.lock);
    b->refcnt++;
    // release(&bcache.lock);
    release(&bcache[bucket_id].lock);
}

// 减少缓冲区引用计数（"解固定"缓冲区）
void bunpin(struct buf *b)
{
    int bucket_id = hash(b->blockno);
    acquire(&bcache[bucket_id].lock);
    // acquire(&bcache.lock);
    b->refcnt--;
    // release(&bcache.lock);
    release(&bcache[bucket_id].lock);
}

uint hash(uint blockno)
{
    return blockno % NBUCKET;
}

void update_timestamp(struct buf *b)
{
    // 这里的global_timestamp会有溢出问题，暂不考虑优化
    b->timestamp = __sync_fetch_and_add(&global_timestamp, 1);
}