/*
 * defs.h
 * 
 * 本文件为 xv6 操作系统内核的函数声明头文件，声明了各个内核模块的主要接口函数和部分全局变量。
 * 这些声明允许内核各模块之间进行函数调用和数据访问。
 * 
 * 主要功能包括：
 *   - 块设备缓存（bio.c）
 *   - 控制台输入输出（console.c）
 *   - 程序加载与执行（exec.c）
 *   - 文件管理（file.c）
 *   - 文件系统操作（fs.c）
 *   - 内存分配（kalloc.c）
 *   - 日志管理（log.c）
 *   - 管道通信（pipe.c）
 *   - 格式化输出与内核异常处理（printf.c）
 *   - 进程管理与调度（proc.c）
 *   - 上下文切换（swtch.S）
 *   - 自旋锁与睡眠锁（spinlock.c, sleeplock.c）
 *   - 字符串与内存操作（string.c）
 *   - 系统调用参数获取（syscall.c）
 *   - 中断与异常处理（trap.c）
 *   - UART 串口驱动（uart.c）
 *   - 虚拟内存管理（vm.c）
 *   - 外部中断控制器（plic.c）
 *   - 虚拟磁盘驱动（virtio_disk.c）
 *   - 常用宏定义
 *
 * 注意：本文件仅包含声明，具体实现见对应的 .c 文件。
 */

struct buf;           // 块缓存结构体
struct context;       // 上下文切换结构体
struct file;          // 文件结构体
struct inode;         // 索引节点结构体
struct pipe;          // 管道结构体
struct proc;          // 进程结构体
struct spinlock;      // 自旋锁结构体
struct sleeplock;     // 睡眠锁结构体
struct stat;          // 文件状态结构体
struct superblock;    // 超级块结构体

// bio.c
void            binit(void);                        // 初始化块缓存
struct buf*     bread(uint, uint);                  // 读取指定块
void            brelse(struct buf*);                // 释放块缓存
void            bwrite(struct buf*);                // 写回块缓存
void            bpin(struct buf*);                  // 固定块缓存
void            bunpin(struct buf*);                // 解除固定块缓存

// console.c
void            consoleinit(void);                  // 初始化控制台
void            consoleintr(int);                   // 控制台中断处理
void            consputc(int);                      // 输出一个字符到控制台

// exec.c
int             exec(char*, char**);                // 执行新程序

// file.c
struct file*    filealloc(void);                    // 分配文件结构体
void            fileclose(struct file*);            // 关闭文件
struct file*    filedup(struct file*);              // 文件引用计数加一
void            fileinit(void);                     // 初始化文件表
int             fileread(struct file*, uint64, int n); // 读取文件
int             filestat(struct file*, uint64 addr);   // 获取文件状态
int             filewrite(struct file*, uint64, int n); // 写文件

// fs.c
void            fsinit(int);                        // 初始化文件系统
int             dirlink(struct inode*, char*, uint); // 目录添加链接
struct inode*   dirlookup(struct inode*, char*, uint*); // 目录查找
struct inode*   ialloc(uint, short);                // 分配 inode
struct inode*   idup(struct inode*);                // inode 引用计数加一
void            iinit();                            // 初始化 inode 表
void            ilock(struct inode*);               // 加锁 inode
void            iput(struct inode*);                // 释放 inode
void            iunlock(struct inode*);             // 解锁 inode
void            iunlockput(struct inode*);          // 解锁并释放 inode
void            iupdate(struct inode*);             // 更新 inode 到磁盘
int             namecmp(const char*, const char*);  // 比较文件名
struct inode*   namei(char*);                       // 路径查找 inode
struct inode*   nameiparent(char*, char*);          // 查找父目录 inode
int             readi(struct inode*, int, uint64, uint, uint); // 读取 inode
void            stati(struct inode*, struct stat*); // 获取 inode 状态
int             writei(struct inode*, int, uint64, uint, uint); // 写 inode
void            itrunc(struct inode*);              // 截断 inode

// ramdisk.c
void            ramdiskinit(void);                  // 初始化内存盘
void            ramdiskintr(void);                  // 内存盘中断处理
void            ramdiskrw(struct buf*);             // 内存盘读写

// kalloc.c
void*           kalloc(void);                       // 分配物理页
void            kfree(void *);                      // 释放物理页
void            kinit(void);                        // 初始化物理内存分配器
uint64          count_free_mem(void);               //获取空闲内存

// log.c
void            initlog(int, struct superblock*);   // 初始化日志
void            log_write(struct buf*);             // 写日志
void            begin_op(void);                     // 日志操作开始
void            end_op(void);                       // 日志操作结束

// pipe.c
int             pipealloc(struct file**, struct file**); // 分配管道
void            pipeclose(struct pipe*, int);       // 关闭管道
int             piperead(struct pipe*, uint64, int); // 管道读
int             pipewrite(struct pipe*, uint64, int); // 管道写

// printf.c
void            printf(char*, ...);                 // 格式化输出
void            panic(char*) __attribute__((noreturn)); // 内核崩溃
void            printfinit(void);                   // 初始化 printf

// proc.c
int             cpuid(void);                        // 获取当前 CPU id
void            exit(int);                          // 进程退出
int             fork(void);                         // 创建子进程
int             growproc(int);                      // 增加/减少进程内存
pagetable_t     proc_pagetable(struct proc *);      // 获取进程页表
void            proc_freepagetable(pagetable_t, uint64); // 释放进程页表
int             kill(int);                          // 杀死进程
struct cpu*     mycpu(void);                        // 获取当前 CPU 结构体
struct cpu*     getmycpu(void);                     // 获取当前 CPU 结构体（别名）
struct proc*    myproc();                           // 获取当前进程
void            procinit(void);                     // 初始化进程表
void            scheduler(void) __attribute__((noreturn)); // 调度器主循环
void            sched(void);                        // 进程调度
void            setproc(struct proc*);              // 设置当前进程
void            sleep(void*, struct spinlock*);     // 进程睡眠
void            userinit(void);                     // 初始化第一个用户进程
int             wait(uint64);                       // 等待子进程退出
void            wakeup(void*);                      // 唤醒睡眠进程
void            yield(void);                        // 让出 CPU
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len); // 拷贝到用户/内核空间
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);  // 从用户/内核空间拷贝
void            procdump(void);                     // 打印进程信息
uint64          count_process(void);                // 统计空闲进程数

// swtch.S
void            swtch(struct context*, struct context*); // 上下文切换

// spinlock.c
void            acquire(struct spinlock*);          // 获取自旋锁
int             holding(struct spinlock*);          // 判断是否持有自旋锁
void            initlock(struct spinlock*, char*);  // 初始化自旋锁
void            release(struct spinlock*);          // 释放自旋锁
void            push_off(void);                     // 关闭中断
void            pop_off(void);                      // 恢复中断

// sleeplock.c
void            acquiresleep(struct sleeplock*);    // 获取睡眠锁
void            releasesleep(struct sleeplock*);    // 释放睡眠锁
int             holdingsleep(struct sleeplock*);    // 判断是否持有睡眠锁
void            initsleeplock(struct sleeplock*, char*); // 初始化睡眠锁

// string.c
int             memcmp(const void*, const void*, uint); // 内存比较
void*           memmove(void*, const void*, uint);      // 内存移动
void*           memset(void*, int, uint);               // 内存设置
char*           safestrcpy(char*, const char*, int);    // 安全字符串拷贝
int             strlen(const char*);                    // 字符串长度
int             strncmp(const char*, const char*, uint);// 字符串比较
char*           strncpy(char*, const char*, int);       // 字符串拷贝

// syscall.c
int             argint(int, int*);                     // 获取系统调用 int 参数
int             argstr(int, char*, int);               // 获取系统调用字符串参数
int             argaddr(int, uint64 *);                // 获取系统调用地址参数
int             fetchstr(uint64, char*, int);          // 从用户空间获取字符串
int             fetchaddr(uint64, uint64*);            // 从用户空间获取地址
void            syscall();                             // 系统调用分发

// trap.c
extern uint     ticks;                                 // 时钟中断计数
void            trapinit(void);                        // 初始化中断
void            trapinithart(void);                    // 初始化当前核中断
extern struct spinlock tickslock;                      // 时钟锁
void            usertrapret(void);                     // 用户态中断返回

// uart.c
void            uartinit(void);                        // 初始化 UART
void            uartintr(void);                        // UART 中断处理
void            uartputc(int);                         // 输出字符到 UART
void            uartputc_sync(int);                    // 同步输出字符到 UART
int             uartgetc(void);                        // 从 UART 读取字符

// vm.c
void            kvminit(void);                         // 初始化内核页表
void            kvminithart(void);                     // 初始化当前核页表
uint64          kvmpa(uint64);                         // 虚拟地址转物理地址
void            kvmmap(uint64, uint64, uint64, int);   // 内核页表映射
int             mappages(pagetable_t, uint64, uint64, uint64, int); // 建立页表映射
pagetable_t     uvmcreate(void);                       // 创建用户页表
void            uvminit(pagetable_t, uchar *, uint);   // 初始化用户页表
uint64          uvmalloc(pagetable_t, uint64, uint64); // 用户内存分配
uint64          uvmdealloc(pagetable_t, uint64, uint64);// 用户内存释放
int             uvmcopy(pagetable_t, pagetable_t, uint64); // 拷贝用户页表
void            uvmfree(pagetable_t, uint64);          // 释放用户页表
void            uvmunmap(pagetable_t, uint64, uint64, int); // 解除页表映射
void            uvmclear(pagetable_t, uint64);         // 清除用户页表权限
uint64          walkaddr(pagetable_t, uint64);         // 获取虚拟地址对应物理地址
int             copyout(pagetable_t, uint64, char *, uint64); // 拷贝到用户空间
int             copyin(pagetable_t, char *, uint64, uint64);  // 从用户空间拷贝
int             copyinstr(pagetable_t, char *, uint64, uint64);// 从用户空间拷贝字符串

// plic.c
void            plicinit(void);                        // 初始化外部中断控制器
void            plicinithart(void);                    // 初始化当前核外部中断
int             plic_claim(void);                      // 声明中断
void            plic_complete(int);                    // 完成中断

// virtio_disk.c
void            virtio_disk_init(void);                // 初始化虚拟磁盘
void            virtio_disk_rw(struct buf *, int);     // 虚拟磁盘读写
void            virtio_disk_intr(void);                // 虚拟磁盘中断处理

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))           // 计算数组元素个数