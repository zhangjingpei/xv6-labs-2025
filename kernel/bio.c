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

// 缓冲区缓存全局管理结构
struct
{
    struct spinlock lock;    // 自旋锁，保护整个bcache结构
    struct buf buf[NBUFFER]; // 静态分配的缓冲区数组（NBUF为缓冲区总数）->NBUFFER

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    // 双向循环链表，按LRU（最近最少使用）顺序管理所有缓冲区：
    // head.next指向最近使用的缓冲区，head.prev指向最久未使用的缓冲区
    // struct buf head; // 废弃
} bcache[NBUCKET]; // 哈希桶数组，一共13个桶， 每个桶里面有三个buf数组

static uint global_timestamp = 0;

// 初始化缓冲区缓存系统
void binit(void)
{

    // 初始化bcache的自旋锁
    for (int i = 0; i < NBUCKET; i++)
    {
        initlock(&bcache[i].lock, "bcache");

        for (int j = 0; j < NBUFFER; j++)
        {
            bcache[i].buf[j].timestamp = 0; // 初始化时间戳为0
            initsleeplock(&bcache[i].buf[j].lock, "buffer");
        }
    }


}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// 获取指定设备的磁盘块对应的缓冲区（如果不存在则分配）
static struct buf *bget(uint dev, uint blockno)
{
    struct buf *b;

    uint bucket_id = hash(blockno);

    // 优先在当前哈希桶中，查找已经缓存的快
    acquire(&bcache[bucket_id].lock);
    for (int i = 0; i < NBUFFER; i++)
    {
        b = &bcache[bucket_id].buf[i];
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;

            // 增加引用的同时，更新时间戳
            update_timestamp(b);
            release(&bcache[bucket_id].lock);
            acquiresleep(&b->lock); // 获取缓冲区的睡眠锁
            return b;
        }
    }

    // 未找到缓存快，根据时间戳寻找最久未使用的缓冲区,即timestamp最小
    release(&bcache[bucket_id].lock); // 先释放哈希桶的锁
    uint min_timestamp = 0xffffffff;
    struct buf *least_used_buf = 0;
    int least_used_bucket_id = -1;

    // 环形遍历所有桶，找出其他桶中未使用的空闲块
    // 遍历所有桶的过程中，记录最少使用并且最久未使用的块
    // 如果没有空闲块，那么直接使用最少使用且最久未使用的快
    for (int i = 0, cur_bucket_id = bucket_id; i < NBUCKET; i++, cur_bucket_id++)
    {
        if (cur_bucket_id == NBUCKET)
            cur_bucket_id = 0;
        acquire(&bcache[cur_bucket_id].lock);
        for (int j = 0; j < NBUFFER; j++)
        {
            b = &bcache[cur_bucket_id].buf[j];
            if (b->refcnt == 0) // 找到未使用的快
            {
                b->dev = dev;
                b->blockno = blockno;
                b->valid = 0;
                b->refcnt = 1;
                update_timestamp(b);
                release(&bcache[cur_bucket_id].lock);
                acquiresleep(&b->lock);
                return b;
            }
            // 更新 最久未使用的块
            if (b->timestamp < min_timestamp)
            {
                min_timestamp = b->timestamp; // 找到timestamp最小的buffer
                least_used_buf = b;
                least_used_bucket_id = cur_bucket_id;
            }
        }
        release(&bcache[cur_bucket_id].lock);
    }

    // 其他桶里也没有空闲块，那么只能使用最久未使用的buffer
    if (least_used_buf && least_used_bucket_id >= 0)
    {
        acquire(&bcache[least_used_bucket_id].lock);
        // 一大堆赋值和初始化操作
        least_used_buf->dev = dev;
        least_used_buf->blockno = blockno;
        least_used_buf->valid = 0;
        least_used_buf->refcnt = 1;
        update_timestamp(least_used_buf);
        release(&bcache[least_used_bucket_id].lock);
        acquiresleep(&least_used_buf->lock);
        return least_used_buf;
    }
    panic("bget: no buffers");
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

    releasesleep(&b->lock); // 释放睡眠锁

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