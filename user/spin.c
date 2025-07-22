#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int pid = fork();
    if(pid > 0)
    {
        // 父进程
        for(int i = 0; i<2000;i++)
        {
            printf("\\");
        }
    }
    else
    {
        // 子进程
        for(int i = 0; i<2000;i++)
        {
            printf("/");
        }
    }
    exit(0);
}