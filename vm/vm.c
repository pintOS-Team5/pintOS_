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

enum vm_type
page_get_union_type(struct page *page) {
    enum vm_type type = page_get_type(page);
    if(VM_TYPE (page->operations->type) != VM_UNINIT) {
        switch(type){
            case VM_ANON:
                type = page->anon.type;
                break;
            case VM_FILE:
                type = page->file.type;
                break;
            default:
                break;
        }
    }
    return type;
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
				printf("PAGE TYPE ERROR in vm_alloc_page_with_initializer\n");
				goto err;
			}
			new_page->writable = writable;
			new_page->pml4 = thread_current()->pml4;
			/* TODO: Insert the page into the spt. */
			if(!spt_insert_page(spt, new_page)){
				goto err;
			}

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

struct frame*
ft_find_frame (void *kva){
	struct frame * result_frame = NULL;
	struct hash_elem * e;
	struct frame f;
	bool locked = false;

	f.kva = kva;
	if (!lock_held_by_current_thread(&frames_lock)){
		locked = true;
		lock_acquire(&frames_lock);
	}
	e = hash_find(&frames, &f.hash_elem);
	if (locked){
		locked = false;
		lock_release(&frames_lock);
	}
	if (e)
		result_frame = hash_entry(e, struct frame, hash_elem);
	return result_frame;
}

bool
ft_insert_frame(struct frame *frame){
	bool succ = false;
	bool locked = false;
	if(!lock_held_by_current_thread(&frames_lock)){
		locked = true;
		lock_acquire(&frames_lock);
	}
	succ = hash_insert(&frames, &frame->hash_elem) == NULL;
	if (locked){
		locked = false;
		lock_release(&frames_lock);
	}
	return succ;
}

void
ft_remove_frame(struct frame *frame) {
	bool locked = false;
	if(!lock_held_by_current_thread(&frames_lock)){
		locked = true;
		lock_acquire(&frames_lock);
	}
	hash_delete(&frames, &frame->hash_elem);
    if (locked){
		locked = false;
		lock_release(&frames_lock);
	}
}

uint64_t 
frame_hash (const struct hash_elem *f_, void *aux UNUSED) {
    const struct frame *f = hash_entry (f_, struct frame, hash_elem);
    return hash_bytes (&f->kva, sizeof f->kva);
}

bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
    const struct frame *a = hash_entry (a_, struct frame, hash_elem);
    const struct frame *b = hash_entry (b_, struct frame, hash_elem);
    return a->kva < b->kva;
}

void
frame_table_init(void){
	lock_init(&frames_lock);
	hash_init(&frames, frame_hash, frame_less, NULL);
}



/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	struct hash_iterator ft_iter;
	struct frame *f;
	bool victim_is_dirty = true;
	bool locked = false;

	if(!lock_held_by_current_thread(&frames_lock)){
		locked = true;
		lock_acquire(&frames_lock);
	}
	hash_first(&ft_iter, &frames);
	while (hash_next(&ft_iter)){
		f = hash_entry(hash_cur(&ft_iter), struct frame, hash_elem);

		ASSERT(f->page != NULL);

		uint64_t *f_pml4 = f->page->pml4;

		if (pml4_is_accessed(f_pml4, f->page->va)){
			pml4_set_accessed(f_pml4, f->page->va, false);
			if (victim_is_dirty && !pml4_is_dirty(f_pml4, f->page->va)){
				victim_is_dirty = false;
				victim = f;
			}
		}else{
			victim_is_dirty = false;
			victim = f;
		}
	}
	if (locked){
		locked = false;
		lock_release(&frames_lock);
	}

	// frames에서 매핑된 page와의 연결을 끊을 frame을 찾아야 한다.
	// dirty한 페이지와 매핑된 frame을 꺼내게 되면 swapout해줘야 하므로
	// 최대한 dirty하지 않은 Page와 매핑되어 있ㅈ는 frame을 찾음
	// 그럼 frame이 없다면 dirty한 페이지->frame을 꺼낸다.
	if (victim == NULL && f != NULL){
		victim = f;
	}
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */
	ASSERT(victim != NULL);
	ASSERT(victim->page != NULL);

	swap_out(victim->page);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc(sizeof (struct frame));
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER|PAL_ZERO);
	if (frame->kva == NULL){
		free(frame);
		frame = vm_evict_frame();
	}else{
		ft_insert_frame(frame);
	}
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
	{
		return false;
	}

	if (is_kernel_vaddr(addr))
		return false;

	if (user)
		stack_pointer = f->rsp;
	else
		stack_pointer = thread_current()->stack_pointer;

	page = spt_find_page(spt, pg_aligned_addr);
	if (!page){
		if (stack_pointer - 8 <= addr && MAX_STACK < addr && addr <= USER_STACK){
			vm_stack_growth(pg_aligned_addr);
			return true;
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
	bool result = false;

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)){
		return false;
	}
	result = swap_in(page, frame->kva);
	return result;
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
			container->file = file_reopen(((struct container*)p->uninit.aux)->file);
			container->offset = ((struct container *)p->uninit.aux)->offset;
			container->page_read_bytes = ((struct container *)p->uninit.aux)->page_read_bytes;
			container->page_zero_bytes = ((struct container *)p->uninit.aux)->page_zero_bytes;

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

	hash_clear(h, clear_func);
}

