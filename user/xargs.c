#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAXSZ 512

void clearArgv(char *x_argv[MAXARG], int beg)
{
    for (int i = beg; i < MAXARG; ++i)
        x_argv[i] = 0;
}

int readline(char *buf, int max)
{
    int i = 0;
    char c;
    while (i < max - 1)
    {
        int n = read(0, &c, 1);
        if (n <= 0)
            break; // EOF or error
        if (c == '\\')
        {
            // 处理转义序列
            n = read(0, &c, 1);
            if (n <= 0)
                break;
            if (c == 'n')
                c = '\n'; // 将 \n 转换为换行符
            // 其他转义序列可在此扩展
        }
        if (c == '\n')
            break; // 遇到换行符结束行
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

// echo 1\n2 |xargs echo line
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(2, "xargs: missing command\n");
        exit(1);
    }
    if (argc - 1 >= MAXARG)
    {
        fprintf(2, "xargs: too many arguments.\n");
        exit(1);
    }
    char buf[MAXSZ];
    char *x_argv[MAXARG];

    // 复制原始参数 (跳过 "xargs")
    for (int i = 1; i < argc; ++i)
    {
        x_argv[i - 1] = argv[i];
        printf("xargv[%d]=%s\n", i - 1, x_argv[i - 1]);
    }
    int base_argc = argc - 1;

    while (1)
    {
        int n = readline(buf, MAXSZ);
        printf("buf=%s\n", buf);
        if (n == 0)
            break; // EOF

        if (buf[0] == '\0')
            continue; // 跳过空行

        // 添加整行作为单个参数
        if (base_argc >= MAXARG - 1)
        {
            fprintf(2, "xargs: too many arguments.\n");
            exit(1);
        }
        x_argv[base_argc] = buf;
        x_argv[base_argc + 1] = 0; // 终止参数数组

        if (fork() == 0)
        {
            exec(x_argv[0], x_argv);
            fprintf(2, "xargs: exec %s failed\n", x_argv[0]);
            exit(1);
        }
        wait(0);
        clearArgv(x_argv, base_argc);
    }
    exit(0);
}