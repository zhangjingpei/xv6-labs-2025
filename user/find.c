#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

/*
*参考ls.c  grep.c

*/

int matchhere(char *, char *);
int matchstar(int, char *, char *);

int match(char *re, char *text)
{
    if (re[0] == '^')
    {
        return matchhere(re + 1, text);
    }
    else
    {
        do
        {
            if (matchhere(re, text))
                return 1;
        } while (*text++ != '\0');
    }
    return 0;
}

int matchhere(char *re, char *text)
{
    if (re[0] == '\0')
        return 1;
    if (re[1] == '*')
        return matchstar(re[0], re + 2, text);
    if (re[0] == '$' && re[1] == '\0')
        return *text == '\0';
    if (*text != '\0' && (re[0] == '.' || re[0] == *text))
        return matchhere(re + 1, text + 1);
    return 0;
}

int matchstar(int c, char *re, char *text)
{
    do
    { // a * matches zero or more instances
        if (matchhere(re, text))
            return 1;
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}

void find(char *path, char *filename)
{
    char buf[512], *p; // p指向/use/zhang/clearning 中的clearning中的g
    int fd;
    struct dirent de; // 存放目录项

    struct stat st; // 存放文件状态

    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot fstat %s\n", path);
        close(fd);
        return;
    }

    // 参数错误，find的第一个参数必须是目录
    switch (st.type)
    {
    case T_FILE:
        // 如果文件名结尾匹配'/target' 则视为匹配
        // if (strcmp(path + strlen(path) - strlen(filename), filename) == 0)
        //     printf("%s\n", path);
        if (match(filename, path))
        {
            printf("%s\n", path);
        }
        break;
    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf))
        {
            printf("find: path too long\n");
            break;
        }
        // printf("switch\n");
        strcpy(buf, path); // 将path拷贝到buf中
        p = buf + strlen(buf);
        // printf("*p:%s\n", *(p));
        *p = '/';
        p++;
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0)
                continue;
            // printf("de.name=%s\n", de.name);
            memmove(p, de.name, DIRSIZ); // 不会使de.name置为空
            // printf("p=%s, len(p)=%d\n", p, strlen(p));
            p[DIRSIZ] = 0; // 换行符

            // stat 的核心功能是 通过文件路径或文件描述符 ，获取文件的详细属性信息，
            // 并将这些信息存储在一个 struct stat结构体中。
            if (stat(buf, &st) < 0)
            {
                fprintf(2, "find: cannot stat%s\n", buf);
                continue;
            }
            // printf("buf=%s, len(p)=%d\n", buf, strlen(buf));
            //  不要进入'.'和‘..’ 容易造成死循环
            if (strcmp(buf + strlen(buf) - 2, "/.") != 0 && strcmp(buf + strlen(buf) - 3, "/..") != 0)
            {
                // 递归查找
                // 比如ls 下有个b文件夹，buf = ls/b 这种查找
                find(buf, filename);
            }
        }

    default:
        break;
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(2, "usage: find <directory> >filename>\n");
        exit(1);
    }
    char target[512];
    target[0] = '/';
    strcpy(target + 1, argv[2]);
    find(argv[1], target);
    exit(0);
}