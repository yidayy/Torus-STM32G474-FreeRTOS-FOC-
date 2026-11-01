/**
 * syscalls.c — newlib 系统调用桩（STM32G4 / GCC arm-none-eabi）
 *
 * 只实现裸机必需的最小集合：
 *   _write   已由 usart.c 提供（printf 走 USART1），此处声明为 weak 兜底
 *   _sbrk    给 malloc/newlib 提供堆增长（FreeRTOS heap_4 不用它，但 printf 的
 *            浮点格式化内部可能用到 newlib 的临时缓冲）
 *   _read/_close/_lseek/_fstat/_isatty/_kill/_getpid/_exit 一律空实现
 */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#undef errno
extern int errno;

extern int _write(int file, char *ptr, int len);

/* 链接脚本里的堆末尾标记 */
extern char _end;
static char *s_heap_end = 0;

void *_sbrk(ptrdiff_t incr)
{
    char *prev;
    if (s_heap_end == 0) {
        s_heap_end = &_end;
    }
    prev = s_heap_end;

    /* 栈向下生长，堆向上生长；简单防越界：留出 1 KB 余量 */
    extern char _estack;
    if (s_heap_end + incr > (&_estack - 1024)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    s_heap_end += incr;
    return prev;
}

int _read(int file, char *ptr, int len)
{
    (void)file; (void)ptr; (void)len;
    return 0;
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file; (void)ptr; (void)dir;
    return 0;
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}

void _exit(int status)
{
    (void)status;
    while (1) {
        /* 停机等待看门狗复位 */
    }
}
