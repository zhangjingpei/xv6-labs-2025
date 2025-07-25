// 文件系统实现。分为五层：
//   + 块层：原始磁盘块的分配器。
//   + 日志层：多步更新的崩溃恢复机制。
//   + 文件层：inode分配器，读写操作，元数据管理。
//   + 目录层：特殊内容（其他inode列表）的inode。
//   + 名称层：路径名（如/usr/rtm/xv6/fs.c）解析。
//
// 本文件包含底层文件系统操作例程。
// （更高层）系统调用实现在sysfile.c中。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

// 取最小值宏
#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
// 全局超级块结构，每个磁盘设备一个
struct superblock sb;

// 读取超级块
// 参数：设备号 dev，超级块指针 sb
static void readsb(int dev, struct superblock *sb)
{
    struct buf *bp;

    // 读取设备上第1块（超级块位置固定）存放至缓冲区bp
    bp = bread(dev, 1);

    // 将缓冲区数据复制到超级块结构sb
    memmove(sb, bp->data, sizeof(*sb));
    // 释放缓冲区
    brelse(bp);
}

// 初始化文件系统
// 参数：设备号 dev
void fsinit(int dev)
{
    // 读取超级块
    readsb(dev, &sb);
    // 验证魔数（文件系统标识）
    if (sb.magic != FSMAGIC)
        panic("invalid file system");

    // 初始化日志系统 log.c
    initlog(dev, &sb);
}

// 将指定块清零
// 参数：设备号 dev，块号 bno
static void bzero(int dev, int bno)
{
    struct buf *bp;

    bp = bread(dev, bno);
    // 将缓冲区数据清零
    memset(bp->data, 0, BSIZE);
    // 写入日志（确保原子性）
    log_write(bp);
    // 释放缓冲区
    brelse(bp);
}

/********************块管理********************/

// 分配一个清零的磁盘块
// 参数：设备号 dev
// 返回：分配的块号
static uint balloc(uint dev)
{
    int b, bi, m;
    struct buf *bp;

    bp = 0;
    // 遍历所有块组（每个块组包含BPB个块）
    for (b = 0; b < sb.size; b += BPB) // BPB=8192bit位  sb.size=200,000
    {
        // 读取位图块（计算位图块位置：BBLOCK）
        bp = bread(dev, BBLOCK(b, sb)); // bread根据块号得到缓冲区指针
        // 遍历当前块组中的位
        for (bi = 0; bi < BPB && b + bi < sb.size; bi++)
        {
            // 计算位掩码（每字节8位）
            m = 1 << (bi % 8);
            // 检查位图是否标记为空闲
            if ((bp->data[bi / 8] & m) == 0)
            {
                // 标记为已使用
                bp->data[bi / 8] |= m; // Mark block in use.
                // 更新位图到磁盘（通过日志）
                log_write(bp);
                brelse(bp);
                // 将新分配块清零
                bzero(dev, b + bi);
                return b + bi;
            }
        }
        brelse(bp);
    }
    panic("balloc: out of blocks");
}

// 释放磁盘块---只需清除位图上的标记
// 参数：设备号 dev，块号 b
static void bfree(int dev, uint b)
{
    struct buf *bp;
    int bi, m;

    bp = bread(dev, BBLOCK(b, sb));
    // 计算块在位图中的位置
    bi = b % BPB;
    m = 1 << (bi % 8);
    // 检查块是否已被释放（避免重复释放）
    if ((bp->data[bi / 8] & m) == 0)
        panic("freeing free block");
    // 清除位图中的占用标记
    bp->data[bi / 8] &= ~m;
    // 更新位图到磁盘
    log_write(bp);
    brelse(bp);
}

/********************Inode管理********************/
// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

// Inode缓存结构
struct
{
    struct spinlock lock;       // 保护inode缓存的锁
    struct inode inode[NINODE]; // 缓存条目数组
} icache;

void iinit()
{
    int i = 0;

    initlock(&icache.lock, "icache");
    // 初始化每个inode的睡眠锁
    for (i = 0; i < NINODE; i++)
    {
        initsleeplock(&icache.inode[i].lock, "inode");
    }
}

// 分配inode（内部函数声明）
static struct inode *iget(uint dev, uint inum);

// 在设备上分配inode并标记类型
// 参数：设备号 dev，inode类型 type
// 返回：未锁定但已分配且被引用的inode
struct inode *ialloc(uint dev, short type)
{
    int inum;
    struct buf *bp;
    struct dinode *dip; // 磁盘inode结构

    // 遍历所有inode（从1开始，0号保留）
    for (inum = 1; inum < sb.ninodes; inum++) // sb.ninodes = 200
    {
        // 读取包含目标inode的磁盘块
        bp = bread(dev, IBLOCK(inum, sb));
        // 定位具体inode（每个块包含IPB个inode）
        dip = (struct dinode *)bp->data + inum % IPB;
        // 检查是否空闲（type为0）
        if (dip->type == 0)
        {
            // 初始化磁盘inode
            memset(dip, 0, sizeof(*dip));
            dip->type = type;
            // 标记已分配（写入日志）
            log_write(bp); // mark it allocated on the disk
            // printf("ialloc: inode and write %d for new file.\n", IBLOCK(inum, sb));
            brelse(bp);
            return iget(dev, inum);
        }
        brelse(bp);
    }
    panic("ialloc: no inodes");
}

// 将内存修改后的inode写回磁盘inode
// 注意：调用者必须持有ip->lock
void iupdate(struct inode *ip)
{
    struct buf *bp;
    struct dinode *dip;

    // 读取inode所在的缓冲区block
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode *)bp->data + ip->inum % IPB;

    // 将内存inode数据复制到磁盘inode
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    // 块地址数组
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    // 写回磁盘（通过日志）
    log_write(bp);
    // 根据inode类型打印不同信息
    // if (ip->type == T_DIR)
    // printf("iupdate: %d update root inode\n", IBLOCK(ip->inum, sb)); // 目录inode
    // else
    // printf("iupdate: %d update inode for file\n", IBLOCK(ip->inum, sb)); // 文件inode
    brelse(bp);
}

// 根据设备号和inode号获取inode（不锁定/不读盘）
// 参数：设备号 dev，inode号 inum
// 返回：内存inode结构
static struct inode *iget(uint dev, uint inum)
{
    struct inode *ip, *empty;

    acquire(&icache.lock);

    // 查找是否已在缓存中
    empty = 0;
    for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) // NINODE=50
    {
        // 检查引用、设备和inode号是否匹配
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum)
        {
            ip->ref++; // 增加引用计数
            release(&icache.lock);
            return ip; // 返回缓存条目
        }
        // 记录第一个空闲槽位
        if (empty == 0 && ip->ref == 0) // Remember empty slot.
            empty = ip;
    }

    if (empty == 0)
        panic("iget: no inodes");

    // 使用空闲槽位
    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0; // 标记数据未加载
    release(&icache.lock);

    return ip;
}

// 增加inode引用计数
// 返回ip以便链式调用
struct inode *idup(struct inode *ip)
{
    acquire(&icache.lock);
    ip->ref++;
    release(&icache.lock);
    return ip;
}

// 锁定inode并从磁盘加载数据
// 参数：目标inode
void ilock(struct inode *ip)
{
    struct buf *bp;
    struct dinode *dip;

    if (ip == 0 || ip->ref < 1)
        panic("ilock");

    // 获取睡眠锁（可能阻塞）
    acquiresleep(&ip->lock);

    // 若数据未加载，从磁盘读取
    if (ip->valid == 0)
    {
        bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        dip = (struct dinode *)bp->data + ip->inum % IPB;
        // 复制元数据
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        brelse(bp);
        ip->valid = 1;
        if (ip->type == 0)
            panic("ilock: no type");
    }
}

// 解锁inode
void iunlock(struct inode *ip)
{
    // 校验锁状态和引用
    if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
        panic("iunlock");

    releasesleep(&ip->lock); // 释放睡眠锁
}

// 减少inode引用计数
// 若引用归零且无链接，则释放磁盘资源
// 注意：所有iput调用需在事务中（可能释放inode）
void iput(struct inode *ip)
{
    acquire(&icache.lock);

    if (ip->ref == 1 && ip->valid && ip->nlink == 0)
    {
        // inode has no links and no other references: truncate and free.

        // ip->ref == 1 means no other process can have ip locked,
        // so this acquiresleep() won't block (or deadlock).
        acquiresleep(&ip->lock);

        release(&icache.lock);

        // 截断文件内容（释放所有块）
        itrunc(ip);
        ip->type = 0;  // 标记为未分配
        iupdate(ip);   // 更新磁盘
        ip->valid = 0; // 标记数据无效

        releasesleep(&ip->lock);

        acquire(&icache.lock);
    }

    ip->ref--;
    release(&icache.lock);
}

// 常用组合：解锁后减少引用
void iunlockput(struct inode *ip)
{
    iunlock(ip);
    iput(ip);
}

/********************文件内容管理********************/

// 映射逻辑块号到物理块号（必要时分配）
// 参数：inode指针 ip，逻辑块号 bn
// 返回：物理块地址
static uint bmap(struct inode *ip, uint bn)
{
    uint addr, *a; // addr 存储物理块号，a 用于访问缓冲区
    struct buf *bp;

    // 直接块处理
    if (bn < NDIRECT)
    {
        // 若未分配则分配
        if ((addr = ip->addrs[bn]) == 0)
            ip->addrs[bn] = addr = balloc(ip->dev);
        return addr;
    }
    bn -= NDIRECT; // 调整逻辑块号

    // 如果bn超出了11个，也就说明需要通过一级间接块去获取
    if (bn < NINDIRECT)
    {
        // 加载间接块（必要时分配）
        if ((addr = ip->addrs[NDIRECT]) == 0)
            ip->addrs[NDIRECT] = addr = balloc(ip->dev);

        // 从硬盘中读出这个块
        bp = bread(ip->dev, addr);
        a = (uint *)bp->data;
        // 处理目标块
        if ((addr = a[bn]) == 0)
        {
            a[bn] = addr = balloc(ip->dev); // 分配新块
            log_write(bp);                  // 更新间接块
        }
        brelse(bp);
        return addr;
    }

    // 需要从二级间接块中寻找
    bn -= NINDIRECT;
    if (bn < NDINDIRECT)
    {
        // inode的二级间接块还未分配
        if ((addr = ip->addrs[NDIRECT + 1]) == 0)
        {
            ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
        }
        int level1 = bn / NINDIRECT; // et. 500 / 256 = 1  二级是索引1
        int level2 = bn % NINDIRECT; // et. 500 % 256 = 244  一级是索引244

        // 读取level1二级间接块
        bp = bread(ip->dev, addr);
        a = (uint *)bp->data;
        if ((addr = a[level1]) == 0)
        {
            a[level1] = addr = balloc(ip->dev); // 此时 addr存放的是二级索引1分配的块号 比如 600
            log_write(bp);
        }
        brelse(bp);

        bp = bread(ip->dev, addr);
        a = (uint *)bp->data;
        if ((addr = a[level2]) == 0)
        {
            a[level2] = addr =
                balloc(ip->dev); // 此时addr存放的是一级索引244分配的块号 比如1000  1000块里面存放的就是数据了
            log_write(bp);
        }
        brelse(bp);
        return addr;
    }

    panic("bmap: out of range");
}

// 截断inode（释放所有数据块）
// 注意：调用者必须持有ip->lock
void itrunc(struct inode *ip)
{
    int i, j;
    struct buf *bp;
    uint *a;

    // 释放直接块
    for (i = 0; i < NDIRECT; i++)
    {
        if (ip->addrs[i])
        {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    // 处理间接块
    if (ip->addrs[NDIRECT])
    {
        bp = bread(ip->dev, ip->addrs[NDIRECT]);
        a = (uint *)bp->data;
        // 释放所有间接块
        for (j = 0; j < NINDIRECT; j++)
        {
            if (a[j])
                bfree(ip->dev, a[j]);
        }
        // 释放间接块本身
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    // 处理二级间接块
    struct buf *bp1;
    uint *a1;
    if (ip->addrs[NDIRECT + 1])
    {
        bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
        a = (uint *)bp->data;
        // 处理一级间接块
        for (int i = 0; i < NINDIRECT; i++)
        {
            if (a[i])
            {
                bp1 = bread(ip->dev, a[i]);
                a1 = (uint *)bp1->data;
                for (int j = 0; j < NINDIRECT; j++)
                {
                    if (a1[j])
                    {
                        bfree(ip->dev, a1[j]);
                    }
                }
                brelse(bp1);
                bfree(ip->dev, a[i]);
            }
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT + 1]);
        ip->addrs[NDIRECT + 1] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}

// 复制inode元数据到stat结构
// 注意：调用者必须持有ip->lock
void stati(struct inode *ip, struct stat *st)
{
    st->dev = ip->dev;
    st->ino = ip->inum;
    st->type = ip->type;
    st->nlink = ip->nlink;
    st->size = ip->size;
}

// 从inode读取数据
// 参数：user_dst指示目标地址类型（用户/内核空间）
// 返回：实际读取字节数
int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
    uint tot, m;
    struct buf *bp;

    // 校验偏移范围
    if (off > ip->size || off + n < off)
        return 0;
    // 调整读取长度（避免越界）
    if (off + n > ip->size)
        n = ip->size - off;

    // 分段读取
    for (tot = 0; tot < n; tot += m, off += m, dst += m)
    {
        // 获取数据块（通过bmap）
        bp = bread(ip->dev, bmap(ip, off / BSIZE));
        // 计算本次读取长度（考虑块边界）
        m = min(n - tot, BSIZE - off % BSIZE);
        // 复制数据到目标地址
        if (either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1)
        {
            brelse(bp);
            tot = -1; // 读取失败
            break;
        }
        brelse(bp);
    }
    return tot;
}

// 向inode写入数据
// 返回：实际写入字节数
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
    uint tot, m;
    struct buf *bp;

    // 校验偏移范围
    if (off > ip->size || off + n < off)
        return -1;
    // 文件大小限制检查
    if (off + n > MAXFILE * BSIZE) // (12 + 256) * 1024
        return -1;

    // 分段写入
    for (tot = 0; tot < n; tot += m, off += m, src += m)
    {
        // 获取数据块（必要时分配）
        bp = bread(ip->dev, bmap(ip, off / BSIZE));
        // 计算本次写入长度
        m = min(n - tot, BSIZE - off % BSIZE);
        // 从源地址复制数据到缓冲区block
        if (either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1)
        {
            brelse(bp);
            break;
        }
        // 记录写操作（日志提交）
        log_write(bp);
        brelse(bp);
    }

    if (off > ip->size)
        ip->size = off;

    // write the i-node back to disk even if the size didn't change
    // because the loop above might have called bmap() and added a new
    // block to ip->addrs[].
    // 更新文件大小
    iupdate(ip);

    return tot; // 返回写入字节数
}

/********************目录操作********************/

// 目录项名称比较（比较前DIRSIZ个字符）
int namecmp(const char *s, const char *t)
{
    return strncmp(s, t, DIRSIZ);
}

// 在目录中查找目录项
// 参数：目录inode dp，目标名称 name，偏移指针 poff（可选）
// 返回：目标inode（未锁定），失败返回0
struct inode *dirlookup(struct inode *dp, char *name, uint *poff)
{
    uint off, inum;
    struct dirent de; // 目录项结构

    if (dp->type != T_DIR)
        panic("dirlookup not DIR");

    // 遍历目录项（每个目录项大小sizeof(de)）
    for (off = 0; off < dp->size; off += sizeof(de))
    {
        // 读取目录项
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlookup read");
        // 跳过空闲条目
        if (de.inum == 0)
            continue;
        // 名称匹配
        if (namecmp(name, de.name) == 0)
        {
            // 返回偏移位置（可选）
            // entry matches path element
            if (poff)
                *poff = off;
            inum = de.inum;
            // 返回目标inode
            return iget(dp->dev, inum);
        }
    }

    return 0;
}

// 创建新目录项
// 参数：目录inode dp，名称 name，目标inode号 inum
// 返回：成功0，失败-1
int dirlink(struct inode *dp, char *name, uint inum)
{
    int off;
    struct dirent de;
    struct inode *ip;

    // 检查名称是否已存在
    if ((ip = dirlookup(dp, name, 0)) != 0)
    {
        iput(ip);  // 释放引用
        return -1; // 名称冲突
    }

    // 查找空闲目录项
    for (off = 0; off < dp->size; off += sizeof(de))
    {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlink read");
        // 找到空闲项
        if (de.inum == 0)
            break;
    }

    // 填充目录项
    strncpy(de.name, name, DIRSIZ);
    de.inum = inum;
    // 写入目录项
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("dirlink");

    // printf("dirlink: %d record new file in directory's data block\n",
    // dp->inum); // 新增打印
    return 0;
}

/********************路径解析********************/

// 提取路径中的下一个元素
// 参数：输入路径 path，输出名称 name
// 返回：剩余路径指针
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char *skipelem(char *path, char *name)
{
    char *s;
    int len;

    // 跳过前导斜杠
    while (*path == '/')
        path++;
    // 路径结束判断
    if (*path == 0)
        return 0;
    s = path;
    // 定位到下一个斜杠或结尾
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    // 复制名称（最大DIRSIZ字节）
    if (len >= DIRSIZ)
        memmove(name, s, DIRSIZ);
    else
    {
        memmove(name, s, len);
        name[len] = 0; // 添加终止符
    }
    // 跳过后续斜杠
    while (*path == '/')
        path++;
    return path;
}

// 路径解析核心函数
// 参数：路径 path，标志 nameiparent（是否返回父目录），输出名称 name
// 返回：目标inode（或父inode）
static struct inode *namex(char *path, int nameiparent, char *name)
{
    struct inode *ip, *next;

    // 绝对路径从根开始，否则从当前目录开始
    if (*path == '/')
        ip = iget(ROOTDEV, ROOTINO); // 根inode
    else
        ip = idup(myproc()->cwd); // 进程当前目录

    // 逐级解析路径
    while ((path = skipelem(path, name)) != 0)
    {
        ilock(ip);
        // 必须为目录类型
        if (ip->type != T_DIR)
        {
            iunlockput(ip);
            return 0;
        }
        // 父目录模式且路径结束：返回当前目录（父目录）
        if (nameiparent && *path == '\0')
        {
            // Stop one level early.
            iunlock(ip);
            return ip;
        }
        // 在目录中查找下一级
        if ((next = dirlookup(ip, name, 0)) == 0)
        {
            // 释放当前目录，继续下一级
            iunlockput(ip);
            return 0;
        }
        iunlockput(ip);
        ip = next;
    }
    // 父目录模式需额外处理
    if (nameiparent)
    {
        iput(ip);
        return 0;
    }
    return ip; // 返回最终inode
}

// 路径解析接口：返回目标inode
struct inode *namei(char *path)
{
    char name[DIRSIZ];
    return namex(path, 0, name);
}

// 路径解析接口：返回父目录inode并复制最后一级名称
struct inode *nameiparent(char *path, char *name)
{
    return namex(path, 1, name);
}
