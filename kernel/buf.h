
// 定义缓冲区结构体，表示一个磁盘块缓存
struct buf
{
    int valid;             // has data been read from disk? // 数据是否已从磁盘读取（有效标志）
    int disk;              // does disk "own" buf?// 磁盘是否"拥有"该缓冲区（用于同步）
    uint dev;              // 设备号
    uint blockno;          // 磁盘块号
    struct sleeplock lock; // 睡眠锁，用于进程同步
    uint refcnt;           // 引用计数，记录使用该缓冲区的进程数
    // 废弃掉原链表节点
    // struct buf *prev;      // LRU cache list // LRU缓存链表的前驱指针
    // struct buf *next;      // LRU缓存链表的后继指针
    uchar data[BSIZE]; // 实际存储磁盘块数据的内存空间（BSIZE通常为512或1024字节）
    uint timestamp;    // 新增时间戳字段
};
