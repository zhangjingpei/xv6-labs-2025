#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

void find(char * path, const char* filename)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

}