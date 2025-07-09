// Shell - 一个简单的shell实现

#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "user/user.h"

// 解析后的命令表示
#define EXEC 1  // 可执行命令
#define REDIR 2 // 重定向命令
#define PIPE 3  // 管道命令
#define LIST 4  // 命令列表（顺序执行）
#define BACK 5  // 后台命令

#define MAXARGS 10 // 命令最大参数数量

// 基础命令结构
struct cmd
{
    int type; // 命令类型
};

// 可执行命令结构
struct execcmd
{
    int type;             // 命令类型（EXEC）
    char *argv[MAXARGS];  // 参数数组（以空指针结尾）
    char *eargv[MAXARGS]; // 参数结束位置指针
};

// 重定向命令结构
struct redircmd
{
    int type;        // 命令类型（REDIR）
    struct cmd *cmd; // 要重定向的子命令
    char *file;      // 文件名起始位置
    char *efile;     // 文件名结束位置
    int mode;        // 文件打开模式
    int fd;          // 文件描述符
};

// 管道命令结构
struct pipecmd
{
    int type;          // 命令类型（PIPE）
    struct cmd *left;  // 左侧命令
    struct cmd *right; // 右侧命令
};

// 命令列表结构（顺序执行）
struct listcmd
{
    int type;          // 命令类型（LIST）
    struct cmd *left;  // 左侧命令
    struct cmd *right; // 右侧命令
};

// 后台命令结构
struct backcmd
{
    int type;        // 命令类型（BACK）
    struct cmd *cmd; // 要在后台执行的命令
};

// 函数声明
int fork1(void);              // 封装fork，失败时panic
void panic(char *);           // 错误处理函数
struct cmd *parsecmd(char *); // 命令解析函数

// 执行命令（永不返回）
void runcmd(struct cmd *cmd)
{
    int p[2]; // 管道文件描述符
    struct backcmd *bcmd;
    struct execcmd *ecmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if (cmd == 0) // 空命令检查
        exit(1);

    switch (cmd->type)
    {
    default:
        panic("runcmd"); // 未知命令类型

    case EXEC: // 执行可执行命令
        ecmd = (struct execcmd *)cmd;
        if (ecmd->argv[0] == 0) // 无命令检查
            exit(1);
        exec(ecmd->argv[0], ecmd->argv);               // 执行程序
        fprintf(2, "exec %s failed\n", ecmd->argv[0]); // 执行失败提示
        break;

    case REDIR: // 处理重定向
        rcmd = (struct redircmd *)cmd;
        close(rcmd->fd); // 关闭原文件描述符
        // 打开文件并重定向
        if (open(rcmd->file, rcmd->mode) < 0)
        {
            fprintf(2, "open %s failed\n", rcmd->file); // 文件打开失败
            exit(1);
        }
        runcmd(rcmd->cmd); // 执行重定向后的命令
        break;

    case LIST: // 顺序执行命令列表
        lcmd = (struct listcmd *)cmd;
        if (fork1() == 0) // 创建子进程执行左侧命令
            runcmd(lcmd->left);
        wait(0);             // 等待左侧命令完成
        runcmd(lcmd->right); // 执行右侧命令
        break;

    case PIPE: // 处理管道命令
        pcmd = (struct pipecmd *)cmd;
        if (pipe(p) < 0) // 创建管道
            panic("pipe");
        // 左侧进程（写入端）
        if (fork1() == 0)
        {
            close(1);           // 关闭标准输出
            dup(p[1]);          // 复制管道写端
            close(p[0]);        // 关闭读端（未使用）
            close(p[1]);        // 关闭原始写端
            runcmd(pcmd->left); // 执行左侧命令
        }
        // 右侧进程（读取端）
        if (fork1() == 0)
        {
            close(0);            // 关闭标准输入
            dup(p[0]);           // 复制管道读端
            close(p[0]);         // 关闭原始读端
            close(p[1]);         // 关闭写端（未使用）
            runcmd(pcmd->right); // 执行右侧命令
        }
        // 父进程关闭管道并等待
        close(p[0]);
        close(p[1]);
        wait(0); // 等待左侧进程
        wait(0); // 等待右侧进程
        break;

    case BACK: // 后台执行命令
        bcmd = (struct backcmd *)cmd;
        if (fork1() == 0) // 创建子进程执行
            runcmd(bcmd->cmd);
        break;
    }
    exit(0);
}

// 获取用户输入命令
int getcmd(char *buf, int nbuf)
{
    fprintf(2, "$ "); // 显示提示符
    memset(buf, 0, nbuf);
    gets(buf, nbuf); // 读取用户输入
    if (buf[0] == 0) // 处理EOF（Ctrl+D）
        return -1;
    return 0;
}

int main(void)
{
    static char buf[100]; // 输入缓冲区
    int fd;

    // 确保至少有三个打开的文件描述符
    while ((fd = open("console", O_RDWR)) >= 0)
    {
        if (fd >= 3)
        { // 保留0,1,2（stdin, stdout, stderr）
            close(fd);
            break;
        }
    }

    // 读取并执行命令循环
    while (getcmd(buf, sizeof(buf)) >= 0)
    {
        // 处理内建cd命令（必须在父进程执行）
        if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ')
        {
            buf[strlen(buf) - 1] = 0;                  // 去除换行符
            if (chdir(buf + 3) < 0)                    // 切换目录
                fprintf(2, "cannot cd %s\n", buf + 3); // 错误提示
            continue;
        }
        // 创建子进程执行命令
        if (fork1() == 0)
            runcmd(parsecmd(buf));
        wait(0); // 等待子进程完成
    }
    exit(0);
}

// 错误处理函数（输出信息并退出）
void panic(char *s)
{
    fprintf(2, "%s\n", s);
    exit(1);
}

// 安全fork封装
int fork1(void)
{
    int pid;
    pid = fork();
    if (pid == -1)
        panic("fork");
    return pid;
}

// 命令结构构造函数
// 创建可执行命令
struct cmd *execcmd(void)
{
    struct execcmd *cmd;
    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = EXEC;
    return (struct cmd *)cmd;
}

// 创建重定向命令
struct cmd *redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
    struct redircmd *cmd;
    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = REDIR;
    cmd->cmd = subcmd;  // 重定向的子命令
    cmd->file = file;   // 文件名起始位置
    cmd->efile = efile; // 文件名结束位置
    cmd->mode = mode;   // 文件打开模式
    cmd->fd = fd;       // 要重定向的文件描述符
    return (struct cmd *)cmd;
}

// 创建管道命令
struct cmd *pipecmd(struct cmd *left, struct cmd *right)
{
    struct pipecmd *cmd;
    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = PIPE;
    cmd->left = left;   // 管道左侧命令
    cmd->right = right; // 管道右侧命令
    return (struct cmd *)cmd;
}

// 创建命令列表
struct cmd *listcmd(struct cmd *left, struct cmd *right)
{
    struct listcmd *cmd;
    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;   // 先执行的命令
    cmd->right = right; // 后执行的命令
    return (struct cmd *)cmd;
}

// 创建后台命令
struct cmd *backcmd(struct cmd *subcmd)
{
    struct backcmd *cmd;
    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = BACK;
    cmd->cmd = subcmd; // 后台执行的命令
    return (struct cmd *)cmd;
}

// 解析相关定义
char whitespace[] = " \t\r\n\v"; // 空白字符集
char symbols[] = "<|>&;()";      // 特殊符号集

// 获取下一个标记
int gettoken(char **ps, char *es, char **q, char **eq)
{
    char *s;
    int ret;
    s = *ps;
    // 跳过空白字符
    while (s < es && strchr(whitespace, *s))
        s++;
    if (q)
        *q = s; // 记录标记起始位置
    ret = *s;   // 返回标记类型

    // 处理不同标记类型
    switch (*s)
    {
    case 0: // 结束符
        break;
    case '|':
    case '(':
    case ')': // 单字符标记
    case ';':
    case '&':
    case '<':
        s++;
        break;
    case '>': // 输出重定向（可能为追加）
        s++;
        if (*s == '>')
        { // 检测追加模式
            ret = '+';
            s++;
        }
        break;
    default: // 普通字符串
        ret = 'a';
        while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
            s++;
        break;
    }
    if (eq)
        *eq = s; // 记录标记结束位置

    // 跳过尾部空白
    while (s < es && strchr(whitespace, *s))
        s++;
    *ps = s; // 更新解析位置
    return ret;
}

// 查看下一个字符（不消耗）
int peek(char **ps, char *es, char *toks)
{
    char *s;
    s = *ps;
    // 跳过空白
    while (s < es && strchr(whitespace, *s))
        s++;
    *ps = s;
    // 检查是否在目标字符集中
    return *s && strchr(toks, *s);
}

// 解析函数声明
struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

// 主解析函数
struct cmd *parsecmd(char *s)
{
    char *es;
    struct cmd *cmd;
    es = s + strlen(s);      // 输入结束位置
    cmd = parseline(&s, es); // 开始解析
    // 检查是否完全解析
    peek(&s, es, "");
    if (s != es)
    {
        fprintf(2, "leftovers: %s\n", s);
        panic("syntax"); // 存在未解析内容
    }
    nulterminate(cmd); // 终止字符串
    return cmd;
}

// 解析行（处理后台和列表）
struct cmd *parseline(char **ps, char *es)
{
    struct cmd *cmd;
    cmd = parsepipe(ps, es); // 先解析管道
    // 处理后台符号'&'
    while (peek(ps, es, "&"))
    {
        gettoken(ps, es, 0, 0); // 消耗'&'
        cmd = backcmd(cmd);     // 包装为后台命令
    }
    // 处理命令分隔符';'
    if (peek(ps, es, ";"))
    {
        gettoken(ps, es, 0, 0); // 消耗';'
        // 递归解析后续命令
        cmd = listcmd(cmd, parseline(ps, es));
    }
    return cmd;
}

// 解析管道（处理'|'）
struct cmd *parsepipe(char **ps, char *es)
{
    struct cmd *cmd;
    cmd = parseexec(ps, es); // 解析基础命令
    // 处理管道符号'|'
    if (peek(ps, es, "|"))
    {
        gettoken(ps, es, 0, 0); // 消耗'|'
        // 递归解析右侧命令
        cmd = pipecmd(cmd, parsepipe(ps, es));
    }
    return cmd;
}

// 解析重定向
struct cmd *parseredirs(struct cmd *cmd, char **ps, char *es)
{
    int tok;
    char *q, *eq;
    // 循环处理连续重定向
    while (peek(ps, es, "<>"))
    {
        tok = gettoken(ps, es, 0, 0); // 获取重定向符号
        // 获取文件名
        if (gettoken(ps, es, &q, &eq) != 'a')
            panic("missing file for redirection");
        // 根据符号类型创建重定向命令
        switch (tok)
        {
        case '<': // 输入重定向
            cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
            break;
        case '>': // 输出重定向（覆盖）
            cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
            break;
        case '+': // 输出重定向（追加）
            cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
            break;
        }
    }
    return cmd;
}

// 解析括号块
struct cmd *parseblock(char **ps, char *es)
{
    struct cmd *cmd;
    if (!peek(ps, es, "(")) // 起始括号检查
        panic("parseblock");
    gettoken(ps, es, 0, 0);  // 消耗'('
    cmd = parseline(ps, es); // 解析括号内命令
    if (!peek(ps, es, ")"))  // 结束括号检查
        panic("syntax - missing )");
    gettoken(ps, es, 0, 0);         // 消耗')'
    cmd = parseredirs(cmd, ps, es); // 解析重定向
    return cmd;
}

// 解析可执行命令
struct cmd *parseexec(char **ps, char *es)
{
    char *q, *eq;
    int tok, argc;
    struct execcmd *cmd;
    struct cmd *ret;

    // 检查是否括号块
    if (peek(ps, es, "("))
        return parseblock(ps, es);

    // 创建基础可执行命令
    ret = execcmd();
    cmd = (struct execcmd *)ret;

    argc = 0;
    ret = parseredirs(ret, ps, es); // 解析前置重定向
    // 循环解析参数
    while (!peek(ps, es, "|)&;"))
    {
        if ((tok = gettoken(ps, es, &q, &eq)) == 0) // 获取标记
            break;
        if (tok != 'a') // 非普通字符串错误
            panic("syntax");
        // 存储参数
        cmd->argv[argc] = q;
        cmd->eargv[argc] = eq;
        argc++;
        if (argc >= MAXARGS) // 参数过多检查
            panic("too many args");
        ret = parseredirs(ret, ps, es); // 解析重定向
    }
    // 终止参数列表
    cmd->argv[argc] = 0;
    cmd->eargv[argc] = 0;
    return ret;
}

// 终止所有字符串（添加NUL）
struct cmd *nulterminate(struct cmd *cmd)
{
    int i;
    struct backcmd *bcmd;
    struct execcmd *ecmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if (cmd == 0)
        return 0;

    // 根据命令类型处理
    switch (cmd->type)
    {
    case EXEC: // 可执行命令：终止每个参数字符串
        ecmd = (struct execcmd *)cmd;
        for (i = 0; ecmd->argv[i]; i++)
            *ecmd->eargv[i] = 0;
        break;

    case REDIR: // 重定向命令：终止文件名并递归处理子命令
        rcmd = (struct redircmd *)cmd;
        nulterminate(rcmd->cmd);
        *rcmd->efile = 0;
        break;

    case PIPE: // 管道命令：递归处理两侧命令
        pcmd = (struct pipecmd *)cmd;
        nulterminate(pcmd->left);
        nulterminate(pcmd->right);
        break;

    case LIST: // 命令列表：递归处理两侧命令
        lcmd = (struct listcmd *)cmd;
        nulterminate(lcmd->left);
        nulterminate(lcmd->right);
        break;

    case BACK: // 后台命令：递归处理子命令
        bcmd = (struct backcmd *)cmd;
        nulterminate(bcmd->cmd);
        break;
    }
    return cmd;
}