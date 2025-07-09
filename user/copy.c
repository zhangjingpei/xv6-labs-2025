#include "kernel/types.h"
#include "user/user.h"

int main()
{
    char buf[64];
    while (1)
    {
        int n = read(0, buf, sizeof buf);
        if (n < 0)
            fprintf(2, "read error.\n");
        else if (n == 0)
            break;
        if (write(1, buf, n) != n)
        {
            fprintf(2, "write error\n");
            exit(1);
        }
    }
    exit(0);
}