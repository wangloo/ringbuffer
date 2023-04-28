/**
 * @file ringbuf_core.h 
 * @author wangloo (cnwanglu@icloud.com)
 * @brief  define some internel and core functions.
 *         should be included after standard libaraies.
 *         内部使用，不应该暴露给外部
 * @version 0.1
 * @date 2023-04-28
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include "ringbuf.h"

#define PAGE_SIZE   0x1000

#define BUF_PAGE_HDR_SIZE (offsetof(struct buf_page, data))
#define BUF_PAGE_SIZE (PAGE_SIZE - BUF_PAGE_HDR_SIZE)



/* list_head->next指针的后两位被用作: 指示指向页面的属性 */
#define RB_PAGE_NORMAL 0UL
#define RB_PAGE_HEAD   1UL
#define RB_PAGE_UPDATE 2UL
#define RB_FLAG_MASK   3UL
// PAGE_MOVED is not part of the mask
#define RB_PAGE_MOVED  4UL


#ifndef RB_ALLOC_DYNAMIC
struct ringbuf g_buffer;
struct buf_page_meta g_bpage[RB_STATIC_PAGES];
char g_page[RB_STATIC_PAGES][PAGE_SIZE];
int g_page_idx = 0;
#endif

static struct list_head *
rb_list_head(struct list_head *list)
{
    unsigned long val = (unsigned long)list;
    return (struct list_head *)(val & ~RB_FLAG_MASK);
}

#define rb_debug(args...) printf(args)

////////////////////////////////////////////
// ringbuf 基础
////////////////////////////////////////////
static inline u32 
rb_num_of_entry(struct ringbuf *buffer)
{
    return buffer->nr_entry - buffer->nr_read;
}

////////////////////////////////////////////
// page 基础
////////////////////////////////////////////
static __always_inline void *
rb_page_index(struct buf_page_meta *bpage, u32 index)
{
    return bpage->page->data + index;
}
static inline 
u32 rb_page_write(struct buf_page_meta *bpage)
{
    return bpage->write;
}

static inline void
rb_inc_page(struct ringbuf *buffer, struct buf_page_meta **bpage)
{
    struct list_head *p = rb_list_head((*bpage)->list.next);
    *bpage = list_entry(p, struct buf_page_meta, list);
}

static __always_inline u32 
rb_page_commit(struct buf_page_meta *bpage)
{
    return bpage->page->commit;
}
static __always_inline u32 
rb_page_size(struct buf_page_meta *bpage)
{
    return rb_page_commit(bpage);
}

////////////////////////////////////////////
// item 相关
////////////////////////////////////////////
#define RB_ITEM_HDR_SIZE (offsetof(struct ringbuf_item, array))
static __always_inline struct ringbuf_item *
rb_reader_item(struct ringbuf *buffer)
{
    return rb_page_index(buffer->reader_page,
            buffer->reader_page->read);
}

static inline void
rb_init_item(struct ringbuf_item *item, int type, u32 len)
{
    item->type = type;
    item->len = len-RB_ITEM_HDR_SIZE;
}

static __always_inline void *
rb_item_data(struct ringbuf_item *item)
{
    return &item->array[0];
}

static inline u32
rb_item_data_length(struct ringbuf_item *item)
{
    return item->len + RB_ITEM_HDR_SIZE;
}
static inline u32
rb_item_length(struct ringbuf_item *item)
{
    // ONLY RINGBUG_TYPE_DATA
    return rb_item_data_length(item);
}

////////////////////////////////////////////
// head_page 相关
////////////////////////////////////////////
static inline int
rb_is_head_page(struct ringbuf *buffer, 
        struct buf_page_meta *page, struct list_head *list)
{
    unsigned long val;
    
    val = (unsigned long)list->next;
    if ((val & ~RB_FLAG_MASK) != (unsigned long)&page->list)
        return RB_PAGE_MOVED;
    return val&RB_FLAG_MASK;
}

// set a list_head to be pointing to head_page 
static void 
rb_set_list_to_head(struct list_head *list)
{
    unsigned long *ptr;

    ptr = (unsigned long *)&list->next;
    *ptr |= RB_PAGE_HEAD;
    *ptr &= ~RB_PAGE_UPDATE;
}

static void 
rb_list_head_clear(struct list_head *list)
{
    unsigned long *ptr = (unsigned long *)&list->next;
    *ptr &= ~RB_FLAG_MASK;
}

// 当old->prev->next的值有flag RB_PAGE_HEAD, 代表next指向的是
// head_page, 此函数实现的效果是将old->prev->next指向new,
// 会取消head_page的FLAG，即修改后new不再是head_page.
// 同时，该函数不能建立完整的链表，还有new->prev应该设置为
// old->prev, caller完成
static int
rb_head_page_replace(struct buf_page_meta *old, struct buf_page_meta *new)
{
    unsigned long *ptr;
    unsigned long val, ret;

    ptr = (unsigned long *)&old->list.prev->next;
    val = *ptr & ~RB_FLAG_MASK;
    val |= RB_PAGE_HEAD;

    ret = cmpxchg(ptr, val, (unsigned long)&new->list);
    return ret == val;
}

static void
rb_head_page_activate(struct ringbuf *buffer)
{
    struct buf_page_meta *head;
    
    head = buffer->head_page;
    if (!head) 
        assert(0);

    rb_set_list_to_head(head->list.prev);
}
static void
rb_head_page_deactivate(struct ringbuf *buffer)
{
    rb_list_head_clear(buffer->head_page->list.prev);
}

    

////////////////////////////////////////////
// reader_page 相关
////////////////////////////////////////////
/**
 * 获取当前状态下合适的 reader page
 * 如果当前buffer->reader_page已经读取完毕，那么该函数还负责
 * 选择新的reader_page, 并将旧的放回环形链表中.
 */
struct buf_page_meta *
rb_get_reader_page(struct ringbuf *buffer)
{
    struct buf_page_meta *reader = buffer->reader_page;

    if (reader->read < rb_page_size(reader)) {
        rb_debug("[move](reader_page) unmoved\n");
        return reader;
    }
    
    // 完整性检查 
    if (reader->read > rb_page_size(reader))
        assert(0);

    if(rb_num_of_entry(buffer) == 0) {
        rb_debug("[r] no data to read\n");
        return NULL;
    }

    /* reader_page needs to be moved */

    /* reset the older reader page */
    buffer->reader_page->write = 0;
    buffer->reader_page->nr_entry = 0;

    /* new reader_page is head_page */
    reader = buffer->head_page;
    if (!reader)
        return NULL;
    buffer->reader_page->list.next = rb_list_head(reader->list.next);
    buffer->reader_page->list.prev = reader->list.prev;

    /* the reader page will be pointing to the head */
    rb_set_list_to_head(&buffer->reader_page->list);

    // little trick, 将head_page->prev->next指向
    // reader_page, 配合前后的操作实现将reader_page
    // 插入新的head_page前，而旧的head_page则加入
    // reader_page
    rb_head_page_replace(reader, buffer->reader_page);
    rb_list_head(reader->list.next)->prev = &buffer->reader_page->list;

    // old reader_page->next 已经添加了head_page FLAG
    // 可以放心设置head_page
    rb_inc_page(buffer, &buffer->head_page);

    // update reader_page finally
    buffer->reader_page = reader;
    buffer->reader_page->read = 0;
    rb_debug("[move](reader_page) change to new : <%p>\n", reader);

    return reader;
}

/**
 * 更新buffer的状态, 主要包括:
 * - reader_page->read
 * - buffer->nr_read
 * TODO: 简化操作，或许不需要重新get_reader_page
 */
static void rb_advance_reader(struct ringbuf *buffer)
{
    struct ringbuf_item *item;
    struct buf_page_meta *reader;
    u32 length;  // length of item

    reader = rb_get_reader_page(buffer);
    if (!reader)
        assert(0);
    item = rb_reader_item(buffer);
    buffer->nr_read += 1;

    length = rb_item_length(item);
    buffer->reader_page->read += length;
}


/**
 * peek next readable item in ringbuffer.
 * 
 * return NULL is no readable data for this buffer.
 */
static struct ringbuf_item *
rb_buf_peek(struct ringbuf *buffer)
{
    struct buf_page_meta *reader;
    struct ringbuf_item *item;
    
    reader = rb_get_reader_page(buffer);
    if (!reader)
        return NULL;

    item = rb_reader_item(buffer);
    return item;
}
    
////////////////////////////////////////////
// tail_page 相关
////////////////////////////////////////////
static int
rb_move_tail(struct ringbuf *buffer, u32 length)
{
    struct buf_page_meta *tail_page, *next_page;

    tail_page = buffer->tail_page;
    next_page = tail_page;
    
    if (tail_page == buffer->reader_page){
        next_page = buffer->head_page;
    }
    else {
        rb_inc_page(buffer, &next_page);
    }

    // 填满original tail_page, 使得不会在填入任何长度的item
    tail_page->write = BUF_PAGE_SIZE;

    // ringbuffer 所有的page已经满了
    if (length + rb_page_size(next_page) > BUF_PAGE_SIZE) {
        next_page->write = BUF_PAGE_SIZE;
        rb_debug("[move](tail_page) no more available pages!\n");
        return 1; 
    }
    
    next_page->page->commit = 0;
    buffer->tail_page = next_page;
    rb_debug("[move](tail_page) <%p> to <%p>\n", tail_page, next_page);
    return 0;
}

////////////////////////////////////////////
// build 相关
////////////////////////////////////////////
static void
rb_init_page(struct buf_page *bpage)
{
    bpage->commit = 0;
}
static int 
__rb_allocate_pages(u32 nr_pages, struct list_head *pages)
{
    struct buf_page_meta *bpage;
    struct buf_page *page;
    long i;

    for (i = 0; i < nr_pages; i++) {
#ifdef RB_ALLOC_DYNAMIC
        bpage = malloc(sizeof(*bpage));
        if (!bpage)
            assert (0);
        rb_debug("[new] alloc new page <%p>\n",  bpage);
        page = malloc(PAGE_SIZE);
        if (!page)
            assert(0);
#else
        assert(g_page_idx < nr_pages+1);
        bpage = &g_bpage[g_page_idx];
        page = (struct buf_page *)&g_page[g_page_idx];
        g_page_idx ++;
#endif
        
        bpage->page = page;
        list_add(&bpage->list, pages);
        rb_init_page(page);
    }
    return 0;
}

static int
rb_allocate_pages(struct ringbuf *buffer, u32 nr_pages)
{
    if (nr_pages < 2) 
        assert(0);

    LIST_HEAD(pages);
    if(__rb_allocate_pages(nr_pages, &pages))
        return 1;

    /* 以上创建的pages仅仅是建立整个双向链表
     * 过渡使用的临时变量
     */
    buffer->pages = pages.next;
    list_del(&pages);

    buffer->nr_page = nr_pages;

    return 0;
}


////////////////////////////////////////////
// commit 相关
////////////////////////////////////////////
static void 
rb_commit(struct ringbuf *buffer, struct ringbuf_item *item)
{
    buffer->nr_entry += 1;
    buffer->tail_page->page->commit = rb_page_write(buffer->tail_page);
}

#if 0
////////////////////////////////////////////
// iterator 相关
////////////////////////////////////////////
static void 
rb_inc_iter(struct ringbuf_iter *iter)
{

static __always_inline struct ringbuf_item *
rb_iter_head_event(struct ringbuf_iter *iter)
{
    return rb_page_index(iter->head_page, iter->head);
}

static struct ringbuf_item *
rb_iter_peek(struct ringbuf_iter *iter)
{
    struct ringbuf *buffer = iter->buffer;

again:
    if (ringbuf_iter_empty(iter))
        return NULL;
    if (rb_buffer_empty(buffer))
        return NULL;
    
    // 此page读取完成
    if (iter->head >= rb_page_size(iter->head_page)) {
        rb_inc_iter(iter);
        goto again;
    }

}
#endif
