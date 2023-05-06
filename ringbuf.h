/**
 * @file ringbuf.h
 * @author wangloo (cnwanglu@icloud.com)
 * @brief  提供外部调用的接口
 * @version 0.1
 * @date 2023-04-28
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include <stdint.h>
#include "list.h"

////////////////////////////////////////////
// Configuration of ringbuffer
////////////////////////////////////////////
// #define RB_ALLOC_DYNAMIC       // 启用此定义代表所有内存分配使用malloc/free接口
#define RB_STATIC_PAGES   (3)  // 如果采用静态定义方案，规定池子中的page数
#define RB_ARCH_ALIGNMENT (4u) // 存入数据长度的对齐规则

typedef uint8_t u8;
typedef uint32_t u32;


////////////////////////////////////////////
// Declaration of rinbuffer structure
////////////////////////////////////////////

// ringbuf 中存储单元结构
struct ringbuf_item {
    // type: not used for now
    // len: exclude header
    u32 type:5, len:27;
    u8 array[];
};

// ring buffer 中一个完整的page, 其动态长度=PAGE_SIZE
struct buf_page {
    u32 time_stamp; // not used for now!
    u32 commit;     // 代表page中真实数据的大小，因为write有可能添加了padding
                    // TODO: 是否可以放到 struct buf_page_meta 中？
    u8 data[];
};

// 描述一个ringbuffer page
struct buf_page_meta {
    struct list_head list;
    u32 write, read;
    u32 nr_entry;
    struct buf_page *page;
};

// 描述一个ringbuffer
struct ringbuf {
    struct buf_page_meta *head_page, *tail_page;
    struct buf_page_meta *reader_page;
    struct list_head *pages;
    u32 nr_page;     // 包含多少page
    u32 nr_entry;    // 存入的item数量
    u32 nr_read;     // 已经读到的item数量
};

struct ringbuf * ringbuf_alloc_static(u32 size);
struct ringbuf * ringbuf_alloc(u32 size);
void ringbuf_free(struct ringbuf *buffer);
void ringbuf_show_state(struct ringbuf *buffer);

int  ringbuf_write(struct ringbuf *buffer, u32 length, void *data);
void ringbuf_commit(struct ringbuf *buffer, struct ringbuf_item *item);
struct ringbuf_item * ringbuf_reserve_item(struct ringbuf *buffer, u32 length);
struct ringbuf_item * ringbuf_consume(struct ringbuf *buffer);

    
void * ringbuf_item_data(struct ringbuf_item *item);
u32    ringbuf_item_data_length(struct ringbuf_item *item);
