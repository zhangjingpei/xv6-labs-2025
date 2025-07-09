#include "kernel/types.h" // 基本类型定义（如uint、ushort等）
#include "kernel/stat.h"  // 文件状态结构体stat的定义
#include "user/user.h"    // 用户级系统调用接口（如open、read等）
#include "kernel/fs.h"    // 文件系统常量（如DIRSIZ、T_FILE、T_DIR等）

// 全局缓冲区大小常量（用于路径拼接）
#define BUF_SIZE 512

/**
 * 格式化文件名：从路径中提取文件名并填充至固定长度(DIRSIZ=14)
 * 参数：
 *   path - 路径字符串（如"/usr/include/stdio.h"）
 * 返回值：
 *   指向静态缓冲区的指针，包含格式化后的文件名（如"stdio.h"）
 */
char *fmtname(char *path)
{
    static char buf[DIRSIZ + 1]; // 静态缓冲区存储格式化后的文件名
    char *p;

    // 从路径末尾向前查找最后一个'/'的位置
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ; // 循环结束后，p指向最后一个'/'前的字符
    p++;  // p指向文件名起始位置

    // 如果文件名长度超过DIRSIZ，直接返回原名；否则填充空格至DIRSIZ长度
    if (strlen(p) >= DIRSIZ)
        return p;

    // 拷贝文件名到buf并填充空格
    memmove(buf, p, strlen(p));                       // 拷贝文件名到缓冲区
    memset(buf + strlen(p), ' ', DIRSIZ - strlen(p)); // 用空格填充剩余部分
    return buf;
}

/**
 * 列出目录内容的核心函数
 * 参数：
 *   path - 要列出的路径（文件或目录）
 */
void ls(char *path)
{
    char buf[BUF_SIZE], *p; // 用于存储完整路径的缓冲区
    int fd;                 // 文件描述符
    struct dirent de;       // 目录项结构体（包含inum和name）
    struct stat st;         // 文件状态结构体（包含类型、inode号、大小等）

    // 打开目标路径
    if ((fd = open(path, 0)) < 0)
    { // 0表示只读模式
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    // 获取文件状态信息
    if (fstat(fd, &st) < 0)
    { // 通过文件描述符获取状态
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    // 处理文件类型
    switch (st.type)
    {
    case T_FILE: // 如果是普通文件，直接输出文件名和信息
        printf("%s %d %d %d\n", fmtname(path), st.type, st.ino, st.size);
        break;

    case T_DIR: // 如果是目录，遍历目录项
        // 检查路径长度是否超出缓冲区限制
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            printf("ls: path too long\n");
            break;
        }

        // 构造完整路径：path + '/' + filename
        strcpy(buf, path);     // 将路径拷贝到缓冲区
        p = buf + strlen(buf); // p指向缓冲区末尾
        *p++ = '/';            // 添加斜杠分隔符

        // 读取目录项（每次读取一个dirent结构体）
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0)
                continue; // 跳过空目录项

            // 将文件名拷贝到路径缓冲区
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0; // 确保字符串结尾

            // 获取文件状态并输出
            if (stat(buf, &st) < 0)
            { // 通过路径获取状态
                printf("ls: cannot stat %s\n", buf);
                continue;
            }
            printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
        }
        break;
    }
    close(fd); // 关闭文件描述符
}

/**
 * 程序入口点
 * 参数：
 *   argc - 命令行参数数量
 *   argv - 命令行参数数组（路径列表）
 */
int main(int argc, char *argv[])
{
    int i;

    // 如果没有参数，默认列出当前目录内容
    if (argc < 2)
    {
        ls(".");
        exit(0);
    }

    // 遍历所有参数，调用ls处理每个路径
    for (i = 1; i < argc; i++)
        ls(argv[i]);
    exit(0);
}