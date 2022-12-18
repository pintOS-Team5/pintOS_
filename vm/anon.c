/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */
#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include <bitmap.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);
size_t slot_cnt(void);

// used_slot의 비트 수
#define SLOT_CNT (slot_cnt()) 
// 한 슬롯당 섹터 수
#define SECTOR_PER_SLOT ((PGSIZE)/(DISK_SECTOR_SIZE))
// anon_paeg의 멤버 disk_sector_t의 초기값
#define SLOT_DEFAULTS -1
// 섹터 번호를 슬롯 번호로 바꿔줌
#define sector_to_slot(sec_no) ((disk_sector_t) ((sec_no) / (SECTOR_PER_SLOT)))
// 슬롯 번호를 섹터 번호로 바꿔줌. disk 관련 함수 실행 시 사용됨
#define slot_to_sector(slot_no) ((disk_sector_t)((slot_no) * (SECTOR_PER_SLOT)))

struct swap_table{
	struct lock lock;
	struct bitmap *used_slots;
};

struct swap_table swap_table;

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
	swap_disk = disk_get(1, 1);
	lock_init(&swap_table.lock);
	swap_table.used_slots = bitmap_create(SLOT_CNT);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {

	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->type = type;
	anon_page->aux = page->uninit.aux;
	anon_page->swap_slot_no = SLOT_DEFAULTS;
	return true;
}

// swap_disk의 크기에 따른 가능한 슬롯의 최대 갯수를 반환함.
size_t
slot_cnt(void){
	return disk_size(swap_disk) / SECTOR_PER_SLOT;
}

// 슬롯 할당기. SLOT_CNT개의 슬롯을 할당하고 할당 받은 슬롯번호들 중 가장 작은 슬롯 번호를 반환.
disk_sector_t
salloc_get_multiple(size_t slot_cnt){
	lock_acquire(&swap_table.lock);
	disk_sector_t slot_no = bitmap_scan_and_flip(swap_table.used_slots, 0, slot_cnt, false);
	lock_release(&swap_table.lock);
	return slot_no;
}

// 하나의 슬롯을 할당.
disk_sector_t
salloc_get_slot(void){
	return salloc_get_multiple(1);
}

// slot_no부터 시작하는 slot_cnt개의 슬롯을 free
void
salloc_free_multiple(disk_sector_t slot_no, size_t slot_cnt){
	lock_acquire(&swap_table.lock);
	bitmap_set_multiple(swap_table.used_slots, slot_no, slot_cnt, false);
	lock_release(&swap_table.lock);
}

// slot_no 슬롯을 free
void
salloc_free_slot(disk_sector_t slot_no){
	salloc_free_multiple(slot_no, 1);
}

void
write_to_swap_disk(disk_sector_t slot_no, void *upage){
	disk_sector_t sec_no = slot_to_sector(slot_no);
	for (int i = 0; i < SECTOR_PER_SLOT; i++)
	{
		disk_write(swap_disk, sec_no +i, upage);
		upage += DISK_SECTOR_SIZE;
	}
}

void
read_to_swap_disk(disk_sector_t slot_no, void *upage){
	disk_sector_t sec_no = slot_to_sector(slot_no);
	// printf("SEC_NO: %d\n", sec_no);
	for (int i = 0; i < SECTOR_PER_SLOT; i++)
	{
		disk_read(swap_disk, sec_no + i, upage);
		// printf("FOR STATEMENT : %d\n", i);
		upage += DISK_SECTOR_SIZE;
	}
	// printf("for done\n");
}

// JH: swap_in() 이 호출된느 시점은 vm_claim_page() 또는 vm_do_claim_page()를 호출하는 시점이고
// 위의 두 claim 함수를 호출하는 시점은 
	// vm_try_handle_fault()
	// supplemental_page_table_copy()

// swap_in 되는 2가지 경우
// 1. uninit 타입 페이지 경우, 아직 물리메모리에 매핑된 적 없는 페이지이므로
// 처음으로 frame을 할당받을 것이며, 이는 백업된 내용이 없다는 뜻이다.
// 따라서 "물리 메모리에 적어줄 내용"이 없다는 것이므로
// uninit_initializer() 라는 함수를 operation의 swap_in 멤버룰 사용했다.

// 2. uninit 타입이 아닌 페이지의 경우, 한번 물리메모리에 매핑된 적이 있었다는 의미이고
// 이런 페이지에 대해 swap_in 요청이 들어왔다는 것은 
// 해당 페이지가 물리메모리에 한 번 이상 매핑된 이후, swap_out 된 적 있다는 의미이다.
// 따라서 이런 페이지들은 swap_out 당한 페이지를 별도로 관리하는 테이블에서 관리되다가
// swap_in 요청시 그 테이블에서 제거되고
// 백업에 사용했던 디스크 영역의 내용을 다시 해당 페이지와 연결된 물리메모리에 옮겨 적어줘야 한다.

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// printf("SWAP IN\n");
	struct anon_page *anon_page = &page->anon;
	disk_sector_t slot_no = anon_page->swap_slot_no;

	if(slot_no != SLOT_DEFAULTS){
		read_to_swap_disk(slot_no, kva);
		salloc_free_slot(slot_no);
		anon_page->swap_slot_no = SLOT_DEFAULTS;
		return true;
	}
	return false;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct frame *frame = page->frame;
	disk_sector_t slot_no = salloc_get_slot();
	// printf("SLOT_NO: %d\n", slot_no);

	ASSERT(frame != NULL);

	// 할당받은 slot_no을 저장하고 메모리의 내용을 swap_disk에 백업
	anon_page->swap_slot_no = slot_no;
	write_to_swap_disk(slot_no, frame->kva);

	// 사용한 물리 메모리 영역 초기화
	memset(frame->kva, 0, PGSIZE);
	pml4_clear_page(page->pml4, page->va);

	// frame - page 연결관계 제거
	frame->page = NULL;
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	struct frame *frame = page->frame;
	void * aux  = anon_page->aux;
	disk_sector_t slot_no = anon_page->swap_slot_no;

	if (frame == NULL || aux == NULL){
		// goto end;
		return;
	}

	memset(frame->kva, 0, PGSIZE);
	if (slot_no != SLOT_DEFAULTS){
		write_to_swap_disk(slot_no, frame->kva);
		salloc_free_slot(slot_no);
	}

	if ((anon_page->type & VM_MARKER_0)){
		pml4_clear_page(thread_current()->pml4, page->va);
		palloc_free_page(frame->kva);
	}

	ft_remove_frame(frame);
	frame->page = NULL;
	page->frame = NULL;
	free(frame);

// end:
// 	free(aux);
}