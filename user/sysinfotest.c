#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

/*
 * 文件功能：sysinfo系统调用的测试程序
 * 包含以下测试：
 * 1. 系统调用参数验证测试 (testcall)
 * 2. 内存信息准确性测试 (testmem)
 * 3. 进程计数正确性测试 (testproc)
 */

// 封装sysinfo系统调用，错误处理
void sinfo(struct sysinfo *info)
{
    if (sysinfo(info) < 0)
    {
        printf("FAIL: sysinfo failed");
        exit(1);
    }
}

//
// use sbrk() to count how many free physical memory pages there are.
//
/* 通过sbrk分配内存计算可用物理内存 */
int countfree()
{
    uint64 sz0 = (uint64)sbrk(0); // 记录初始堆顶
    struct sysinfo info;
    int n = 0;

    // 循环分配内存页直到失败
    while (1)
    {
        if ((uint64)sbrk(PGSIZE) == 0xffffffffffffffff) // 分配失败时退出
        {
            break;
        }
        n += PGSIZE; // 累计分配的内存大小
    }

    // 此时应无可用内存
    sinfo(&info);
    if (info.freemem != 0)
    {
        printf("FAIL: there is no free mem, but sysinfo.freemem=%d\n", info.freemem);
        exit(1);
    }

    // 恢复堆指针到初始位置
    sbrk(-((uint64)sbrk(0) - sz0));
    return n; // 返回计算出的总空闲内存
}

/* 测试sysinfo内存信息准确性 */
void testmem()
{
    struct sysinfo info;
    uint64 n = countfree(); // 获取基准空闲内存值

    sinfo(&info);

    // 验证空闲内存报告值
    if (info.freemem != n)
    {
        printf("FAIL: free mem %d (bytes) instead of %d\n", info.freemem, n);
        exit(1);
    }

    // 分配一页内存
    if ((uint64)sbrk(PGSIZE) == 0xffffffffffffffff)
    {
        printf("sbrk failed");
        exit(1);
    }

    sinfo(&info);

    // 验证分配后空闲内存减少
    if (info.freemem != n - PGSIZE)
    {
        printf("FAIL: free mem %d (bytes) instead of %d\n", n - PGSIZE, info.freemem);
        exit(1);
    }

    // 释放分配的内存页
    if ((uint64)sbrk(-PGSIZE) == 0xffffffffffffffff)
    {
        printf("sbrk failed");
        exit(1);
    }

    sinfo(&info);

    // 验证释放后空闲内存恢复
    if (info.freemem != n)
    {
        printf("FAIL: free mem %d (bytes) instead of %d\n", n, info.freemem);
        exit(1);
    }
}

/* 测试sysinfo系统调用参数验证 */
void testcall()
{
    struct sysinfo info;

    // 测试正常调用
    if (sysinfo(&info) < 0)
    {
        printf("FAIL: sysinfo failed\n");
        exit(1);
    }

    // 测试非法地址参数（应返回错误）
    if (sysinfo((struct sysinfo *)0xeaeb0b5b00002f5e) != 0xffffffffffffffff)
    {
        printf("FAIL: sysinfo succeeded with bad argument\n");
        exit(1);
    }
}

/* 测试进程计数功能 */
void testproc()
{
    struct sysinfo info;
    uint64 nproc; // 初始进程数
    int status;
    int pid;

    // 获取初始进程数
    sinfo(&info);
    nproc = info.nproc;

    // 创建子进程
    pid = fork();
    if (pid < 0)
    {
        printf("sysinfotest: fork failed\n");
        exit(1);
    }
    if (pid == 0) // 子进程
    {
        sinfo(&info);
        // 子进程中应看到进程数+1
        if (info.nproc != nproc + 1)
        {
            printf("sysinfotest: FAIL nproc is %d instead of %d\n", info.nproc, nproc + 1);
            exit(1);
        }
        exit(0);
    }
    wait(&status); // 等待子进程退出
    sinfo(&info);
    // 子进程退出后进程数应恢复
    if (info.nproc != nproc)
    {
        printf("sysinfotest: FAIL nproc is %d instead of %d\n", info.nproc, nproc);
        exit(1);
    }
}

/* 主测试函数 */
int main(int argc, char *argv[])
{
    printf("sysinfotest: start\n");
    testcall(); // 参数验证测试
    testmem();  // 内存报告测试
    testproc(); // 进程计数测试
    printf("sysinfotest: OK\n");
    exit(0);
}