/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/mmu.h"
#include <round.h>

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
	// printf("file_backed_initializer\n");
	page->operations = &file_ops;
	page->vm_type = VM_TYPE(type);
	struct file_page *file_page = &page->file;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	printf("file_back_dest\n");
	struct file_page *file_page UNUSED = &page->file;

	
	printf("page->va :%X", page->va);
	// file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->offset);
	file_write_at(page->file.file, page->va, page->file.page_read_bytes, page->file.offset);
		// pml4_clear_page(thread_current()->pml4, page->va);
	free(page);

}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	size_t alloc_length = ROUND_UP(length, PGSIZE);
	void * alloc_addr = addr;
	while(alloc_length != 0){
		// printf("alloc_length:%d\n", alloc_length);
		// printf("mmap writable : %d\n", writable);
		vm_alloc_page(VM_FILE, alloc_addr, writable);
		alloc_addr += PGSIZE;
		alloc_length -= PGSIZE;
	}

	size_t read_bytes = file_length(file)-offset;
	size_t zero_bytes = (ROUND_UP(read_bytes, PGSIZE) - read_bytes);

	// printf("read_bytes : %d, zero_bytes :%d\n", read_bytes, zero_bytes);
	
	void *read_addr = addr;
	struct page* page = NULL;
	while (read_bytes > 0 || zero_bytes > 0) {
		page = spt_find_page(&thread_current()->spt, read_addr);
		// printf("page-> va : %X read_addr : %X\n", page->va, read_addr);
		struct file_page *file_page = &page->file;
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      	size_t page_zero_bytes = PGSIZE - page_read_bytes;

		if(file_read_at(file, read_addr, page_read_bytes, offset) != page_read_bytes){
			printf("read_bytes error\n");
		}
		memset(read_addr + page_read_bytes, 0, PGSIZE-page_zero_bytes);

		file_page->file = file;
		file_page->offset = offset;
		file_page->page_read_bytes = page_read_bytes;
		file_page->page_zero_bytes = page_zero_bytes;

		
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		read_addr += PGSIZE;
		offset += page_read_bytes;
	}
	// 시작지점 체크
	page = spt_find_page(&thread_current()->spt, addr);
	page->file.is_start=true;
	
}
#include "vm/vm.h"
/* Do the munmap */
void
do_munmap (void *addr) {
	void* write_addr = addr;
	struct page* page = NULL;
	while(1){
		struct page* page = spt_find_page(&thread_current()->spt, write_addr);
		if(page == NULL){
			break;
		}
		destroy(page);
		// if(page->vm_type != VM_TYPE(VM_FILE) 
		// 	|| ( page->vm_type != VM_TYPE(VM_UNINIT) || page->uninit.type != VM_TYPE(VM_FILE) ))
		// 	break;
			
		// if(page->vm_type == VM_TYPE(VM_FILE)){
		// 	// printf("page->va :%X", page->va);
		// 	file_write_at(page->file.file, page->va, page->file.page_read_bytes, page->file.offset);
		// }
		// else if(page->vm_type == VM_TYPE(VM_UNINIT) && page->uninit.type == VM_TYPE(VM_FILE)){
		// 	printf("uninit todo\n");
		// }
		// else{
		// 	break;
		// }
		write_addr+= PGSIZE;
	}
	printf("munmap_end!!!\n");
	
	

}
