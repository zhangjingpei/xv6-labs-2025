#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;

union header {
    struct
    {
        union header *ptr;
        uint size;
    } s;
    Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void free(void *ap)
{
    Header *bp, *p;

    bp = (Header *)ap - 1;
    for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
        if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
            break;
    if (bp + bp->s.size == p->s.ptr)
    {
        bp->s.size += p->s.ptr->s.size;
        bp->s.ptr = p->s.ptr->s.ptr;
    }
    else
        bp->s.ptr = p->s.ptr;
    if (p + p->s.size == bp)
    {
        p->s.size += bp->s.size;
        p->s.ptr = bp->s.ptr;
    }
    else
        p->s.ptr = bp;
    freep = p;
}

static Header *morecore(uint nu)
{
    char *p;
    Header *hp;

    if (nu < 4096)
        nu = 4096;
    p = sbrk(nu * sizeof(Header));
    if (p == (char *)-1)
        return 0;
    hp = (Header *)p;
    hp->s.size = nu;
    free((void *)(hp + 1));
    return freep;
}

void *malloc(uint nbytes)
{
    Header *p, *prevp; // p：当前遍历到的空闲块；prevp：p 的前驱空闲块
    uint nunits;       // 需要占用的“单位”数量（Header 为单位）

    // 计算需要多少 Header 单位（向上取整）：
    //   1. 先把用户请求字节数换算成至少能容纳的 Header 数量
    //   2. 再加 1 个 Header 存放块首部的元数据
    nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;

    // 第一次调用 malloc 时，空闲链表尚未建立
    if ((prevp = freep) == 0)
    {
        // 建立只含哑元节点 base 的循环链表
        base.s.ptr = freep = prevp = &base; // 让 freep 指向 base
        base.s.size = 0;                    // 哑元节点本身不占可用空间
    }

    // 遍历空闲链表（循环链表）
    for (p = prevp->s.ptr;; prevp = p, p = p->s.ptr)
    {
        // 1. 找到足够大的空闲块
        if (p->s.size >= nunits)
        {
            // 1-a) 刚好等于所需大小：整块摘链返回
            if (p->s.size == nunits)
                prevp->s.ptr = p->s.ptr;
            // 1-b) 大于所需：将尾部切出一个精确大小的块
            else
            {
                p->s.size -= nunits; // 剩余空闲块缩小
                p += p->s.size;      // 跳到切出的新块起始
                p->s.size = nunits;  // 设置新块大小
            }
            freep = prevp;          // 更新全局空闲链表指针
            return (void *)(p + 1); // 跳过元数据，返回用户可用区
        }

        // 2. 转了一圈又回到 freep，说明当前没有合适空闲块
        if (p == freep)
        {
            // 向内核申请更多内存（morecore 会扩展空闲链表）
            if ((p = morecore(nunits)) == 0)
                return 0; // 申请失败，返回 NULL
            // 申请成功后继续 for 循环，此时 p 指向新加入的大块
        }
    }
}
