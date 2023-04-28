/**
 * @file ringbuf_test.c
 * @author your name (you@domain.com)
 * @brief  ringbuffer的测试程序， 欢迎补充测试样例
 * @version 0.1
 * @date 2023-04-28
 * 
 * @copyright Copyright (c) 2023
 */
#include <stdio.h>
#include <string.h>
#include "ringbuf.h"

int main()
{
    struct ringbuf *buffer;
    struct ringbuf_item *buf_item;
    char data[32] = "WangLu\n";
    char *p;
    int len = strlen(data)+1;

    /* alloc 传入的size为0, 则会启用申请最小2个page */
    buffer = ringbuf_alloc(0);
    ringbuf_show_state(buffer);

    /* 循环写入 */
    for (int i = 0; i < 256; i++) {
        sprintf(data, "ringbufdata%d\n", i);
        len = strlen(data)+1;
        ringbuf_write(buffer, len, data);
    }

    /* 循环读出 */
    for (int i = 0; i < 256; i++) {
        buf_item = ringbuf_consume(buffer);
        len = ringbuf_item_data_length(buf_item);
        p = ringbuf_item_data(buf_item);
        printf("read from ringbuf, len: %d, %s\n", len, p);
    }
        
    /* 循环写入 */
    for (int i = 0; i < 16; i++) {
        sprintf(data, "ringbufdata%d\n", i);
        len = strlen(data)+1;
        ringbuf_write(buffer, len, data);
    }

    ringbuf_show_state(buffer);
    return 0;
}
