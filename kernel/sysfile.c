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

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd(int n, int *pfd, struct file **pf)
{
    int fd;
    struct file *f;

    if (argint(n, &fd) < 0)
        return -1;
    if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
        return -1;
    if (pfd)
        *pfd = fd;
    if (pf)
        *pf = f;
    return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int fdalloc(struct file *f)
{
    int fd;
    struct proc *p = myproc();

    for (fd = 0; fd < NOFILE; fd++)
    {
        if (p->ofile[fd] == 0)
        {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

uint64 sys_dup(void)
{
    struct file *f;
    int fd;

    if (argfd(0, 0, &f) < 0)
        return -1;
    if ((fd = fdalloc(f)) < 0)
        return -1;
    filedup(f);
    return fd;
}

uint64 sys_read(void)
{
    struct file *f;
    int n;
    uint64 p;

    if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
        return -1;
    return fileread(f, p, n);
}

uint64 sys_write(void)
{
    struct file *f;
    int n;
    uint64 p;

    if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
        return -1;

    return filewrite(f, p, n);
}

uint64 sys_close(void)
{
    int fd;
    struct file *f;

    if (argfd(0, &fd, &f) < 0)
        return -1;
    myproc()->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

uint64 sys_fstat(void)
{
    struct file *f;
    uint64 st; // user pointer to struct stat

    if (argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
        return -1;
    return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64 sys_link(void)
{
    char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
    struct inode *dp, *ip;

    if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
        return -1;

    begin_op();
    if ((ip = namei(old)) == 0)
    {
        end_op();
        return -1;
    }

    ilock(ip);
    if (ip->type == T_DIR)
    {
        iunlockput(ip);
        end_op();
        return -1;
    }

    ip->nlink++;
    iupdate(ip);
    iunlock(ip);

    if ((dp = nameiparent(new, name)) == 0)
        goto bad;
    ilock(dp);
    if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0)
    {
        iunlockput(dp);
        goto bad;
    }
    iunlockput(dp);
    iput(ip);

    end_op();

    return 0;

bad:
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
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

uint64 sys_unlink(void)
{
    struct inode *ip, *dp;
    struct dirent de;
    char name[DIRSIZ], path[MAXPATH];
    uint off;

    if (argstr(0, path, MAXPATH) < 0)
        return -1;

    begin_op();
    if ((dp = nameiparent(path, name)) == 0)
    {
        end_op();
        return -1;
    }

    ilock(dp);

    // Cannot unlink "." or "..".
    if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
        goto bad;

    if ((ip = dirlookup(dp, name, &off)) == 0)
        goto bad;
    ilock(ip);

    if (ip->nlink < 1)
        panic("unlink: nlink < 1");
    if (ip->type == T_DIR && !isdirempty(ip))
    {
        iunlockput(ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("unlink: writei");
    if (ip->type == T_DIR)
    {
        dp->nlink--;
        iupdate(dp);
    }
    iunlockput(dp);

    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);

    end_op();

    return 0;

bad:
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

uint64 sys_mkdir(void)
{
    char path[MAXPATH];
    struct inode *ip;

    begin_op();
    if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0)
    {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

uint64 sys_mknod(void)
{
    struct inode *ip;
    char path[MAXPATH];
    int major, minor;

    begin_op();
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

uint64 sys_chdir(void)
{
    char path[MAXPATH];
    struct inode *ip;
    struct proc *p = myproc();

    begin_op();
    if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0)
    {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type != T_DIR)
    {
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    iput(p->cwd);
    end_op();
    p->cwd = ip;
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

// mmap系统调用：将文件或匿名内存映射到进程地址空间
// 参数：
//   addr: 用户期望的映射起始地址（0表示由内核分配）
//   len: 映射区域长度（字节）
//   prot: 内存保护权限（PROT_READ/PROT_WRITE/PROT_EXEC等组合）
//   flags: 映射标志（如MAP_SHARED/MAP_PRIVATE）
//   fd: 待映射的文件描述符（匿名映射时为-1）
//   offset: 文件偏移量（必须页对齐）
// 返回：成功返回映射起始地址，失败返回-1
uint64 sys_mmap(void)
{
    uint64 addr;                      // 用户指定的映射起始地址（或0）
    int len, prot, flags, fd, offset; // 映射长度、保护权限、标志、文件描述符、文件偏移
    struct file *file;                // 指向待映射文件的指针
    struct vma *vma = 0;              // 用于存储找到的空闲虚拟内存区域结构

    // 从用户空间获取系统调用参数：addr、len、prot、flags、fd、offset
    if (argaddr(0, &addr) < 0 || argint(1, &len) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0 ||
        argfd(4, &fd, &file) < 0 || argint(5, &offset) < 0)
        return -1;

    // 保护权限冲突检查：若为共享映射(MAP_SHARED)且文件不可写，但prot要求写权限，则错误
    if (!file->writable && (prot & PROT_WRITE) && flags == MAP_SHARED)
        return -1;

    struct proc *p = myproc();
    len = PGROUNDUP(len); // 将长度向上页对齐（确保映射区域是整页）

    // 检查地址空间是否足够：当前进程大小+映射长度不能超过最大用户虚拟地址(MAXVA)
    if (p->sz + len > MAXVA)
        return -1;

    // 检查文件偏移是否合法：偏移量不能为负，且必须页对齐
    if (offset < 0 || offset % PGSIZE)
        return -1;

    // 在进程的vmas数组中查找空闲的虚拟内存区域结构（NVMA为vma最大数量）
    for (int i = 0; i < NVMA; i++)
    {
        if (p->vmas[i].addr) // addr为0表示该vma项未使用
            continue;
        vma = &p->vmas[i]; // 找到空闲vma，记录其指针
        break;
    }

    if (!vma) // 若vmas数组已满（无空闲vma项），返回错误
        return -1;

    // 确定映射起始地址：若用户指定addr为0，则由内核分配（从进程当前大小p->sz开始）
    if (addr == 0)
        vma->addr = p->sz;
    else
        vma->addr = addr; // 否则使用用户指定的addr（需确保未被占用，此处简化处理）

    // 初始化vma结构的其他字段
    vma->len = len;       // 映射区域长度（页对齐后）
    vma->prot = prot;     // 保护权限（如读/写/执行）
    vma->flags = flags;   // 映射标志（如共享/私有）
    vma->fd = fd;         // 文件描述符（供后续解除映射时使用）
    vma->offset = offset; // 文件偏移量（从文件该位置开始映射）
    vma->file = file;     // 指向映射文件的指针（用于后续缺页时读取文件内容）
    filedup(file);        // 增加文件引用计数（确保文件在映射期间不被关闭）
    p->sz += len;         // 更新进程地址空间大小（扩展到映射区域末尾）
    return vma->addr;     // 返回映射起始地址
}

// munmap系统调用：解除进程地址空间中的内存映射
// 参数：
//   addr: 待解除映射的起始地址（必须页对齐）
//   len: 解除映射的长度（字节）
// 返回：成功返回0，失败返回-1
uint64 sys_munmap(void)
{
    uint64 addr;               // 解除映射的起始地址
    int len;                   // 解除映射的长度
    struct vma *vma = 0;       // 指向待解除映射对应的vma结构
    struct proc *p = myproc(); // 获取当前进程结构体

    // 从用户空间获取系统调用参数：addr和len
    if (argaddr(0, &addr) < 0 || argint(1, &len) < 0)
        return -1;

    // 地址和长度页对齐：确保操作以页为单位（内存管理的基本单位）
    addr = PGROUNDDOWN(addr); // 起始地址向下对齐到页边界
    len = PGROUNDUP(len);     // 长度向上对齐到页边界

    // 在进程vmas数组中查找与addr匹配的vma（即addr属于该vma的映射范围）
    for (int i = 0; i < NVMA; i++)
    {
        // 检查vma是否有效（addr非0），且addr在[vma.addr, vma.addr + vma.len)范围内
        if (p->vmas[i].addr && addr >= p->vmas[i].addr && addr + len <= p->vmas[i].addr + p->vmas[i].len)
        {
            vma = &p->vmas[i]; // 找到匹配的vma
            break;
        }
    }

    if (!vma) // 未找到对应的vma，返回错误
        return -1;

    // 检查解除映射的起始地址是否与vma的起始地址一致（简化处理：仅支持整段解除）
    if (addr != vma->addr)
        return -1;

    // 调整vma结构：移除已解除映射的部分（剩余区域的起始地址后移，长度减少）
    vma->addr += len; // 新的起始地址 = 原起始地址 + 解除的长度
    vma->len -= len;  // 新的长度 = 原长度 - 解除的长度

    // 若为共享映射(MAP_SHARED)，需将内存中的修改写回文件
    if (vma->flags & MAP_SHARED)
    {
        filewrite(vma->file, addr, len); // 将addr开始的len字节写回文件
    }

    // 解除页表映射：释放物理页（do_free=1表示释放物理内存）
    uvmunmap(p->pagetable, addr, len / PGSIZE, 1); // 页数 = len / 页大小(PGSIZE)
    return 0;                                      // 成功解除映射
}