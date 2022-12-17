/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <hash.h>
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/anon.h"
#include "vm/file.h"
#include "userprog/process.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	upage = pg_round_down(upage);
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
			struct page *new_page = (struct page *)malloc(sizeof(struct page));
			if (!new_page)
				goto err;
			switch (VM_TYPE(type))
			{
			case VM_ANON:
				uninit_new(new_page, upage, init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
				break;
			default:
				// PANIC("NOT YET IMPLEMENT IN VM_ALLOC_INITIALIZER");
				printf("PAGE TYPE ERROR in vm_alloc_page_with_initializer\n");
				goto err;
			}
			new_page->writable = writable;

			/* TODO: Insert the page into the spt. */
			if(!spt_insert_page(spt, new_page)){
				goto err;
			}

		// /* Claim immediately if the page is the first stack page. */
		// 	if (type & VM_MARKER_0){
		// 		return vm_do_claim_page(new_page);
		// 	}

		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page dummy_page;
	/* TODO: Fill this function. */
	struct hash_elem *e;
	dummy_page.va = va;
	e = hash_find(&spt->pages, &dummy_page.hash_elem);

	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	if (!hash_insert(&spt->pages, &page->hash_elem))
		succ = true;
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	struct hash_elem * result =	hash_delete (&spt->pages, &page->hash_elem);
	if (result == NULL) {
		return false; 
	}
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc(sizeof (struct frame));
	/* TODO: Fill this function. */
	void *kva = palloc_get_page(PAL_USER|PAL_ZERO);
	if (kva == NULL){
		thread_current()->my_exit_code = -1;
		thread_exit();
		// PANIC("No memory in physical memory IN VM_GET_FRAME");
	}
	frame->kva = kva;
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	void *pg_aligned_addr =  pg_round_down(addr);

	while (!spt_find_page(spt, pg_aligned_addr)){
		vm_alloc_page(VM_ANON | VM_MARKER_0, pg_aligned_addr, true);
		vm_claim_page(pg_aligned_addr);
		pg_aligned_addr += PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	uint64_t MAX_STACK = USER_STACK - (1 << 20);
	void *pg_aligned_addr = pg_round_down(addr);
	void *stack_pointer = NULL;

	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if ((!not_present) && write)
		return false;

	if (is_kernel_vaddr(addr))
		return false;

	if (user)
		stack_pointer = f->rsp;
	else
		stack_pointer = thread_current()->stack_pointer;

	page = spt_find_page(spt, pg_aligned_addr);
	if (!page){
		if (stack_pointer - 8 <= addr && MAX_STACK < addr && addr < USER_STACK){
			vm_stack_growth(pg_aligned_addr);
			page = spt_find_page(spt, pg_aligned_addr);
		}
		else{
			return false;
		}
	}
	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, pg_round_down(va));
	if (page)
		return vm_do_claim_page(page);
	return false;
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)){
		// printf("SET ERROR\n");
		return false;
	}
	// printf("SWAP_IN BEOFRE\n");
	return swap_in(page, frame->kva);
}

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->va < b->va;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->pages, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {

	struct hash_iterator i;
	struct hash *h = &src->pages;

	hash_first(&i, h);
	while(hash_next(&i)){
		struct page *p = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type type = page_get_type(p);

		void *va = p->va;
		bool writable = p->writable;

		if (p->operations->type == VM_UNINIT){
			vm_initializer *init = p->uninit.init;
			struct container *container = (struct container *)malloc(sizeof(struct container));
			container = p->uninit.aux;

			if (!vm_alloc_page_with_initializer(type, va, writable, init, container))
				return false;
		}
		else{
			if (!vm_alloc_page(type, va, writable))
				return false;
			if (!vm_claim_page(va))
				return false;
			memcpy(va, p->frame->kva, PGSIZE);
		}
	}
	return true;
}

void clear_func (struct hash_elem *elem, void *aux) {
	struct page *page = hash_entry(elem, struct page, hash_elem);
	vm_dealloc_page(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	struct list *mmap_list = &spt->mmap_list;
	while (!list_empty(mmap_list)){
		struct page *page = list_entry (list_pop_front (mmap_list), struct page, mmap_elem);
		do_munmap(page->va);
	}

	struct hash *h = &spt->pages;

	// hash_destroy (h, clear_func);
	// hash_init (h, page_hash, page_less, NULL);
	hash_clear(h, clear_func);
}
