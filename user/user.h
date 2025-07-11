// 文件系统元数据结构（定义在 stat.h 中）
struct stat;
// 实时时钟日期结构（定义在 date.h 中）
struct rtcdate;

// 系统调用声明
// ==============
int fork(void);                          // 创建新进程，返回子进程PID（父进程）或0（子进程）
int exit(int) __attribute__((noreturn)); // 终止当前进程，状态码传递给 wait()
int wait(int *);                         // 等待子进程退出，并获取其退出状态
int pipe(int *);                         // 创建管道，fd[0]读端，fd[1]写端
int write(int, const void *, int);       // 向文件描述符写入数据
int read(int, void *, int);              // 从文件描述符读取数据
int close(int);                          // 关闭文件描述符
int kill(int);                           // 杀死指定PID的进程
int exec(char *, char **);               // 加载并执行新程序（替换当前镜像）
int open(const char *, int);             // 打开文件，返回文件描述符
int mknod(const char *, short, short);   // 创建设备文件
int unlink(const char *);                // 删除文件（目录项）
int fstat(int fd, struct stat *);        // 获取文件状态信息
int link(const char *, const char *);    // 创建硬链接
int mkdir(const char *);                 // 创建目录
int chdir(const char *);                 // 改变当前工作目录
int dup(int);                            // 复制文件描述符
int getpid(void);                        // 获取当前进程PID
char *sbrk(int);                         // 调整进程内存大小（返回堆顶地址）
int sleep(int);                          // 休眠指定时钟滴答数
int uptime(void);                        // 获取系统启动后的时钟滴答数

// 用户库函数声明 (ulib.c)
// ======================
int stat(const char *, struct stat *);        // 获取文件状态（通过路径）
char *strcpy(char *, const char *);           // 复制字符串
void *memmove(void *, const void *, int);     // 安全内存移动（处理重叠区域）
char *strchr(const char *, char c);           // 查找字符在字符串中的首次出现位置
int strcmp(const char *, const char *);       // 字符串比较
void fprintf(int, const char *, ...);         // 格式化输出到文件描述符
void printf(const char *, ...);               // 格式化输出到标准输出
char *gets(char *, int max);                  // 从标准输入读取一行（带缓冲）
uint strlen(const char *);                    // 获取字符串长度
void *memset(void *, int, uint);              // 内存块填充
void *malloc(uint);                           // 动态内存分配
void free(void *);                            // 释放动态内存
int atoi(const char *);                       // 字符串转整数
int memcmp(const void *, const void *, uint); // 内存块比较
void *memcpy(void *, const void *, uint);     // 内存块复制（不处理重叠）