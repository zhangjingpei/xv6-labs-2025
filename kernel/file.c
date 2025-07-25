/*
该文件实现文件描述符（FD）相关系统调用的底层支持，包括：

全局文件表 ftable 的管理
文件分配/关闭/引用计数操作
文件元数据读取（filestat）
文件读写操作（fileread/filewrite）
支持管道（FD_PIPE）、设备（FD_DEVICE）和索引节点（FD_INODE）三种文件类型
*/

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV]; // 设备驱动表 (字符设备/块设备)
struct
{
    struct spinlock lock;    // 文件表的自旋锁
    struct file file[NFILE]; // 全局打开文件数组
} ftable;                    // 全局文件表

// 初始化文件表​
void fileinit(void)
{
    initlock(&ftable.lock, "ftable"); // 初始化文件表的自旋锁，名称"ftable"
}

// 分配空闲文件结构​
struct file *filealloc(void)
{
    struct file *f;

    acquire(&ftable.lock); // 获取文件表锁

    for (f = ftable.file; f < ftable.file + NFILE; f++) // 遍历所有文件槽
    {
        if (f->ref == 0) // 找到空闲槽位
        {
            f->ref = 1; // 设置引用计数为1
            release(&ftable.lock);
            return f;
        }
    }
    release(&ftable.lock);
    return 0; // 无空闲槽位返回0
}

// 增加文件引用计数​
struct file *filedup(struct file *f)
{
    acquire(&ftable.lock);
    if (f->ref < 1)
        panic("filedup");
    f->ref++;
    release(&ftable.lock);
    return f;
}

//  关闭文件（引用计数减为0时释放）​
void fileclose(struct file *f)
{
    struct file ff;

    acquire(&ftable.lock);
    if (f->ref < 1) // 文件未被引用时崩溃
        panic("fileclose");

    if (--f->ref > 0) // 引用计数-1后仍>0
    {
        release(&ftable.lock);
        return; // 无需真正关闭
    }

    // 复制文件结构（避免锁内操作）
    ff = *f;
    f->ref = 0;        // 重置引用计数
    f->type = FD_NONE; // 标记为无效类型
    release(&ftable.lock);

    // 根据文件类型释放资源
    if (ff.type == FD_PIPE)
    {
        pipeclose(ff.pipe, ff.writable); // 关闭管道
    }

    else if (ff.type == FD_INODE || ff.type == FD_DEVICE)
    {
        begin_op();  // 文件系统事务开始
        iput(ff.ip); // 减少索引节点引用
        end_op();    // 文件系统事务结束
    }
}

//  获取文件元数据​
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64 addr)
{
    struct proc *p = myproc(); // 获取当前进程
    struct stat st;            // 元数据结构

    if (f->type == FD_INODE || f->type == FD_DEVICE)
    {
        ilock(f->ip);      // 锁定索引节点
        stati(f->ip, &st); // 复制元数据到st
        iunlock(f->ip);    // 解锁索引节点
        // 拷贝数据到用户空间
        if (copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
            return -1;
        return 0;
    }
    return -1; // 非INODE/DEVICE文件返回错误
}

// 从文件读取数据​
int fileread(struct file *f, uint64 addr, int n)
{
    int r = 0;

    if (f->readable == 0) // 文件不可读
        return -1;

    if (f->type == FD_PIPE)
    {
        r = piperead(f->pipe, addr, n); // 从管道读取
    }
    else if (f->type == FD_DEVICE)
    {
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
            return -1;                        // 设备号非法或驱动不存在
        r = devsw[f->major].read(1, addr, n); // 调用设备驱动读
    }
    else if (f->type == FD_INODE) // 锁定索引节点
    {
        ilock(f->ip); // 锁定索引节点
        if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
            f->off += r; // 成功时增加偏移量
        iunlock(f->ip);
    }
    else
    {
        panic("fileread"); // 未知文件类型崩溃
    }

    return r; // 返回实际读取字节数
}

// 向文件写入数据​
int filewrite(struct file *f, uint64 addr, int n)
{
    int r, ret = 0;

    if (f->writable == 0)
        return -1; // 文件不可写

    if (f->type == FD_PIPE)
    {
        ret = pipewrite(f->pipe, addr, n); // 写入管道
    }
    else if (f->type == FD_DEVICE)
    {
        // 设备合法性检查
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
            return -1;
        ret = devsw[f->major].write(1, addr, n); // 调用设备驱动写
    }
    else if (f->type == FD_INODE)
    {
        // 关键翻译（原注释）：
        // 分批写入以避免超过最大日志事务大小（含索引节点、间接块、分配块）
        // 以及2块用于非对齐写入的冗余空间
        int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE; // 计算最大单次写入量
        int i = 0;
        while (i < n) // 循环写入直到完成
        {
            int n1 = n - i;
            if (n1 > max) // 单次不超过max
                n1 = max;

            begin_op();                                           // 文件系统事务开始
            ilock(f->ip);                                         // 锁定索引节点
            if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0) // 更新文件偏移量
                f->off += r;

            // printf("filewrite: %d inode write %d bytes to file \n",f->ip->inum, r);
            iunlock(f->ip);
            end_op(); // 文件系统事务结束

            if (r != n1) // 写入失败退出
            {
                // error from writei
                break;
            }
            i += r; // 移动写入位置
        }
        ret = (i == n ? n : -1); // 全部写入成功返回n，否则-1
    }
    else
    {
        panic("filewrite");
    }

    return ret; // 返回写入结果
}
