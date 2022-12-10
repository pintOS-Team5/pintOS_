/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include <round.h>
#include <debug.h>


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
	struct thread* curr = thread_current();
	// printf("file_back_dest\n");
	struct file_page *file_page UNUSED = &page->file;
	// printf("page->va :%X", page->va);
	// file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->offset);
	// printf("file :%X, va:%X, prb: %d, offset:%d\n", page->file.file, page->va, page->file.page_read_bytes, page->file.offset);
	if (pml4_is_dirty(curr->pml4, page->va) && page->writable==true){
		file_write_at(page->file.file, page->va, page->file.page_read_bytes, page->file.offset);
	}
	pml4_clear_page(thread_current()->pml4, page->va);
	free(page->frame);
	
	// 아래는 어디서 해주지?
	// spt_remove_page(&curr->spt, page);
	
	// free(page->frame);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	size_t read_bytes = file_length(file)-offset;
	size_t zero_bytes = (ROUND_UP(read_bytes, PGSIZE) - read_bytes);

	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
   	ASSERT (pg_ofs (addr) == 0);
   	ASSERT (offset % PGSIZE == 0);

	size_t alloc_length = ROUND_UP(length, PGSIZE);	
	void *read_addr = addr;

	// printf("do mmap alloc_lenth : %d, read_bytes : %d, zero_bytes :%d\n", alloc_length, read_bytes, zero_bytes);	
	while (alloc_length !=0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      	size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct load_segment_passing_args* aux = (struct load_segment_passing_args*)malloc(sizeof(struct load_segment_passing_args));
		aux->file= file_reopen(file); // 매핑에 대해 개별적이고 독립적인 참조
      	aux->ofs = offset;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		// printf("read_addr :%X offset:%d prb : %d, pzb :%d\n", read_addr, offset, page_read_bytes, page_zero_bytes);
		
		if (!vm_alloc_page_with_initializer (VM_FILE, read_addr,
			writable, lazy_load_segment, aux))
			return false;

		if(!(read_bytes == 0 && zero_bytes == 0)){
			read_bytes -= page_read_bytes;
			zero_bytes -= page_zero_bytes;
			offset += page_read_bytes;
		}
		read_addr += PGSIZE;
		alloc_length -= PGSIZE;
	}
	// 시작지점 체크
	// struct page *page= spt_find_page(&thread_current()->spt, addr);
	// page->file.is_start=true;
	// printf("do mmap end\n"); 
}

/* Do the munmap */
void
do_munmap (void *addr) {
	void* write_addr = addr;
	struct page* page = NULL;
	// printf("do_munmap :%X\n", addr);
	while(1){
		page = spt_find_page(&thread_current()->spt, write_addr);
		if(page == NULL){
			break;
		}
		if(page->vm_type != VM_TYPE(VM_FILE) 
			&& !( page->vm_type == VM_TYPE(VM_UNINIT) && page->uninit.type == VM_TYPE(VM_FILE) ))
			// printf("here!!\n");
			break;
					
		if(page->vm_type == VM_TYPE(VM_FILE)){
			// file_write_at(page->file.file, page->va, page->file.page_read_bytes, page->file.offset);
			spt_remove_page(&thread_current()->spt, page); 

			// printf("page->va :%X", page->va);
			// printf("file :%X, page->va :%X, prb: %d, pzb : %d, file_offset :%d\n",page->file.file, page->va, page->file.page_read_bytes,page->file.page_zero_bytes , page->file.offset);
			// file_seek(page->file.file, page->file.offset);
			// memset(page->va, 0, page->file.page_read_bytes);
			// printf("reset!!!\n");
			// printf("%s\n", page->va);
			// file_read_at(page->file.file, page->va, page->file.page_read_bytes, page->file.offset);
			// printf("read!!!\n");
			// printf("%s\n", page->va);
			// printf("file_write_end\n");
		}
		else if(page->vm_type == VM_TYPE(VM_UNINIT) && page->uninit.type == VM_TYPE(VM_FILE)){
			// printf("uninit todo\n");
			spt_remove_page(&thread_current()->spt, page); 
		}
		else{
			break;
		}
		write_addr+= PGSIZE;
	}
	// printf("munmap_end!!!\n");
}
