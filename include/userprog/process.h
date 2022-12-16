#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "userprog/process.h"

struct container{
    struct file *file;
    off_t offset;
    size_t page_read_bytes;
    size_t page_zero_bytes;
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
bool mmap_lazy_load(struct page *page, struct container *container);

#endif /* userprog/process.h */
