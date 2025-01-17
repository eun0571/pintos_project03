/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
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
	frame_table_init ();
	swap_table_init ();
}

struct list frame_table;

void
frame_table_init (void) {
	list_init(&frame_table);
}

// void insert_frame_table (struct page *page) {
// 	list_push_back(&frame_table, &page->frame->elem);
// }

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

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = (struct page *)calloc(1,sizeof(struct page));

		bool (*initializer)(struct page *, enum vm_type, void *kva);

		if (VM_TYPE(type) == VM_ANON) {
			initializer = anon_initializer;
		} else if (VM_TYPE(type) == VM_FILE) {
			initializer = file_backed_initializer;
		}

		uninit_new(page, upage, init, type, aux, initializer);

		page->writable = writable;

		/* TODO: Insert the page into the spt. */
		if (!spt_insert_page(spt, page))
			goto err;

		return true;
	} else {
		goto err;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct page page_;
	page_.va = pg_round_down(va);
	struct hash_elem *elem_ = hash_find(&spt->hash_table, &page_.elem);
	if (elem_ != NULL)
		page = hash_entry(elem_, struct page, elem);

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	if (hash_insert(&spt->hash_table, &page->elem) == NULL)
		succ = true;

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
	victim = list_entry(list_pop_front(&frame_table),struct frame, elem);
	
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if (!swap_out(victim->page))
		return NULL;
	victim->page = NULL;

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	uint64_t *kva = palloc_get_page(PAL_USER);
	if (kva == NULL) {
		frame = vm_evict_frame();
	} else {
		frame = (struct frame *)calloc(1, sizeof(struct frame));
		frame->kva = kva;
	}

	list_push_back(&frame_table, &frame->elem);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	uintptr_t align = pg_round_down(addr);
	for (; align < USER_STACK - PGSIZE; align += PGSIZE) {
		if (!spt_find_page(&thread_current()->spt, align)) {
			if (!vm_alloc_page(VM_ANON, align, true))
				exit(-1);
			/* list of sharing physical memory */
			struct page *page = spt_find_page(&thread_current()->spt, addr);
			if (!shared_list_init(page))
				exit(-1);
		}
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
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (!not_present)
		return false;
	
	page = spt_find_page(spt, addr);
	if (page == NULL) {
		uintptr_t *gap = f->rsp - (uintptr_t)addr;
		if (0 <= gap && gap <= 8 && USER_STACK_LIMIT <= addr) {
		// if (0 == gap && USER_STACK_LIMIT <= addr) {
			vm_stack_growth(addr);
			page = spt_find_page(spt, addr);
			if (page == NULL)
				return false;
		} else {
			return false;
		}
	}

	if (write && !page->writable)
		return false;

	return vm_do_claim_page (page);
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
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
		return false;
	
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	ASSERT (frame->page == NULL);
	ASSERT (page->frame == NULL);
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
		return false;

	memset(page->frame->kva, 0, PGSIZE);

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->hash_table, hashing, compare_page_va, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	hash_apply(&src->hash_table, page_copy);
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy(&spt->hash_table, page_destroy);
}

/* list of sharing physical memory */
bool
shared_list_init (struct page *page) {
	struct list *shared_list = (struct list *)calloc(1, sizeof(struct list));
	list_init(shared_list);
	if (!shared_list || !page)
		return false;
	list_push_back(shared_list, &page->shared_elem);
	return true;
}