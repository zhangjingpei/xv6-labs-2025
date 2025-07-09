/*
您的目标是使用pipe和fork来设置管道。第一个进程将数字2到35输入管道。
对于每个素数，您将安排创建一个进程，该进程通过一个管道从其左邻居读取数据，
并通过另一个管道向其右邻居写入数据。
由于xv6的文件描述符和进程数量有限，因此第一个进程可以在35处停止。


主进程
  |
  | (写入2-35)
  v
进程A (素数2) --[管道p1: 3,5,7,...35]-->
                    进程B (素数3) --[管道p2: 5,7,11,...35]-->
                          进程C (素数5) --[管道p3: 7,11,13,...]-->
                                ...
                                    进程N (素数31) --[空管道]--> 终止
*/

#include "kernel/types.h"
#include "user/user.h"

#define RD 0
#define WR 1

const uint INT_LEN = sizeof(int);

/*
    读取左邻居的第一个数据
    lpipe左邻居的管道符
    pfirst用于存储第一个数据的地址
    return 如果没有数据 返回-1 有数据返回0
*/
int lpipe_first_data(int lpipe[2], int *dst)
{
    if (read(lpipe[RD], dst, sizeof(int)) == sizeof(int))
    {
        printf("prime %d\n", *dst);
        return 0;
    }
    return -1;
}

/*
    读取左邻居的数据，将不能被first整除的数据写入右邻居
    lpipe左邻居的管道符
    rpipe右邻居的管道符
    first左邻居的第一个数据
*/
void transmit_data(int lpipe[2], int rpipe[2], int first)
{
    int data;
    // 从左管道读取数据
    while (read(lpipe[RD], &data, sizeof(int)) == sizeof(int))
    {
        // 将无法整除的数据传入右管道
        if (data % first)
        {
            write(rpipe[WR], &data, sizeof(int));
        }
    }
    close(lpipe[RD]);
    close(rpipe[WR]);
}

// 寻找素数
void primes(int lpipe[2])
{
    close(lpipe[WR]);
    int first;
    if (lpipe_first_data(lpipe, &first) == 0) // 第一次first=2，此时剩下3...  第二次first=3 此时剩下5...
    {
        int p[2];
        pipe(p); // 当前的管道

        transmit_data(lpipe, p, first); // 第一次存放的是无法被2整除的数字

        if (fork() == 0)
        {
            primes(p);
        }
        else
        {
            close(p[RD]);
            // wait(0);
            int child_status;
            wait(&child_status);
            // 关键修复：从子进程状态中提取实际退出码
            int child_exit_code = child_status & 0xff;
            exit(child_exit_code + 1); // 返回子进程数 + 自身
        }
    }
    else
    {
        // 关键修复：没有数据时也需要显式退出
        exit(0);
    }
}


int main(int argc, char *garv[])
{
    int p[2];
    pipe(p);
    for (int i = 2; i <= 35; ++i)
    {
        write(p[WR], &i, INT_LEN);
    }

    if (fork() == 0)
    {
        primes(p);
    }
    else
    {
        close(p[RD]);
        close(p[WR]);
        int status;
        wait(&status);
        // int total = status & 0xff;
        //printf("Total processes created: %d\n", total);
    }
    exit(0);
}