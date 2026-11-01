/*
 * usart.c  --  printf 重定向
 *
 * 三件套缺一不可（踩过的坑）：
 *   1) --specs=nano.specs     已在 cmake/gcc-arm-none-eabi.cmake 里配好
 *   2) __io_putchar 必须是非 weak 版本，覆盖 syscalls.c 里的 weak 声明
 *   3) setvbuf(_IONBF)        否则行缓冲会憋住 '\n' 后半行，串口看着像卡死
 */
#include "main.h"
#include <stdio.h>

extern UART_HandleTypeDef huart1;

int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;

    /* 把 '\n' 展开成 CRLF，否则串口助手显示锯齿状 */
    if (c == '\n') {
        uint8_t cr = '\r';
        HAL_UART_Transmit(&huart1, &cr, 1, 10);
    }
    HAL_UART_Transmit(&huart1, &c, 1, 10);
    return ch;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    for (int i = 0; i < len; i++) __io_putchar((int)ptr[i]);
    return len;
}
