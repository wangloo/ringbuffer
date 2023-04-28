#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "ringbuf.h"
#include "ringbuf_core.h"

/**
 * @brief reserve a part of the buffer
 * @param buffer 
 * @param length length of the data to reserve (excluding event header)
 * 
 * 返回一个对应于保留区域的ring buffer 子项结构，caller可直接向其成员
 * `->data`指向的位置写入长度为length的数据
 */
struct ringbuf_item *
ringbuf_reserve_item(struct ringbuf *buffer, u32 length)
{
    struct buf_page_meta *tail_page;
    struct ringbuf_item *item;
    u32 tail;

    if (!length) 
        length += 1;
    length += RB_ITEM_HDR_SIZE;
    length = ALIGN_UP(length, RB_ARCH_ALIGNMENT);

    // no enough space for this page
    if (length + rb_page_write(buffer->tail_page) > BUF_PAGE_SIZE) {
        if (rb_move_tail(buffer, length)) {
            ringbuf_show_state(buffer);
            assert(0);
        }
    }
    rb_debug("[w] write in 0x%x bytes, remain 0x%lx bytes in current tail_page\n",
            length, BUF_PAGE_SIZE-length-rb_page_write(buffer->tail_page));

    tail_page = buffer->tail_page;
    tail = tail_page->write;

    tail_page->write += length;
    tail_page->nr_entry += 1;

    item = rb_page_index(tail_page, tail);
    rb_init_item(item, 0, length);

    return item;
}



/**
 * @brief return an item and consume it
 * @param buffer 
 * 
 * Returns the next event in the ring buffer, and that event is consumed.
 * Meaning, that sequential reads will keep returning a different event,
 * and eventually empty the ring buffer if the producer is slower.
 * 
 * Return NULL if no readable data.
 */
struct ringbuf_item *
ringbuf_consume(struct ringbuf *buffer)
{
    struct ringbuf_item *item;

    item = rb_buf_peek(buffer);
    if (item)
        rb_advance_reader(buffer);
    return item;
}

void *ringbuf_item_data(struct ringbuf_item *item)
{
    return rb_item_data(item);
}

u32 ringbuf_item_data_length(struct ringbuf_item *item)
{
    u32 length;

    length = rb_item_length(item);
    rb_debug("ORIGIN lengeth: %d\n", length);
    return length-RB_ITEM_HDR_SIZE;
}


/* 
 * write data to the buffer without reserving
 *
 * This is like ring_buffer_lock_reserve and ring_buffer_unlock_commit as
 * one function. If you already have the data to write to the buffer, it
 * may be easier to simply call this function.
 * 
 * Note, like ring_buffer_lock_reserve, the length is the length of the data
 * and not the length of the event which would hold the header.
 */
int
ringbuf_write(struct ringbuf *buffer, u32 length, void *data)
{
    struct ringbuf_item *item;
    void *body;

    item = ringbuf_reserve_item(buffer, length);
    if (!item)
        return 1;
    /* printf("write to item: 0x%p\n", item); */
    
    body = rb_item_data(item);
    memcpy(body, data, length);

    rb_commit(buffer, item);

    return 0;
}

static void 
free_buf_page(struct buf_page_meta *bpage)
{
#ifdef RB_ALLOC_DYNAMIC
    free(bpage->page);
    free(bpage);
#endif
}

/**
 * @brief allocate and init a ringbuffer
 * 
 * @param size 
 * @return struct ringbuf* 
 */
struct ringbuf *ringbuf_alloc(u32 size)
{
    struct ringbuf *buffer;
    struct buf_page_meta *bpage;
    struct buf_page *page;
    u32 nr_pages;
    int ret;

    nr_pages = DIV_ROUND_UP(size, BUF_PAGE_SIZE);
    if (nr_pages < 2)
        nr_pages = 2;

#ifdef RB_ALLOC_DYNAMIC
    buffer = malloc(sizeof(*buffer));
    if (!buffer)  assert(0);

    // allocate reader page alone
    bpage = calloc(1, sizeof(*bpage));
    if (!bpage)  assert(0);
    page = malloc(PAGE_SIZE);
    if (!page) assert(0);
#else
    buffer = &g_buffer;
    bpage = &g_bpage[g_page_idx];
    page = (struct buf_page *)&g_page[g_page_idx];
    g_page_idx ++;
#endif

    bpage->page = page;
    buffer->reader_page = bpage;
    rb_init_page(page);

    INIT_LIST_HEAD(&buffer->reader_page->list);

    // allocate other pages
    ret = rb_allocate_pages(buffer, nr_pages);
    if (ret < 0)
        assert(0);

    buffer->head_page = list_entry(buffer->pages, struct buf_page_meta, list);
    buffer->tail_page = buffer->head_page;
    
    rb_head_page_activate(buffer);
    
    return buffer;
}

/**
 * @brief free the ringbuffer
 * 
 * @param buffer 
 */
void ringbuf_free(struct ringbuf *buffer)
{
    struct list_head *head = buffer->pages;
    struct buf_page_meta *bpage, *tmp;

    // clear flag 才可以使用list_for_each
    rb_head_page_deactivate(buffer);
    if (head) {
        list_for_each_entry_safe(bpage, tmp, head, list) {
            list_del_init(&bpage->list);
            free_buf_page(bpage);
        }
        bpage = list_entry(head, struct buf_page_meta, list);
        free_buf_page(bpage);
    }
#ifdef RB_ALLOC_DYNAMIC
    free(buffer);
#endif
}

/*
 * print some state of ringbuffer 
 */
void ringbuf_show_state(struct ringbuf *buffer)
{
    struct list_head *p, *tmp;
    struct buf_page_meta *page;

    rb_debug("ringbuf hdr:\n");
    rb_debug("- nr_page: %d\n", buffer->nr_page);
    rb_debug("- nr_entry: %d\n", buffer->nr_entry);
    rb_debug("- nr_read: %d\n", buffer->nr_read);
    rb_debug("- reader_page: <0x%lx>\n", (unsigned long)buffer->reader_page);
    rb_debug("- head_page: <0x%lx>\n", (unsigned long)buffer->head_page);
    rb_debug("- tail_page: <0x%lx>\n", (unsigned long)buffer->tail_page);

    rb_debug("- entryof pages:\n");
    p = buffer->pages;
    tmp = p;
    do {
        page = list_entry(tmp, struct buf_page_meta, list);
        rb_debug("   <%p>, write: 0x%x, read: 0x%x\n", 
                page, page->write, page->read);
        tmp = rb_list_head(tmp->next);
    } while (tmp != p);
}


#if 0
/**
 * ring_buffer_iter_empty - check if an iterator has no more to read
 */
int ringbuf_iter_empty(struct ringbuf_iter *iter)
{
    struct ringbuf *buffer;
    struct buf_page_meta *reader;
    struct buf_page_meta *head_page, *commit_page;
    u32 commit;

    buffer = iter->buffer;

    reader = buffer->reader_page;
    head_page = buffer->head_page;
    commit = rb_page_commit(commit_page);
    return ((iter->head_page == commit_page && iter->head == commit) ||
            (iter->head_page == reader && commit_page == head_page &&
             head_page->read == commit &&
             iter->head == rb_page_commit(buffer->reader_page)));
}

#endif
