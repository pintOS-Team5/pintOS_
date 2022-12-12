/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// swap_disk = NULL;
	swap_disk = disk_get(1, 1);
	
	
	// printf("size :%d\n", disk_size(swap_disk));
	// disk 테스트
	// printf("anon_init\n");
	// char test[] = "hello\n";
	// void *test2;
	
	// disk_write(swap_disk,0, &test);
	// // disk_write(swap_disk,1, &test);
	// disk_read(swap_disk, 0, test2);
	// printf("%s\n", test2);
	// disk_print_stats();
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// printf("anon vm_type: %d\n", type);
	// printf("anon page->va : %X page->vm_type : %d\n", page->va, page->vm_type);
	page->operations = &anon_ops;
	// page->vm_type = type;
	// printf("anon_initializer\n");
	// printf_hash_page(&thread_current()->spt);
	
	
	struct anon_page *anon_page = &page->anon;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// printf("anon swap_in========\n"); 
	struct page *p = page;
	// printf("va: %X, type : %d vm_type:%d writable : %d kva : %X\n",p->va, p->operations->type, p->vm_type, p->writable, p->frame->kva);
	struct anon_page *anon_page = &page->anon;
	void *disk_sector = anon_page->disk_sector;
	// printf("read disk_sector : %d \n", disk_sector);
	// void *in_addr = kva;
	// for(int i =0; i<8; i++){
	// 	// printf("in_addr :%X", in_addr);
	// 	disk_read(swap_disk, (disk_sector_t)disk_sector+i, in_addr);
	// 	in_addr += DISK_SECTOR_SIZE;
	// }
	write_swap_disk(swap_disk, (disk_sector_t)disk_sector, kva);
	// printf("\n");
	set_swap_table_bit((disk_sector_t)disk_sector, false);	
	// printf("set_bit end\n");
	page->anon.disk_sector = NULL;
	page->swap_out_yn = false;
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	// printf("anon swap_out========\n"); 
	struct page *p = page;  
	// printf("va: %X, type : %d vm_type:%d writable : %d kva : %X\n",p->va, p->operations->type, p->vm_type, p->writable, p->frame->kva);
	// disk_print_stats();
	struct anon_page *anon_page = &page->anon;
	void *disk_sector= get_empty_swap_disk_sector();
	// void *out_addr = page->va;
	// printf("write disk_sector : %d \n", disk_sector);
	// for(int i =0; i<8; i++){
	// 	lock_acquire(&swap_disk_table.swap_disk_lock);
	// 	disk_write(swap_disk,(disk_sector_t)disk_sector+i, out_addr);
	// 	out_addr += DISK_SECTOR_SIZE;  
	// }
	read_swap_disk(swap_disk, (disk_sector_t)disk_sector, page->frame->kva); 
	page->anon.disk_sector = disk_sector;

	pml4_clear_page(thread_current()->pml4, page->va);
	page->swap_out_yn = true;
	page->frame = NULL;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	// printf("anon_destory\n");
	// printf("anon page->va : %X kva :%X\n", page->va, page->frame->kva);
	struct anon_page *anon_page = &page->anon;
	// printf_hash_page(&thread_current()->spt);
	if(page->frame == NULL){
		PANIC("!!!! NOT ANON!!!!!!");
	}
	// pml4_clear_page(thread_current()->pml4, page->va);


	//TODO: stack의 Frame Free 어떻게 하지?
	if((page->vm_type & VM_STACK_MARKER) != VM_STACK_MARKER){
		// printf("anon page->va : %X kva :%X\n", page->va, page->frame->kva);
		free(page->frame);
	}
	else{
		// printf("anon page->va : %X kva :%X\n", page->va, page->frame->kva); 
		// free(page->frame);
	}
	
	page->frame = NULL;

}
