// Simple grep. Only supports ^ . * $ operators.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

char buf[1024];            // 全局缓冲区，用于存储从文件读取的数据
int match(char *, char *); // 正则匹配函数声明

// grep核心函数：在文件描述符fd中搜索pattern
void grep(char *pattern, int fd)
{
    int n, m;    // n: 单次读取字节数, m: 缓冲区当前有效数据长度
    char *p, *q; // 指针：p-缓冲区起始位置, q-行尾位置

    m = 0; // 初始化有效数据长度
    while ((n = read(fd, buf + m, sizeof(buf) - m - 1)) > 0)
    {
        m += n;        // 更新有效数据长度
        buf[m] = '\0'; // 确保缓冲区以NULL结尾（用于字符串处理）
        p = buf;       // 从缓冲区起始开始处理

        // 逐行处理缓冲区内容
        while ((q = strchr(p, '\n')) != 0)
        {
            *q = 0; // 临时替换换行为NULL（将行内容隔离为独立字符串）

            // 检查该行是否匹配模式
            if (match(pattern, p))
            {
                *q = '\n';              // 恢复换行符
                write(1, p, q + 1 - p); // 输出整行（包含换行符）
            }
            p = q + 1; // 移动到下一行起始位置
        }

        // 处理剩余未完成的行（跨缓冲区边界）
        if (m > 0)
        {
            m -= p - buf;       // 计算剩余数据长度
            memmove(buf, p, m); // 将剩余数据移到缓冲区开头
        }
    }
}

int main(int argc, char *argv[])
{
    int fd, i;
    char *pattern;

    if (argc <= 1)
    {
        fprintf(2, "usage: grep pattern [file ...]\n");
        exit(1);
    }
    pattern = argv[1]; // 获取搜索模式

    // 无文件名参数：从标准输入读取
    if (argc <= 2)
    {
        grep(pattern, 0); // 0 表示标准输入
        exit(0);
    }

    // 处理多个文件
    for (i = 2; i < argc; i++)
    {
        if ((fd = open(argv[i], 0)) < 0)
        { // 只读模式打开文件
            printf("grep: cannot open %s\n", argv[i]);
            exit(1);
        }
        grep(pattern, fd);
        close(fd);
    }
    exit(0);
}

/****** 正则引擎（Kernighan & Pike实现）******/

int matchhere(char *, char *);
int matchstar(int, char *, char *);

// 主匹配函数：判断文本是否匹配正则表达式
int match(char *re, char *text)
{
    if (re[0] == '^')
        return matchhere(re + 1, text); // ^ 要求从开头匹配

    // 非^模式：尝试文本的每个起始位置
    do
    {
        if (matchhere(re, text))
            return 1;
    } while (*text++ != '\0'); // 即使空字符串也检查
    return 0;
}

// 检查正则是否在文本开头匹配
int matchhere(char *re, char *text)
{
    if (re[0] == '\0')
        return 1; // 正则消耗完毕->匹配成功

    if (re[1] == '*')
        return matchstar(re[0], re + 2, text); // 处理*通配符

    if (re[0] == '$' && re[1] == '\0')
        return *text == '\0'; // $ 要求文本结束

    // 常规字符或.通配符匹配
    if (*text != '\0' && (re[0] == '.' || re[0] == *text))
        return matchhere(re + 1, text + 1); // 递归匹配剩余部分

    return 0;
}

// 匹配 c* 模式（c为字符，*表示0次或多次重复）
int matchstar(int c, char *re, char *text)
{
    do
    {
        // 重要：优先尝试0次匹配（即使c匹配也可能跳过）
        if (matchhere(re, text))
            return 1;
        // 无法匹配时：消耗一个c字符（或.匹配任意字符）
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}