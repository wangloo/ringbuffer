> 欢迎大家提出 issue 和 pr~

## Introduce

模仿 Linux trace event ring buffer 的实现方式，实现一个简易版本。

仅包含基本的存入、取出功能，未实现所有 lock-free 相关的部分。

## Basic data-structure

数据结构的组织基本与 Linux 一致，ringbuffer 由多个 page 组成，
page 之间通过内置链表组成 ring，故称之为 ringbuffer。

每个 page 可存储若干数据子项, Linux 中称之为`ring_buffer_event`, 与本项目的`ringbuf_item`结构对应。

```c
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
```

## Interface

本项目所有接口的使用可参见 ringbuf_test.c

## Build

本项目提供了一个简单的 makefle，可以编译运行`ringbuf_test.c`中的 demo。

> 注意：没有实现对头文件的追踪，修改头文件后别忘了`make clean`再`make`.
