/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "lib/stdio.h"

struct frame_table frame_table;

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

	frame_init();
}

unsigned frame_hash (const struct hash_elem *f_, void *aux UNUSED);
bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

frame_init(void) {
	hash_init(&frame_table.hash, frame_hash, frame_less, NULL);
	lock_init(&frame_table.ft_lock);
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
	ASSERT (VM_TYPE(type) != VM_UNINIT);
	struct page *new_page = NULL;
	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check whether the upage is already occupied or not. */
	// va 할당 안받은경우
	new_page = spt_find_page (spt, upage);
	if (new_page == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		/* TODO: Insert the page into the spt. */
		/* 페이지를 생성하고 VM 유형에 따라 초기값을 가져온 다음 uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다. 
			uninit_new를 호출한 후 필드를 수정해야 합니다. 페이지를 spt에 삽입합니다.*/
		new_page = (struct page *)malloc(sizeof(struct page));

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			uninit_new(new_page, upage, init, type, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
			break;
		}
		if (!spt_insert_page(spt, new_page))
			goto err;
	}
	new_page->writable = writable;
	new_page->vm_type = VM_TYPE(VM_UNINIT);

	return true;
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* TODO: Fill this function. */
	struct page *p = NULL;
  	struct hash_elem *e = NULL;
	struct page page;
	page.va = pg_round_down(va);

	e = hash_find (&spt->hash, &page.hash_elem);
	if (e)
		p = hash_entry(e, struct page, hash_elem);
	return p;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page ) {
	int succ = false;
	/* TODO: Fill this function. */
	if (hash_insert(&spt->hash, &page->hash_elem) == NULL)
		succ = true;
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}


/* Find VA from spt and return page. On error, return NULL. */
struct frame *
ft_find_page (struct frame_table *ft UNUSED, void *kva UNUSED) {
	/* TODO: Fill this function. */
	struct frame *f = NULL;
  	struct hash_elem *e = NULL;
	struct frame frame;
	frame.kva = kva;

  	e = hash_find (&ft->hash, &frame.hash_elem);
	if (e)
		f = hash_entry(e, struct frame, hash_elem);
	return f;
}

/* Insert PAGE into spt with validation. */
bool
ft_insert_page (struct frame_table *ft,
		struct frame *frame ) {
	int succ = false;
	/* TODO: Fill this function. */
	if (hash_insert(&ft->hash, &frame->hash_elem) == NULL)
		succ = true;
	return succ;
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
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	// page_fault가 발생한 page의 frame을 반환?!
	frame = (struct frame *)malloc(sizeof(struct frame));
	frame->kva =  palloc_get_page(PAL_USER | PAL_ZERO);
	frame->page = NULL;
	if (frame == NULL || frame->kva == NULL){
		PANIC("todo");
	}

	ASSERT(is_kernel_vaddr(frame->kva)); // 추가한 검증
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	vm_claim_page(addr);//frame이랑 연결하고
	thread_current()->stack_bottom -= PGSIZE;//한 페이지만큼 stack bottom 내려주고
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	//유저 스페이스를 가리키고 있어야 함
	if (is_kernel_vaddr(addr))
		return false;

	page = spt_find_page(spt, pg_round_down(addr));
	if (page != NULL) {
		return vm_do_claim_page(page);
	}

	// 유저 주소를 가르키고 있으면 tf->rsp 그대로 사용
	void * stack_pointer = f->rsp;
	//커널 주소를 가리키고 있으면 syscall_handler에서 직접 thread 구조체에 넣어준 값으로
	if (is_kernel_vaddr(f->rsp)) 
		stack_pointer = thread_current()->stack_pointer; 
	
	// printf("fault==========\n");
	// printf("fault_addr : %X stack_pointer :%X f->rsp : %X thread_current()->stack_pointer : %X\n", addr, stack_pointer, f->rsp, thread_current()->stack_pointer);

	// frame과 연결이 되지 않으면
	if (addr <= USER_STACK && addr >= USER_STACK - 0x100000  && addr >= pg_round_down(stack_pointer)){
		vm_stack_growth(addr);
		return true;
	}

	return false;
}


// bool
// is_vm_stack(void* addr){
// 	printf("stack_growth start\n");
// 	struct intr_frame *f = &thread_current()->tf;
// 		printf("right : %d\n", addr <= USER_STACK && addr >= USER_STACK - 0x100000 && addr >= pg_round_down(f->rsp));

// 	if (addr <= USER_STACK && addr >= USER_STACK - 0x100000 && addr >= pg_round_down(f->rsp)){
// 		printf("stack_growth_work");
// 	// if (addr <= USER_STACK && addr >= USER_STACK - 0x100000){
// 		return true;
// 	}
// 	else{
// 		return false;
// 	}
// }


/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}




/* Claim the page that allocate on VA. */
// va를 할당할 페이지를 요청합니다.
bool
vm_claim_page (void *va UNUSED) {
	/* TODO: Fill this function */
	struct thread *curr = thread_current();
	struct page *page = NULL;	
	va = pg_round_down(va);
	
	ASSERT(pg_ofs (va) == 0 ); // 추가한 검증

	vm_alloc_page(VM_ANON, va, true);
	page = spt_find_page(&curr->spt, va);
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	struct thread *curr = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 페이지 테이블 항목을 삽입하여 페이지의 VA를 프레임의 PA에 매핑합니다.
	pml4_set_page(curr->pml4, page->va, frame->kva, true);
	return swap_in (page, frame->kva);
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->va < b->va;
}

/* Returns a hash value for page p. */
unsigned
frame_hash (const struct hash_elem *f_, void *aux UNUSED) {
  const struct frame *f = hash_entry (f_, struct frame, hash_elem);
  return hash_bytes (&f->kva, sizeof f->kva);
}

/* Returns true if page a precedes page b. */
bool
frame_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct frame *a = hash_entry (a_, struct frame, hash_elem);
  const struct frame *b = hash_entry (b_, struct frame, hash_elem);

  return a->kva < b->kva;
}
/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->hash, page_hash, page_less, NULL);
	lock_init(&spt->spt_lock);
}


/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;
   	hash_first (&i, &src->hash);
	// printf_hash(src);
   	while (hash_next (&i))
   	{	
		struct page *sp = hash_entry(hash_cur(&i), struct page, hash_elem);
		// printf("type: %d", sp->vm_type);
		vm_initializer *init = NULL;
		// 부모 page가 UNINIT
		// if( sp->frame == NULL){
		switch (VM_TYPE(sp->vm_type))
		{
		case VM_UNINIT:
			init = sp->uninit.init;
			struct load_segment_passing_args* aux = (struct load_segment_passing_args*)malloc(sizeof(struct load_segment_passing_args));
			memcpy(aux, sp->uninit.aux, sizeof(struct load_segment_passing_args));
			vm_alloc_page_with_initializer(sp->uninit.type, sp->va, sp->writable, init, aux);
			break;
		
		// 부모 page가 ANON/FILE
		case VM_ANON:
			vm_claim_page(sp->va);
			struct page *dp = spt_find_page(dst, sp->va);
			dp->writable = sp->writable;
			memcpy(dp->frame->kva, sp->frame->kva, PGSIZE);
			break;
		}
	}
	return true;
}

void page_free_helper(struct hash_elem *e, void *aux){
	struct page *p= hash_entry(e, struct page, hash_elem);
	if (p->frame){
		free(p->frame);
	}
	free(p);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->hash, page_free_helper);

}


bool 
is_stack(struct page * page){
	if(page->vm_type == VM_STACK_MARKER){
		return true;
	}
	else{
		return false;
	}
}


void
printf_hash(struct supplemental_page_table *spt){
	struct hash *h = &spt->hash;
	struct hash_iterator i;
   	hash_first (&i, h);
	printf("===== hash 순회시작 =====\n");
   	while (hash_next (&i))
   	{
		struct page *p = hash_entry(hash_cur(&i), struct page, hash_elem);
		if (p->frame == NULL){
			printf("va: %X, writable : %X\n",p->va, p->writable);
		}
		else {
			printf("va: %X, kva : %X, writable : %X\n",p->va,p->frame->kva, p->writable);
		}
   	}
	printf("===== hash 순회종료 =====\n");
}