#include "kernel/types.h"
#include "user/user.h"

/*
编写一个使用UNIX系统调用的程序来在两个进程之间“ping-pong”一个字节，请使用两个管道，每个方向一个。父进程应该向子进程发送一个字节;
子进程应该打印“<pid>:receivedping”，

其中<pid>是进程ID，并在管道中写入字节发送给父进程，然后退出;父级应该从读取从子进程而来的字节，打印
“<pid>:received pong”，
然后退出。您的解决方案应该在文件user/pingpong.c中。
*/

#define RD 0 // pipe的read端
#define WR 1 // pipe的write端

int main(int argc, char const *argv[])
{
    char buf = 'P'; // 用于传送的字节

    int fd_c2p[2]; // children->parent
    int fd_p2c[2]; // parent->children
    pipe(fd_c2p);
    pipe(fd_p2c);

    int pid = fork();
    int exit_status = 0;

    if (pid < 0)
    {
        fprintf(2, "fork() error!\n");
        close(fd_c2p[0]);
        close(fd_c2p[1]);
        close(fd_p2c[0]);
        close(fd_p2c[1]);
        exit(1);
    }
    else if (pid == 0)
    {
        close(fd_p2c[1]); // parent->children write端
        close(fd_c2p[0]); // child->parent Read端

        if (read(fd_p2c[RD], &buf, sizeof(char)) != sizeof(char))
        {
            fprintf(2, "child read() error!\n");
            exit_status = 1; // 标记出错
        }
        else
        {
            fprintf(1, "%d: received ping\n", getpid());
        }
        if (write(fd_c2p[WR], &buf, sizeof(char)) != sizeof(char))
        {
            fprintf(2, "child write() errpr!\n");
            exit_status = 1;
        }
        close(fd_p2c[0]);
        close(fd_c2p[1]);
        exit(exit_status);
    }
    else
    {
        close(fd_c2p[1]);
        close(fd_p2c[0]);
        if (write(fd_p2c[WR], &buf, sizeof(char)) != sizeof(char))
        {
            fprintf(2, "parent write() error!\n");
            exit_status = 1;
        }

        if (read(fd_c2p[RD], &buf, sizeof(char)) != sizeof(char))
        {
            fprintf(2, "parent read() error!\n");
            exit_status = 1;
        }
        else
        {
            fprintf(1, "%d: received pong\n", getpid());
        }
        close(fd_c2p[0]);
        close(fd_p2c[1]);
        exit(exit_status);
    }
}