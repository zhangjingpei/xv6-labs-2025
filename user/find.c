#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

/*
*参考ls.c

*/
void find(char *path, const char *filename)
{
    char buf[512], *p;
    int fd;
    struct dirent de; // 存放目录项
    
    struct stat st;   // 存放文件状态

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
    if (st.type != T_DIR)
    {
        fprintf(2, "usage: find<DIRCTORY> <filename>\n");
        close(fd);
        return;
    }

    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
    {
        fprintf(2, "find: path too long\n");
        close(fd);
        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while (read(fd, &de, sizeof de) == sizeof(de))
    {
        if (de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ); // 增加路径名称
        p[DIRSIZ] = 0;               // 字符串结束标志
        if (stat(buf, &st) < 0)
        {
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }
        // 不要再"."和".."目录中递归
        if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0)
        {
            find(buf, filename);
        }
        else if (strcmp(filename, p) == 0)
        {
            printf("%s\n", buf);
        }
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
    find(argv[1], argv[2]);
    exit(0);
}