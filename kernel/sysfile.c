//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// 从系统调用参数获取文件描述符和对应文件结构
static int argfd(int n, int *pfd, struct file **pf)
{
    int fd;
    struct file *f;

    if (argint(n, &fd) < 0)
        return -1;
    // 验证fd有效性：范围0~NOFILE-1且已打开
    if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
        return -1;
    // 返回文件描述符和文件结构指针
    if (pfd)
        *pfd = fd;

    if (pf)
        *pf = f;
    return 0;
}

// 为文件分配空闲文件描述符
static int fdalloc(struct file *f)
{
    int fd;
    struct proc *p = myproc();

    // 遍历进程的文件描述符表
    for (fd = 0; fd < NOFILE; fd++)
    {
        if (p->ofile[fd] == 0)
        {
            p->ofile[fd] = f; // 关联文件对象
            return fd;        // 返回分配的fd
        }
    }
    return -1;
}

// 复制文件描述符
uint64 sys_dup(void)
{
    struct file *f;
    int fd;

    // 获取原文件描述符对应的文件对象
    if (argfd(0, 0, &f) < 0)
        return -1;

    // 分配新文件描述符
    if ((fd = fdalloc(f)) < 0)
        return -1;

    filedup(f); // 增加文件引用计数
    return fd;
}

// 读文件
uint64 sys_read(void)
{
    struct file *f;
    int n;
    uint64 p; // 用户空间缓冲区地址

    // 获取fd/字节数/缓冲区地址
    if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
        return -1;

    return fileread(f, p, n); // 执行实际读取
}

// 写文件
uint64 sys_write(void)
{
    struct file *f;
    int n;
    uint64 p;

    if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
        return -1;

    return filewrite(f, p, n); // 执行实际写入
}

// 关闭文件
uint64 sys_close(void)
{
    int fd;
    struct file *f;

    // 获取fd和对应文件对象
    if (argfd(0, &fd, &f) < 0)
        return -1;

    myproc()->ofile[fd] = 0; // 清除fd表项
    fileclose(f);            // 减少引用计数，必要时释放
    return 0;
}

// 获取文件状态
uint64 sys_fstat(void)
{
    struct file *f;
    uint64 st; // 用户空间stat结构地址

    if (argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
        return -1;

    return filestat(f, st); // 填充stat结构
}

// 创建硬链接（syscall: link(oldpath, newpath)）
// 返回：0成功，-1失败（如源文件不存在、目标已存在、链接目录等）
uint64 sys_link(void)
{
    char name[DIRSIZ], new[MAXPATH], old[MAXPATH];  // 目录项名称、新路径、旧路径
    struct inode *dp, *ip;  // 目标目录inode、源文件inode

    // 获取旧路径和新路径（从用户空间复制到内核缓冲区）
    if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
        return -1;

    begin_op();  // 开始文件系统事务（防止中途崩溃导致不一致）

    // 查找源文件inode（namei解析路径并返回inode）/home/user/file.txt  返回file.txt的inode
    if ((ip = namei(old)) == 0)
    {
        end_op();  // 结束事务
        return -1;
    }

    ilock(ip);  // 获取inode睡眠锁（保护inode数据）

    // 禁止链接目录（防止目录循环，如A链接到B，B又链接到A）
    if (ip->type == T_DIR)
    {
        iunlockput(ip);  // 释放锁并减少引用计数
        end_op();
        return -1;
    }

    ip->nlink++;  // 增加源文件的硬链接计数（硬链接通过nlink关联）
    iupdate(ip);  // 更新inode到磁盘（先更新缓冲区，再写磁盘）因为 ip->nlink++了
    iunlock(ip);  // 释放锁（此时ip仍被引用，未释放）

    // 解析新路径的父目录和最后一级名称（nameiparent返回父目录inode，name为最后一级名称）
    if ((dp = nameiparent(new, name)) == 0) // name = link.txt dp = /user的inode而不是link.txt的
        goto bad;  // 父目录不存在，跳转错误处理

    ilock(dp);  // 锁定父目录inode
    // 验证源文件和目标目录在同一设备，然后在父目录中创建新目录项（关联name和源文件inode号）
    if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0)
    {
        iunlockput(dp);  // 释放父目录inode并减少引用
        goto bad;
    }
    iunlockput(dp);  // 成功创建目录项，释放父目录inode
    iput(ip);        // 减少源文件inode引用计数（之前namei获取的引用）

    end_op();  // 结束文件系统事务
    return 0;

bad:  // 错误处理：回滚源文件链接计数
    ilock(ip);
    ip->nlink--;    // 撤销之前的nlink++
    iupdate(ip);    // 更新到磁盘
    iunlockput(ip); // 释放并减少引用
    end_op();
    return -1;
}
// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp)
{
    int off;
    struct dirent de;

    for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de))
    {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("isdirempty: readi");
        if (de.inum != 0)
            return 0;
    }
    return 1;
}


// 删除文件/目录（syscall: unlink(path)）
// 返回：0成功，-1失败（如目录非空、文件不存在等）
uint64 sys_unlink(void)
{
    struct inode *ip, *dp;  // 目标inode、父目录inode
    struct dirent de;       // 目录项结构
    char name[DIRSIZ], path[MAXPATH];  // 最后一级名称、路径
    uint off;               // 目录项在目录中的偏移

    if (argstr(0, path, MAXPATH) < 0)  // 获取路径
        return -1;

    begin_op();  // 开始文件系统事务
    // 解析路径的父目录和最后一级名称
    if ((dp = nameiparent(path, name)) == 0)
    {
        end_op();
        return -1;
    }
    ilock(dp);  // 锁定父目录

    // 禁止删除"."（当前目录）和".."（父目录）
    if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
        goto bad;
    // 在父目录中查找目标文件/目录的inode和偏移
    if ((ip = dirlookup(dp, name, &off)) == 0)
        goto bad;  // 目标不存在
    ilock(ip);  // 锁定目标inode

    // 检查目标是否合法：链接计数至少为1（防止重复删除）
    if (ip->nlink < 1)
        panic("unlink: nlink < 1");
    // 若目标是目录，必须为空（仅含"."和".."）
    if (ip->type == T_DIR && !isdirempty(ip))
    {
        iunlockput(ip);  // 释放目标inode
        goto bad;
    }

    // 将父目录中目标对应的目录项清零（删除链接）
    memset(&de, 0, sizeof(de));
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("unlink: writei");

    // 若目标是目录，父目录的链接计数减1（目录的".."指向父目录，删除目录需减少父目录nlink）
    if (ip->type == T_DIR)
    {
        dp->nlink--;
        iupdate(dp);  // 更新父目录inode到磁盘
    }
    iunlockput(dp);  // 释放父目录inode

    ip->nlink--;     // 目标的链接计数减1（若为0，后续将释放inode和数据块）
    iupdate(ip);     // 更新目标inode到磁盘
    iunlockput(ip);  // 释放目标inode

    end_op();  // 结束事务
    return 0;

bad:  // 错误处理：释放父目录inode
    iunlockput(dp);
    end_op();
    return -1;
}

static struct inode *create(char *path, short type, short major, short minor)
{
    struct inode *ip, *dp;
    char name[DIRSIZ];

    if ((dp = nameiparent(path, name)) == 0)
        return 0;

    ilock(dp);

    if ((ip = dirlookup(dp, name, 0)) != 0)
    {
        iunlockput(dp);
        ilock(ip);
        if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
            return ip;
        iunlockput(ip);
        return 0;
    }

    if ((ip = ialloc(dp->dev, type)) == 0)
        panic("create: ialloc");

    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    if (type == T_DIR)
    {                // Create . and .. entries.
        dp->nlink++; // for ".."
        iupdate(dp);
        // No ip->nlink++ for ".": avoid cyclic ref count.
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
            panic("create dots");
    }

    if (dirlink(dp, name, ip->inum) < 0)
        panic("create: dirlink");

    iunlockput(dp);

    return ip;
}

uint64 sys_open(void)
{
    char path[MAXPATH];
    int fd, omode;
    struct file *f;
    struct inode *ip;
    int n;

    if ((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
        return -1;

    begin_op();

    if (omode & O_CREATE)
    {
        ip = create(path, T_FILE, 0, 0);
        if (ip == 0)
        {
            end_op();
            return -1;
        }
    }
    else
    {
        if ((ip = namei(path)) == 0)
        {
            end_op();
            return -1;
        }
        ilock(ip);
        // 新增部分, 处理符号链接
        int depth = 0;        // 解析深度上限
        char target[MAXPATH]; // 128
        // 尝试解析符号链接（最多解析10层）
        while (depth < 10 && ip->type == T_SYMLINK && !(omode & O_NOFOLLOW))
        {
            // 读取链接目标路径
            if (readi(ip, 0, (uint64)target, 0, MAXPATH) <= 0)
            {
                iunlockput(ip);
                end_op();
                return -1;
            }
            iunlockput(ip);

            if ((ip = namei(target)) == 0)
            {
                end_op();
                return -1;
            }
            ilock(ip);
            depth++;
        }
        if (depth >= 10)
        {
            iunlockput(ip);
            end_op();
            return -1;
        }

        if (ip->type == T_DIR && omode != O_RDONLY)
        {
            iunlockput(ip);
            end_op();
            return -1;
        }
    }

    if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV))
    {
        iunlockput(ip);
        end_op();
        return -1;
    }

    if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0)
    {
        if (f)
            fileclose(f);
        iunlockput(ip);
        end_op();
        return -1;
    }

    if (ip->type == T_DEVICE)
    {
        f->type = FD_DEVICE;
        f->major = ip->major;
    }
    else
    {
        f->type = FD_INODE;
        f->off = 0;
    }
    f->ip = ip;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

    if ((omode & O_TRUNC) && ip->type == T_FILE)
    {
        itrunc(ip);
    }

    iunlock(ip);
    end_op();

    return fd;
}

// 创建目录（syscall: mkdir(path)）
// 返回：0成功，-1失败
uint64 sys_mkdir(void)
{
    char path[MAXPATH];
    struct inode *ip;

    begin_op();
    // 创建类型为T_DIR的inode（create内部会处理父目录、目录项创建，并添加".",".."）
    if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0)
    {
        end_op();
        return -1;
    }
    iunlockput(ip);  // 释放inode（create返回的inode已锁定，需解锁并减少引用）
    end_op();
    return 0;
}


// 创建设备文件（syscall: mknod(path, major, minor)）
// 返回：0成功，-1失败
uint64 sys_mknod(void)
{
    struct inode *ip;
    char path[MAXPATH];
    int major, minor;  // 设备号（主设备号、次设备号）

    begin_op();
    // 获取路径、主设备号、次设备号，创建类型为T_DEVICE的inode
    if ((argstr(0, path, MAXPATH)) < 0 || argint(1, &major) < 0 || argint(2, &minor) < 0 ||
        (ip = create(path, T_DEVICE, major, minor)) == 0)
    {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

// 修改当前工作目录（syscall: chdir(path)）
// 返回：0成功，-1失败（如路径非目录）
uint64 sys_chdir(void)
{
    char path[MAXPATH];
    struct inode *ip;
    struct proc *p = myproc();  // 当前进程

    begin_op();
    if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0)  // 解析路径获取inode
    {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type != T_DIR)  // 必须是目录
    {
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);      // 无需锁定（进程cwd仅自身修改）
    iput(p->cwd);     // 释放原当前目录inode引用
    end_op();
    p->cwd = ip;      // 更新进程当前目录为新inode
    return 0;
}

uint64 sys_exec(void)
{
    char path[MAXPATH], *argv[MAXARG];
    int i;
    uint64 uargv, uarg;

    if (argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0)
    {
        return -1;
    }
    memset(argv, 0, sizeof(argv));
    for (i = 0;; i++)
    {
        if (i >= NELEM(argv))
        {
            goto bad;
        }
        if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0)
        {
            goto bad;
        }
        if (uarg == 0)
        {
            argv[i] = 0;
            break;
        }
        argv[i] = kalloc();
        if (argv[i] == 0)
            goto bad;
        if (fetchstr(uarg, argv[i], PGSIZE) < 0)
            goto bad;
    }

    int ret = exec(path, argv);

    for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
        kfree(argv[i]);

    return ret;

bad:
    for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
        kfree(argv[i]);
    return -1;
}

uint64 sys_pipe(void)
{
    uint64 fdarray; // user pointer to array of two integers
    struct file *rf, *wf;
    int fd0, fd1;
    struct proc *p = myproc();

    if (argaddr(0, &fdarray) < 0)
        return -1;
    if (pipealloc(&rf, &wf) < 0)
        return -1;
    fd0 = -1;
    if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0)
    {
        if (fd0 >= 0)
            p->ofile[fd0] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
        copyout(p->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0)
    {
        p->ofile[fd0] = 0;
        p->ofile[fd1] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    return 0;
}

uint64 sys_symlink(void)
{
    char target[MAXPATH], path[MAXPATH];
    if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    {
        return -1;
    }
    begin_op();

    // 软链接是一个特殊的文件，其存储的数据为目标文件的路径信息，因此我们在创建软链接时需要创建一个新的inode
    struct inode *ip = create(path, T_SYMLINK, 0, 0);
    if (ip == 0)
    {
        end_op();
        return -1;
    }

    // 将target字符串写入inode的第一个直接块中
    if (writei(ip, 0, (uint64)target, 0, strlen(target)) < 0)
    {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}