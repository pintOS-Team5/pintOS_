/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) {
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/* Initalize the page on first fault */
static bool
uninit_initialize (struct page *page, void *kva) {
	// printf("uninit initialzier\n"); 
	struct uninit_page *uninit = &page->uninit;
	/* Fetch first, page_initialize may overwrite the values */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;
	// printf("va: %X, uninit->type: %d\n", page->va, uninit->type);


	// if(VM_TYPE(uninit->type) == VM_ANON){
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
	// }	

	// else if(VM_TYPE(uninit->type) == VM_FILE){
	// 	// struct file_page* file_page = (struct file_page*)aux;
	// 	// void *addr = mmap_passing_args->addr;
	// 	// size_t length = mmap_passing_args->length;
	// 	// int writable = mmap_passing_args->writable;
	// 	// struct file *file = mmap_passing_args->file;
	// 	// off_t offset = mmap_passing_args->offset;
	// 	struct uninit_page *uninit_mal = (struct uninit_page *) malloc(sizeof(struct uninit_page));
	// 	memcpy(uninit_mal, uninit, sizeof(struct uninit_page));
	// 	memcpy(&page->file, aux, sizeof(struct file_page));
	// 	free(aux);
	// 	// printf("page->file->file:%X, page->file->offset : %d, r_b : %d, z_b: %d, is_start: %d\n", page->file.file, page->file.offset, page->file.page_read_bytes, page->file.page_zero_bytes, page->file.is_start);

	// 	uninit_mal->page_initializer(page, VM_FILE, kva);
	// 	free(uninit_mal);
	// 	return true;
	// 	// return uninit->page_initializer (page, VM_FILE, kva) &&
	// 	// (init ? init (page, aux) : true);
	// }
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
	switch (VM_TYPE(page->uninit.type))
	{
		case VM_ANON:
			free(page);
			break; 
		case VM_FILE: 
			break;
	}
	return;
}
