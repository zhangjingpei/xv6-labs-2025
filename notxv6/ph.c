#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define NBUCKET 5    // 哈希表的桶数量
#define NKEYS 100000 // 要操作的键值对总数

// 哈希表节点结构
struct entry
{
    int key;
    int value;
    struct entry *next; // 指向下一个节点的指针（链表解决哈希冲突）
};
struct entry *table[NBUCKET]; // 哈希表数组，每个桶是一个链表头指针
int keys[NKEYS];              // 存储所有键的数组
// 为每个 bucket 创建一个锁
pthread_mutex_t locks[NBUCKET];
int nthread = 1; // 线程数（默认为1）

// 获取当前时间（秒为单位，精确到微秒）
double now()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1000000.0; // 转换为秒
}

// 在链表头部插入新节点
static void insert(int key, int value, struct entry **p, struct entry *n)
{
    struct entry *e = malloc(sizeof(struct entry));
    e->key = key;
    e->value = value;
    e->next = n; // 新节点指向原链表头
    *p = e;      // 更新链表头指针为新节点
}

static void put(int key, int value)
{
    int i = key % NBUCKET; // 哈希函数：计算桶索引

    // is the key already present?
    // 遍历链表查找是否已存在该键
    struct entry *e = 0;
    for (e = table[i]; e != 0; e = e->next)
    {
        if (e->key == key)
            break;
    }
    if (e)
    {
        // update the existing key.
        // 键已存在：更新值
        e->value = value;
    }
    else
    {
        pthread_mutex_lock(&locks[i]); // 加锁
        // the new is new.
        // 键不存在：在链表头部插入新节点
        insert(key, value, &table[i], table[i]);
        pthread_mutex_unlock(&locks[i]); // 解锁
    }
}

// 查找键对应的节点
static struct entry *get(int key)
{
    int i = key % NBUCKET; // 哈希函数：计算桶索引

    // 遍历链表查找键
    struct entry *e = 0;
    for (e = table[i]; e != 0; e = e->next)
    {
        if (e->key == key)
            break;
    }

    return e;
}

// 线程函数：执行键值对插入
static void *put_thread(void *xa)
{
    int n = (int)(long)xa;   // 线程编号（0到nthread-1）
    int b = NKEYS / nthread; // 每个线程处理的键数量

    // 将分配给本线程的键插入哈希表
    for (int i = 0; i < b; i++)
    {
        put(keys[b * n + i], n);
    }

    return NULL;
}

// 线程函数：执行键查找
static void *get_thread(void *xa)
{
    int n = (int)(long)xa; // thread number/ 线程编号
    int missing = 0;       // 未找到的键计数器

    for (int i = 0; i < NKEYS; i++)
    {
        struct entry *e = get(keys[i]);
        if (e == 0)
            missing++;
    }
    printf("%d: %d keys missing\n", n, missing); // 打印本线程的缺失统计
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t *tha; // 线程句柄数组
    void *value;    // 线程返回值占位符
    double t1, t0;  // 时间测量变量

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
        exit(-1);
    }
    // 初始化所有锁
    for (int i = 0; i < NBUCKET; i++)
    {
        pthread_mutex_init(&locks[i], NULL);
    }

    nthread = atoi(argv[1]);

    // 分配线程句柄数组内存
    tha = malloc(sizeof(pthread_t) * nthread);
    // 初始化随机数生成器
    srandom(0);
    assert(NKEYS % nthread == 0); // 确保键数能被线程数整除

    // 生成随机键数组
    for (int i = 0; i < NKEYS; i++)
    {
        keys[i] = random();
    }

    //
    // first the puts
    //
    // ------------ 插入阶段 ------------
    t0 = now(); // 记录起始时间
    for (int i = 0; i < nthread; i++)
    {
        assert(pthread_create(&tha[i], NULL, put_thread, (void *)(long)i) == 0);
    }
    for (int i = 0; i < nthread; i++)
    {
        assert(pthread_join(tha[i], &value) == 0);
    }
    t1 = now();

    // 打印插入性能统计
    printf("%d puts, %.3f seconds, %.0f puts/second\n", NKEYS, t1 - t0, NKEYS / (t1 - t0));

    //
    // now the gets
    //
    t0 = now();
    for (int i = 0; i < nthread; i++)
    {
        assert(pthread_create(&tha[i], NULL, get_thread, (void *)(long)i) == 0);
    }
    for (int i = 0; i < nthread; i++)
    {
        assert(pthread_join(tha[i], &value) == 0);
    }
    t1 = now();

    printf("%d gets, %.3f seconds, %.0f gets/second\n", NKEYS * nthread, t1 - t0, (NKEYS * nthread) / (t1 - t0));
}
