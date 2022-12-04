/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"

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
	printf("vm_alloc_page_with_initializer start\n");
	ASSERT (VM_TYPE(type) != VM_UNINIT);
	struct page *new_page = NULL;
	// = palloc_get_page(PAL_USER | PAL_ZERO);
	struct supplemental_page_table *spt = &thread_current ()->spt;
	printf("thread_name : %s\n", thread_name());
	// printf_hash(spt);

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
		// uninit_new(new_page, upage, init ,VM_UNINIT, aux, new_page->uninit.page_initializer);
		if(VM_TYPE(type) == VM_ANON){
			uninit_new(new_page, upage, init, type, aux, anon_initializer);
		}
		if (!spt_insert_page(spt, new_page))
			goto err;
	}

	printf("vm_alloc_page_with_initializer end\n");
	return true;
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* TODO: Fill this function. */
	struct page *p = NULL;
	// for(int i = 0; i <100; i++){
	// 	if (spt->pages[i]->va == va){
	// 		// page = pml4_get_page(thread_current()->pml4, va);			
	// 		page = spt->pages[i];
	// 		break;
	// 	}
	// }
	printf("find_page start\n");
  	struct hash_elem *e = NULL;
	struct page page;
	page.va = va;


	lock_acquire(&spt->spt_lock);
  	e = hash_find (&spt->hash, &page.hash_elem);
	lock_release(&spt->spt_lock);
	if (e)
		// printf("hash exists\n");
		p = hash_entry(e, struct page, hash_elem);
		printf("find_page success\n");
		// printf("find element va = %d\n", p->va);
	return p;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page ) {
	int succ = false;
	/* TODO: Fill this function. */
	// for(int i = 0; i <100; i++){
	// 	if (spt->pages[i] == NULL){
	// 		spt->pages[i] = page;
	// 		succ = true;
	// 		break;
	// 	}
	// }
	lock_acquire(&spt->spt_lock);
	if (hash_insert(&spt->hash, &page->hash_elem) == NULL)
		printf("insert_success");
		succ = true;
	lock_release(&spt->spt_lock);
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
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
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	printf("start try_handle_fault\n");
	printf("thread_name : %s\n", thread_name());
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// 현재 항상 bogus fault로 동작
	printf("vm_try_handle_fault \n");
	printf("fault_addr :%X, user: %d, write : %d, not_present :%d\n", addr, user, write, not_present);
	// printf("page_addr :%X\n", pg_round_down(addr));
	// page = spt_find_page(spt, pg_round_down(addr));
	printf("page_addr :%X\n", pg_round_down(addr));
	printf("thread_name : %s, spt_addr : %X\n", thread_name(), &thread_current ()->spt.hash);
	lock_acquire(&spt->spt_lock);
	printf_hash(&spt->hash);
	lock_release(&spt->spt_lock);
	page = spt_find_page(&thread_current ()->spt, pg_round_down(addr));
	// printf_hash(spt);
	// lock_acquire(&spt->spt_lock);
	// printf_hash(&spt->hash);
	// lock_release(&spt->spt_lock);

	if (page != NULL){
		printf("---match---\n");
		printf("%X\n",page->va);
		return vm_do_claim_page (page);
		// printf("end try_handler_fault\n");
		// return true;
	}
	// else{
	// 	printf("im not here\n");
	// 	return false;
	// }


	
}

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
	// printf("vm_claim_page start\n");
	pg_round_down(va);
	vm_alloc_page(VM_ANON, va, true);
	page = spt_find_page(&curr->spt, va);
	printf("page->va : %X\n", page->va);

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	printf("vm_do_claim_page start\n");
	struct frame *frame = vm_get_frame ();
	struct thread *curr = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 페이지 테이블 항목을 삽입하여 페이지의 VA를 프레임의 PA에 매핑합니다.
	printf("page->va : %X\n", page->va);
	printf("frame->kva : %X\n", frame->kva);
	printf("page->type : %X\n", page->uninit.type);

	pml4_set_page(curr->pml4, page->va, frame->kva, true);

	// spt_insert_page(&thread_current()->spt, page);
	// printf("memcpy::::::\n");
	// memcpy(page->va, "12312", 20);

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

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// init pages
	// for(int i=0; i<100; i++){
	// 	spt->pages[i] = NULL;
	// }
	printf("spt init=========\n");
	// struct hash pages;
	// spt->hash = (struct hash *)malloc(sizeof (struct hash *));
	hash_init(&spt->hash, page_hash, page_less, NULL);
	// spt->hash = pages;
	lock_init(&spt->spt_lock);
	printf("spt init end=========\n");
	
	
	// // hash테스트
	// struct thread * curr = thread_current();
	// struct page *p=(struct page *)malloc(sizeof(struct page));
	// void *test_va = "1000";
	// p->va = test_va;

	// printf("thread_name : %s\n", thread_name());
	// spt_insert_page(spt, p);
	// printf_hash(&curr->spt);
	// struct page *p2 = spt_find_page(spt, test_va);
	// // p2 = spt_find_page(spt, test_va);
	// if(p2){
	// 	printf("result : %X\n", p2->va);
	// }
	//------------위에꺼만 사용
	// ASSERT(page == NULL);
	// printf("page: %d", page==NULL);
	

	// //테스트
	// struct thread * curr = thread_current();
	// void *test_va = "1000";
	// struct page *p;
	// // struct page *p=(struct page *)malloc(sizeof(struct page));
	// struct page tmp_p;
	// struct page tmp_p_2;
	// struct hash_elem *e;

	// tmp_p.va = test_va;
	// struct page *tmp_p_3 = &tmp_p;
	// spt_insert_page(&curr->spt, tmp_p_3);
	// printf_hash(&curr->spt);
	// p = spt_find_page(&curr->spt, test_va);
	// p = spt_find_page(&curr->spt, test_va); 
	// if(p){
	// 	printf("result : %X\n", p->va);
	// }
	
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
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
			printf("va: %X, p_addr : %X\n",p->va, p);
		}
		else {
			printf("va: %X, kva : %X, p_addr : %X\n",p->va,p->frame->kva, p);
		}
   	}
	printf("===== hash 순회종료 =====\n");
}