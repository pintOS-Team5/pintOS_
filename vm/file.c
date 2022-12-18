/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "userprog/syscall.h"
#include <round.h>
#include <string.h>


static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	file_page->init = page->uninit.init;
	file_page->type = page->uninit.type;
	file_page->aux = page->uninit.aux;
	file_page->page_initializer = page->uninit.page_initializer;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	// printf("SWAP IN\n");
	bool locked = false;
	struct file_page *file_page = &page->file;

	struct container *container = file_page->aux;
	struct file *file = container->file;
	off_t offset = container->offset;
	size_t page_read_bytes = container->page_read_bytes;
	size_t page_zero_bytes = container->page_zero_bytes;

	/* 나도 이걸 왜 하는지 모르겠음 */
	bool old_dirty = pml4_is_dirty(thread_current()->pml4, page->va);

	if (!lock_held_by_current_thread(&filesys_lock)){
		locked = true;
		lock_acquire(&filesys_lock);
	}

	if (file_read_at(file, page->va, page_read_bytes, offset) != (int)page_read_bytes){
		return false;
	}

	if (locked){
		locked = false;
		lock_release(&filesys_lock);
	}
	memset(page->va + page_read_bytes, 0, page_zero_bytes);

	/* 나도 이걸 왜 하는지 모르겠음 */
	pml4_set_dirty(thread_current()->pml4, page->va, old_dirty);
	

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
	struct frame *frame = page->frame;
	void *aux = file_page->aux;

	if (frame == NULL || aux == NULL){
		goto end;
	}

	if (pml4_is_dirty(page->pml4, page->va)){
		file_backed_write_back(aux, frame->kva);
		pml4_set_dirty(page->pml4, page->va, false);
	}

	if (pml4_get_page(page->pml4, page->va) != NULL){
		memset(frame->kva, 0, PGSIZE);
		pml4_clear_page(page->pml4, page->va);
	}

	frame->page = NULL;
	page->frame = NULL;

end:
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	struct frame *frame = page->frame;
	void *aux = file_page->aux;
	bool locked = false;

	if(frame == NULL || aux == NULL){
		goto end;
		// return;
	}

	struct container *container = (struct container*)aux;
	struct file *file = container->file;
	if (!lock_held_by_current_thread(&filesys_lock))
	{
		locked = true;
		lock_acquire(&filesys_lock);
	}

	if (pml4_is_dirty(thread_current()->pml4, page->va)){
		file_backed_write_back(aux, frame->kva);
	}

	file_close(file);

	if (locked)
	{
		lock_release(&filesys_lock);
		locked = false;
	}

	memset(frame->kva, 0, PGSIZE);
	pml4_clear_page(thread_current()->pml4, page->va);

	ft_remove_frame(frame);
	frame->page= NULL;
	page->frame = NULL;

	palloc_free_page(frame->kva);
	free(frame);
end:
	free(aux);
}

void file_backed_write_back(void *aux, void *kva){
	ASSERT(aux != NULL);

    struct container *container = (struct container *)aux;
    struct file *file = container->file;
	bool locked = false;

	off_t offset = container->offset;
    size_t page_read_bytes = container->page_read_bytes;
    
   	if (!lock_held_by_current_thread(&filesys_lock)){
		locked = true;
		lock_acquire(&filesys_lock);
	}
	file_seek(file, offset);
    file_write(file, kva, page_read_bytes);
    if (locked){
		locked = false;
		lock_release(&filesys_lock);
	}
}

static bool 
file_lazy_load_segment (struct page *page, void *aux) {
    struct container *args = (struct container *)aux;
    struct file *file = args->file;
	off_t ofs = args->offset;
	size_t read_bytes = args->page_read_bytes;
	size_t zero_bytes = args->page_zero_bytes;
	bool file_lock_holder = lock_held_by_current_thread(&filesys_lock);

    if(!file_lock_holder) lock_acquire(&filesys_lock);
    file_seek(file, ofs);
    if (file_read (file, page->frame->kva, read_bytes) != (int) read_bytes) {
        return false;
    }
    if(!file_lock_holder) lock_release(&filesys_lock);

    memset (page->frame->kva + read_bytes, 0, zero_bytes);
    
    return true;
}


/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	void *start_addr = addr;
	int page_cnt = 0;
	bool locked = false;

	if (!lock_held_by_current_thread(&filesys_lock)){
		locked = true;
		lock_acquire(&filesys_lock);
	}
	uint32_t read_bytes, zero_bytes, readable_bytes;
	if((readable_bytes = file_length(file) - offset) <= 0){
		return NULL;
	}

	read_bytes = length <= readable_bytes ? length : readable_bytes;
	zero_bytes = PGSIZE - (read_bytes % PGSIZE);

	for (;addr < (start_addr + read_bytes + zero_bytes); addr += PGSIZE)
	{
		page_cnt += 1;
		if (spt_find_page(spt, pg_round_down(addr)))
			return NULL;
	}

	addr = start_addr;
	while (read_bytes > 0 || zero_bytes > 0)
	{
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct container *container = (struct container *)malloc(sizeof(struct container));
		if (container == NULL)
			return NULL;
		container->file = file_reopen (file);
		container->page_read_bytes = page_read_bytes;
		container->page_zero_bytes = page_zero_bytes;
		container->offset = offset;	

		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, file_backed_swap_in, container)) {
			return NULL;
		}
		
		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		offset += PGSIZE;
		addr += PGSIZE;
	}

	if(locked){
		locked = false;
		lock_release(&filesys_lock);
	}
	struct page *start_page = spt_find_page(spt, start_addr);
	start_page->page_cnt = page_cnt;
	list_push_back(&spt->mmap_list, &start_page->mmap_elem);
	// printf("start_addr : %p\n", start_addr);
	return start_addr;
}


/* Do the munmap */
void
do_munmap (void *addr) {
	ASSERT(addr == pg_round_down(addr));

	struct supplemental_page_table *spt = &thread_current()->spt;
	struct container *container;
	uint64_t *pml4 = thread_current()->pml4;
	struct file *file;
	void *buffer = addr;
	bool locked = false;
	int page_cnt = 0;

	struct page *page = spt_find_page(spt, addr);
	if (!page)
		return;
	page_cnt = page->page_cnt;

	if (!lock_held_by_current_thread(&filesys_lock)){
		locked = true;
		lock_acquire(&filesys_lock);
	}

	for (int i = 0; i < page_cnt; i++){
		page = spt_find_page(spt, pg_round_down(addr));
		container = page->file.aux;
		file = container->file;

		if (page->frame){
			if (pml4_is_dirty(pml4, addr)){
				file_write_at(file, addr, container->page_read_bytes, container->offset);
			}

			/* remove page from pml4 */
			pml4_clear_page(pml4, addr);
		}

		addr += PGSIZE;
	}

	if (locked){
		locked = true;
		lock_release(&filesys_lock);
	}
}
