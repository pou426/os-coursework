/*
 * Buddy Page Allocation Algorithm
 * SKELETON IMPLEMENTATION -- TO BE FILLED IN FOR TASK (2)
 */

/*
 * STUDENT NUMBER: s
 */
#include <infos/mm/page-allocator.h>
#include <infos/mm/mm.h>
#include <infos/kernel/kernel.h>
#include <infos/kernel/log.h>
#include <infos/util/math.h>
#include <infos/util/printf.h>

using namespace infos::kernel;
using namespace infos::mm;
using namespace infos::util;

#define MAX_ORDER	17

/**
 * A buddy page allocation algorithm.
 */
class BuddyPageAllocator : public PageAllocatorAlgorithm
{
private:
	/**
	 * Returns the number of pages that comprise a 'block', in a given order.
	 * @param order The order to base the calculation off of.
	 * @return Returns the number of pages in a block, in the order.
	 */
	static inline constexpr uint64_t pages_per_block(int order)
	{
		/* The number of pages per block in a given order is simply 1, shifted left by the order number.
		 * For example, in order-2, there are (1 << 2) == 4 pages in each block.
		 */
		return (1 << order);
	}

	/**
	 * Returns TRUE if the supplied page descriptor is correctly aligned for the
	 * given order.  Returns FALSE otherwise.
	 * @param pgd The page descriptor to test alignment for.
	 * @param order The order to use for calculations.
	 */
	static inline bool is_correct_alignment_for_order(const PageDescriptor *pgd, int order)
	{
		// Calculate the page-frame-number for the page descriptor, and return TRUE if
		// it divides evenly into the number pages in a block of the given order.
		return (sys.mm().pgalloc().pgd_to_pfn(pgd) % pages_per_block(order)) == 0;
	}

	/** Given a page descriptor, and an order, returns the buddy PGD.  The buddy could either be
	 * to the left or the right of PGD, in the given order.
	 * @param pgd The page descriptor to find the buddy for.
	 * @param order The order in which the page descriptor lives.
	 * @return Returns the buddy of the given page descriptor, in the given order.
	 */
	PageDescriptor *buddy_of(PageDescriptor *pgd, int order)
	{
		// (1) Make sure 'order' is within range
		if (order >= MAX_ORDER) {
			return NULL;
		}

		// (2) Check to make sure that PGD is correctly aligned in the order
		if (!is_correct_alignment_for_order(pgd, order)) {
			return NULL;
		}

		// (3) Calculate the page-frame-number of the buddy of this page.
		// * If the PFN is aligned to the next order, then the buddy is the next block in THIS order.
		// * If it's not aligned, then the buddy must be the previous block in THIS order.
		uint64_t buddy_pfn = is_correct_alignment_for_order(pgd, order + 1) ?
			sys.mm().pgalloc().pgd_to_pfn(pgd) + pages_per_block(order) :
			sys.mm().pgalloc().pgd_to_pfn(pgd) - pages_per_block(order);

		// (4) Return the page descriptor associated with the buddy page-frame-number.
		return sys.mm().pgalloc().pfn_to_pgd(buddy_pfn);
	}

	/**
	 * Inserts a block into the free list of the given order.  The block is inserted in ascending order.
	 * @param pgd The page descriptor of the block to insert.
	 * @param order The order in which to insert the block.
	 * @return Returns the slot (i.e. a pointer to the pointer that points to the block) that the block
	 * was inserted into.
	 */
	PageDescriptor **insert_block(PageDescriptor *pgd, int order)
	{
		// Starting from the _free_area array, find the slot in which the page descriptor
		// should be inserted.
		PageDescriptor **slot = &_free_areas[order];

		// Iterate whilst there is a slot, and whilst the page descriptor pointer is numerically
		// greater than what the slot is pointing to.
		while (*slot && pgd > *slot) {
			slot = &(*slot)->next_free;
		}

		// Insert the page descriptor into the linked list.
		pgd->next_free = *slot;
		*slot = pgd;

		// Return the insert point (i.e. slot)
		return slot;
	}

	/**
	 * Removes a block from the free list of the given order.  The block MUST be present in the free-list, otherwise
	 * the system will panic.
	 * @param pgd The page descriptor of the block to remove.
	 * @param order The order in which to remove the block from.
	 */
	void remove_block(PageDescriptor *pgd, int order)
	{
		// Starting from the _free_area array, iterate until the block has been located in the linked-list.
		PageDescriptor **slot = &_free_areas[order];
		while (*slot && pgd != *slot) {
			slot = &(*slot)->next_free;
		}

		// Make sure the block actually exists.  Panic the system if it does not.
		assert(*slot == pgd);

		// Remove the block from the free list.
		*slot = pgd->next_free;
		pgd->next_free = NULL;
	}

	/**
	 * Given a pointer to a block of free memory in the order "source_order", this function will
	 * split the block in half, and insert it into the order below.
	 * @param block_pointer A pointer to a pointer containing the beginning of a block of free memory.
	 * @param source_order The order in which the block of free memory exists.  Naturally,
	 * the split will insert the two new blocks into the order below.
	 * @return Returns the left-hand-side of the new block.
	 */
	PageDescriptor *split_block(PageDescriptor **block_pointer, int source_order)
	{
		// Make sure there is an incoming pointer.
		assert(*block_pointer);

		// Make sure the block_pointer is correctly aligned.
		assert(is_correct_alignment_for_order(*block_pointer, source_order));

		// An order-0 (or invalid order) block cannot be split.
		if (source_order <= 0)
			return *block_pointer;

		// The target order is simply the order one less than the incoming block.
		int target_order = source_order - 1;

		// Determine the number of pages in a block, in the target order.
		uint64_t ppb = pages_per_block(target_order);

		// Split the current block into a left-hand-side and a right-hand-side.
		PageDescriptor *left = *block_pointer;
		PageDescriptor *right = left + ppb;

		// Remove the block from the source order.
		remove_block(left, source_order);

		// Insert the left and right blocks into the target order.
		insert_block(left, target_order);
		insert_block(right, target_order);

		return left;
	}

	/**
	 * Takes a block in the given source order, and merges it (and it's buddy) into the next order.
	 * This function assumes both the source block and the buddy block are in the free list for the
	 * source order.  If they aren't this function will panic the system.
	 * @param block_pointer A pointer to a pointer containing a block in the pair to merge.
	 * @param source_order The order in which the pair of blocks live.
	 * @return Returns the new slot that points to the merged block.
	 */
	PageDescriptor **merge_block(PageDescriptor **block_pointer, int source_order)
	{
		assert(*block_pointer);

		// Make sure the area_pointer is correctly aligned.
		assert(is_correct_alignment_for_order(*block_pointer, source_order));

		// Areas in the top order cannot be merged.
		if (source_order >= (MAX_ORDER - 1)) {
			return NULL;
		}

		// The target order is one plus the source order.
		int target_order = source_order + 1;

		PageDescriptor *left = *block_pointer;
		PageDescriptor *right = buddy_of(left, source_order);

		// Remove the left and right hand blocks.
		remove_block(left, source_order);
		remove_block(right, source_order);

		// Insert the left hand block into the target order.  We MUST check to see if this function was called
		// with the LEFT or the RIGHT block in the buddy pair.
		if (left < right)
			return insert_block(left, target_order);
		else
			return insert_block(right, target_order);
	}

public:
	/**
	 * Constructs a new instance of the Buddy Page Allocator.
	 */
	BuddyPageAllocator() {
		// Iterate over each free area, and clear it.
		for (unsigned int i = 0; i < ARRAY_SIZE(_free_areas); i++) {
			_free_areas[i] = NULL;
		}
	}

	/**
	 * Allocates 2^order number of contiguous pages
	 * @param order The power of two, of the number of contiguous pages to allocate.
	 * @return Returns a pointer to the first page descriptor for the newly allocated page range, or NULL if
	 * allocation failed.
	 */
	PageDescriptor *alloc_pages(int order) override
	{
		// not_implemented();
		if (order >= MAX_ORDER) {
			return NULL;
		}

		PageDescriptor *pgd = _free_areas[order];
		if (pgd) { // if pgd exists
			remove_block(pgd, order);
			return pgd;
		}

		int higher_order = order + 1;
		PageDescriptor *left; // splitted left block
		while (true) {
			if (higher_order >= MAX_ORDER) { // runs out of higher order blocks, no available pages
				return NULL;
			}
			if (higher_order == order) {
				remove_block(left, order);
				return left;
			}
			if (_free_areas[higher_order]) {
				PageDescriptor **block_pointer = &_free_areas[higher_order];
				left = split_block(block_pointer, higher_order);
				higher_order--;
			} else {
				higher_order++;
			}
		}

	}

	/**
	 * Frees 2^order contiguous pages.
	 * @param pgd A pointer to an array of page descriptors to be freed.
	 * @param order The power of two number of contiguous pages to free.
	 */
	void free_pages(PageDescriptor *pgd, int order) override
	{
		if (order >= MAX_ORDER) {
			return;
		}
		// Make sure that the incoming page descriptor is correctly aligned
		// for the order on which it is being freed, for example, it is
		// illegal to free page 1 in order-1.
		assert(is_correct_alignment_for_order(pgd, order));

		PageDescriptor **slot = insert_block(pgd, order);
		while (order < MAX_ORDER) {
			PageDescriptor *buddy = buddy_of(*slot, order);
			// iterate through the free list of the order and check if the buddy is inside the free list
			PageDescriptor *pg = _free_areas[order];
			bool merged = false;
			while (pg && !merged) {
				if (pg==buddy) {
					PageDescriptor **inserted_block = merge_block(slot, order);
					slot = inserted_block;
					order++;
					merged = true;
				}
				pg = pg->next_free;
			}
			if (!merged) { // if no merge occur, that block cannot be 'further merged'
				return;
			}
		}
	}

	/**
	 * Reserves a specific page, so that it cannot be allocated.
	 * @param pgd The page descriptor of the page to reserve.
	 * @return Returns TRUE if the reservation was successful, FALSE otherwise.
	 */
	bool reserve_page(PageDescriptor *pgd)
	{
		int order = MAX_ORDER-1; // start from the highest order free area list
		bool found = false;
		PageDescriptor *pg; // first page descriptor pointer

		// mm_log.messagef(LogLevel::DEBUG, "PageDescriptor to be allocated pgd=%p", pgd);
		// mm_log.messagef(LogLevel::DEBUG, "PageDescriptor to be allocated pd=%p, nr=0x%lx", page_descriptors, nr_page_descriptors);

		while (order >= 0 && !found) {
			pg = _free_areas[order];
			// mm_log.messagef(LogLevel::DEBUG, "order=%d, pg=%p", order, pg);
			if (pgd < pg) { // while the pgd pointer is numerically smaller than the first pg from the free area list
				// mm_log.messagef(LogLevel::DEBUG, "pgd < pg");
				order--; // move to one order lower
			} else {
				// mm_log.messagef(LogLevel::DEBUG, "pgd >= pg");
				uint64_t nr_page_per_block = pages_per_block(order);
				// mm_log.messagef(LogLevel::DEBUG, "nr_page_per_block=%d", nr_page_per_block);
				while (pg && pgd >= pg && !found) {
					PageDescriptor *last_pg_of_block = &pg[nr_page_per_block];
					// mm_log.messagef(LogLevel::DEBUG, "last_pg_of_block=%p", last_pg_of_block);
					if (pg <= pgd && pgd < last_pg_of_block) { // required page in this block
						// mm_log.messagef(LogLevel::DEBUG, "block has been found");
						found = true;
					} else {
						pg = pg -> next_free; // go to next free block in that order
					}
				}

				if (!found) { // all page descriptors in that order's free list has been looked at, pgd not found
					order--; // move to one order lower
				}
			}
		}

		if (!found)		return false; // pgd not allocated in the free area list for all order

		// mm_log.messagef(LogLevel::DEBUG, "block pointer to page=%p at order=%d", pg, order);
		// pg points to the first page of the block containing the pgd page
		// order wil be the current order containing the block
		PageDescriptor **block_pointer = &pg;
		// mm_log.messagef(LogLevel::DEBUG, "block pointer=%p", *block_pointer);
		// split_block(block_pointer, order);
		// mm_log.messagef(LogLevel::DEBUG, "splitted");

		while (order >= 0) {
			if (order == 0)	{
				// *block_pointer == pgd
				PageDescriptor *pgd_next_pg = pgd->next_free;
				PageDescriptor *pgd_prev_pg = pgd-1;
				// mm_log.messagef(LogLevel::DEBUG, "pgd=%p | *block_pointer=%p | pgd_next_pg=%p | pgd_prev_pg=%p",pgd, *block_pointer, pgd_next_pg, pgd_prev_pg);
				pgd_prev_pg[0].next_free = pgd_next_pg;
				pgd[0].next_free = _free_areas[0];
				_free_areas[0] = pgd;
				alloc_pages(0);
				return true;
			}	 // individual block = individual page = pgd
			int target_order = order-1;
			uint64_t page_per_block = pages_per_block(target_order);
			PageDescriptor *splitted_left = split_block(block_pointer, order);
			// mm_log.messagef(LogLevel::DEBUG, " ============================ %d =================", order);
			PageDescriptor *splitted_right = splitted_left + page_per_block;
			// mm_log.messagef(LogLevel::DEBUG, "splitted_left=%p | splitted_right=%p", splitted_left, splitted_right);

			order--;
			// mm_log.messagef(LogLevel::DEBUG, " ============================ %d =================", order);

			if (order == 0) 	return true;
			if (pgd >= splitted_right) {
				// mm_log.messagef(LogLevel::DEBUG, "search right block");
				block_pointer = &splitted_right;
			} else {
				// mm_log.messagef(LogLevel::DEBUG, "search left block");
				block_pointer = &splitted_left;
			}
		}
	}

	/**
	 * Initialises the allocation algorithm.
	 * @return Returns TRUE if the algorithm was successfully initialised, FALSE otherwise.
	 */
	bool init(PageDescriptor *page_descriptors, uint64_t nr_page_descriptors) override
	{
		mm_log.messagef(LogLevel::DEBUG, "Buddy Allocator Initialising pd=%p, nr=0x%lx", page_descriptors, nr_page_descriptors);

		// TODO: Initialise the free area linked list for the maximum order
		// to initialise the allocation algorithm.

		// not_implemented();

		// NOTE: (from piazza) the no of pages will never be less than the block size of the max order (16)
		uint64_t nr_pgd_per_block = pages_per_block(MAX_ORDER-1); // max no of page descriptors for order-16 == 65536 pages per block

		uint64_t idx = nr_pgd_per_block;
		while (idx < nr_page_descriptors) {
			page_descriptors[idx-nr_pgd_per_block].next_free = &page_descriptors[idx];
			idx += nr_pgd_per_block;
		}

		_free_areas[MAX_ORDER-1] = page_descriptors; // assign the first list of free areas for order-16

		PageDescriptor *pg = _free_areas[MAX_ORDER-1];

		return true;
	}

	/**
	 * Returns the friendly name of the allocation algorithm, for debugging and selection purposes.
	 */
	const char* name() const override { return "buddy"; }

	/**
	 * Dumps out the current state of the buddy system
	 */
	void dump_state() const override
	{
		// Print out a header, so we can find the output in the logs.
		mm_log.messagef(LogLevel::DEBUG, "BUDDY STATE:");

		// Iterate over each free area.
		for (unsigned int i = 0; i < ARRAY_SIZE(_free_areas); i++) {
			char buffer[256];
			snprintf(buffer, sizeof(buffer), "[%d] ", i);

			// Iterate over each block in the free area.
			PageDescriptor *pg = _free_areas[i];
			while (pg) {
				// Append the PFN of the free block to the output buffer.
				snprintf(buffer, sizeof(buffer), "%s%lx ", buffer, sys.mm().pgalloc().pgd_to_pfn(pg));
				pg = pg->next_free;
			}

			mm_log.messagef(LogLevel::DEBUG, "%s", buffer);
		}
	}


private:
	PageDescriptor *_free_areas[MAX_ORDER];
};

/* --- DO NOT CHANGE ANYTHING BELOW THIS LINE --- */

/*
 * Allocation algorithm registration framework
 */
RegisterPageAllocator(BuddyPageAllocator);
