/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-04-14     chenyong     first version
 */

#include <at.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include "qosa_def.h"
#include "qosa_system.h"
#include "hal_uart.h"
#include "qosa_log.h"
/**
 * dump hex format data to console device
 *
 * @param name name for hex object, it will show on log header
 * @param buf hex buffer
 * @param size buffer size
 */
extern LogLevel g_debug_level;

void at_print_raw_cmd(const char *name, const char *buf, size_t size)
{
#define __is_print(ch)       ((unsigned int)((ch) - ' ') < 127u - ' ')
#define WIDTH_SIZE           96

    size_t i, j;
    int index = 0;
    char buffer[128] = {0};

    if (g_debug_level > LOG_DEBUG) {
        return;
    }

    for (i = 0; i < size; i += WIDTH_SIZE)
    {
        index = 0;
        memset(buffer, 0, sizeof(buffer));
        index = snprintf(buffer, sizeof buffer, "\t%.24s: ", name ? name : "?");
        for (j = 0; j < WIDTH_SIZE; j++)
        {
            if (i + j < size)
            {
                //rt_kprintf("%c", __is_print(buf[i + j]) ? buf[i + j] : '.');
                buffer[index++] = __is_print(buf[i + j]) ? buf[i + j] : '.';
            }
        }
        buffer[index] = '\0';
        LOG_D("%s", buffer);
    }
}

size_t at_utils_send(size_t pos, const void *buffer, size_t size)
{
    int sent = qosa_uart_write(pos, buffer, size);
    return sent > 0 ? (size_t)sent : 0;
}

size_t at_vprintf(char *send_buf, size_t buf_size, const char *format, va_list args)
{
    if (send_buf == NULL || format == NULL || buf_size == 0) {
        return 0;
    }
    int result = vsnprintf(send_buf, buf_size, format, args);
    if (result <= 0 || (size_t)result >= buf_size)
    {
        return 0;
    }
    size_t len = (size_t)result;

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("sendline", send_buf, len);
#endif

    return at_utils_send(0, send_buf, len) == len ? len : 0;
}

size_t at_vprintfln(char *send_buf, size_t buf_size, const char *format, va_list args)
{
    if (send_buf == NULL || format == NULL || buf_size < 3) {
        return 0;
    }
    /* Reserve one CR and a NUL. Never send a truncated command. */
    int result = vsnprintf(send_buf, buf_size - 1, format, args);
    if (result <= 0 || (size_t)result >= buf_size - 1)
    {
        return 0;
    }
    size_t len = (size_t)result;

    send_buf[len++] = '\r';
    send_buf[len] = '\0';

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("sendline", send_buf, len);
#endif

    return at_utils_send(0, send_buf, len) == len ? len : 0;
}
