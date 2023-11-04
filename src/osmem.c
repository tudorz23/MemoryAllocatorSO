// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"

block_meta_t head;
int head_init_done = 0;
int heap_prealloc_done = 0;

/**
 * Initialize the head of the circular list. The head will be a permanent,
 * free block, without a payload. It will only serve as the starting point
 * for any traversal of the list.
*/
void head_init(void)
{
	head.size = 0;
	head.prev = &head;
	head.next = &head;
	head_init_done = 1;
}

/**
 * Adds @block to the end of the linked list.
*/
void list_add_last(block_meta_t *block)
{
	block_meta_t *last = head.prev;

	last->next = block;
	block->prev = last;
	block->next = &head;
	head.prev = block;
}

/**
 * Removes @block from the linked list.
*/
void list_remove_block(block_meta_t *block)
{
	block->prev->next = block->next;
	block->next->prev = block->prev;
}

/**
 * Maps memory using mmap() and adds the newly created block to the list.
 * @return the new block's address.
*/
block_meta_t *map_block_in_mem(size_t size)
{
	size_t requested_size = (META_BLOCK_SIZE + size);
	block_meta_t *block = mmap(NULL, requested_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	
	if (block == MAP_FAILED) {
		return NULL;
	}

	block->size = size;
	block->status = STATUS_MAPPED;
	list_add_last(block);

	return block;
}

/**
 * Attempts to do the Heap Preallocation if it had not
 * already been done.
 * @return 1 for success, 0 otherwise.
*/
int prealloc_heap_attempt()
{
	if (heap_prealloc_done != 0) {
		return 1;
	}

	// Try to do the Heap Preallocation
	void *request_block = sbrk(HEAP_PREALLOC_SIZE);

	// Check if sbrk failed.
	if (request_block == (void *) -1) {
		return 0;
	}

	block_meta_t *prealloc_block = (block_meta_t *)request_block;
	prealloc_block->size = HEAP_PREALLOC_SIZE - META_BLOCK_SIZE;
	prealloc_block->status = STATUS_FREE;
	list_add_last(prealloc_block);

	heap_prealloc_done = 1;
	
	return 1;
}

/**
 * Traverses the list and searches for the block that best fits
 * the @size requested.
 * @return start adress of the best fit block, if it exists, NULL, otherwise.
*/
block_meta_t *find_best_block(size_t size)
{
	block_meta_t *iterator = head.next;
	block_meta_t *best_fit = NULL;

	while (iterator != &head) {
		if (iterator->status == STATUS_FREE && iterator->size >= size) {
			if (iterator->size == size) {
				return iterator;
			}

			if (!best_fit || iterator->size < best_fit->size) {
				best_fit = iterator;
			}
		}

		iterator = iterator->next;
	}

	return best_fit;
}

/**
 * Attempts to split the @block if enough bytes remain free
 * after filling @size bytes.
 * Does not change the address @block points to, so
 * it can be used freely afterwards.
*/
void split_block_attempt(block_meta_t *block, size_t size)
{
	if (block->size == size) {
		return;
	}

	// If split happens, the payload of @block will be occupied by the
	// requested size and a new block_meta_t structure.
	size_t occupied_size = size + META_BLOCK_SIZE;

	if (occupied_size + 1 >= block->size) {
		// No split is performed.
		return;
	}

	block_meta_t *new_block = (block_meta_t *)((char *)block + META_BLOCK_SIZE + size);

	new_block->size = block->size - occupied_size;
	new_block->status = STATUS_FREE;

	block->size -= occupied_size;

	// Add new block in the list.
	new_block->next = block->next;
	new_block->prev = block;
	block->next->prev = new_block;
	block->next = new_block;
}

/**
 * Expands the last block.
 * @return the extended last block, in case of success, NULL, otherwise.
*/
block_meta_t *expand_last_block(size_t size)
{
	block_meta_t *last_block = head.prev;
	size_t additional_needed_size = size - last_block->size;

	void *heap_end = (char *)last_block + META_BLOCK_SIZE + last_block->size;

	heap_end = sbrk(additional_needed_size);
	if (heap_end == (void *) -1) {
		return NULL;
	}

	last_block->size += additional_needed_size;
	return last_block;
}

/**
 * Coalesces two blocks, merging them into @block1,
 * while removing @block2 from the list.
*/
void coalesce_blocks(block_meta_t *block1, block_meta_t *block2)
{
	block1->size += META_BLOCK_SIZE + block2->size;
	list_remove_block(block2);
}

/**
 * Traverses the list, searching for adjacent free blocks.
 * If such blocks are found, they are coalesced into one bigger block.
 * The coalescing is done progressively on two blocks at a time.
*/
void coalesce_attempt(void)
{
	block_meta_t *iterator = head.next;
	block_meta_t *to_coalesce1 = NULL;
	block_meta_t *to_coalesce2 = NULL;

	while (iterator != &head) {
		if (iterator->status == STATUS_ALLOC) {
			to_coalesce1 = NULL;
			to_coalesce2 = NULL;
			iterator = iterator->next;
			continue;
		}

		// Two free blocks separated by a mapped block can still be coalesced.
		if (iterator->status == STATUS_MAPPED) {
			iterator = iterator->next;
			continue;
		}

		// Iterator surely points to a free block.
		if (to_coalesce1 == NULL) {
			to_coalesce1 = iterator;
			iterator = iterator->next;
			continue;
		}

		to_coalesce2 = iterator;

		// Before coalescing the blocks, prepare iterator for the next step.
		iterator = iterator->next;

		coalesce_blocks(to_coalesce1, to_coalesce2);
	}
}

/**
 * Searches the list for the memory zone allocated on the heap
 * that best fits the requested @size.
 * If no fit is found, the last block is expanded if free.
 * If it is not free, a new block is allocated.
 * To be called when memory allocated with sbrk() is needed.
 * @return allocated memory in case of success, NULL otherwise
*/
void *request_heap_memory(size_t size)
{
	if (!prealloc_heap_attempt()) {
		// sbrk() failed during preallocation
		return NULL;
	}

	coalesce_attempt();
	
	block_meta_t *best_block = find_best_block(size);
	if (best_block) {
		split_block_attempt(best_block, size);
		best_block->status = STATUS_ALLOC;
		return ((char *)best_block + META_BLOCK_SIZE);
	}

	
	// There is no block able to sustain the requested size.
	if (head.prev->status == STATUS_FREE) {
		block_meta_t *expanded_block = expand_last_block(size);
		if (!expanded_block) {
			return NULL;
		}

		return ((char *)expanded_block + META_BLOCK_SIZE);
	}

	// The last block is not free, so a new block should be added.
	void *request_block = sbrk(META_BLOCK_SIZE + size);

	if (request_block == (void *) -1) {
		return NULL;
	}

	block_meta_t *new_block = (block_meta_t *)request_block;
	new_block->size = size;
	new_block->status = STATUS_ALLOC;
	list_add_last(new_block);

	return ((char *)new_block + META_BLOCK_SIZE);
}

void *os_malloc(size_t size)
{
	if (size <= 0) {
		return NULL;
	}

	// Check if the list head has been initialized
	if (!head_init_done) {
		head_init();
	}

	// The alignment is done before calling any function, so they
	// ought not bother with alignment.
	size_t aligned_size = ALIGN(size);

	if (aligned_size + META_BLOCK_SIZE < MMAP_THRESHOLD) {
		return request_heap_memory(aligned_size);
	} else {
		block_meta_t *block = map_block_in_mem(aligned_size);
		if (!block) {
			return NULL;
		}
		return ((char *)block + META_BLOCK_SIZE);
	}
}

block_meta_t *search_block_to_free(void *ptr)
{
	block_meta_t *iterator = head.next;

	while (iterator != &head) {
		if (((char *)iterator + META_BLOCK_SIZE) == ptr) {
			return iterator;
		}

		iterator = iterator->next;
	}

	return NULL;
}

void os_free(void *ptr)
{
	if (!ptr) {
		return;
	}

	block_meta_t *block = search_block_to_free(ptr);

	if (!block) {
		return;
	}

	if (block->status == STATUS_FREE) {
		return;
	}

	if (block->status == STATUS_MAPPED) {
		list_remove_block(block);
		size_t size_to_delete = META_BLOCK_SIZE + ALIGN(block->size);
		munmap(block, size_to_delete);
		return;
	}

	if (block->status == STATUS_ALLOC) {
		block->status = STATUS_FREE;
		return;
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (nmemb == 0 || size == 0) {
		return NULL;
	}

	if (!head_init_done) {
		head_init();
	} 

	size_t aligned_size = ALIGN(size * nmemb);

	if ((long)(aligned_size + META_BLOCK_SIZE) < (long)getpagesize()) {
		void *result = request_heap_memory(aligned_size);
		if (!result) {
			return NULL;
		}

		memset(result, 0, aligned_size);
		return result;
	} else {
		block_meta_t *block = map_block_in_mem(aligned_size);
		if (!block) {
			return NULL;
		}
		void *result = (char *)block + META_BLOCK_SIZE;
		memset(result, 0, aligned_size);
		return result;
	}
}

void *os_realloc(void *ptr, size_t size)
{
	/* TODO: Implement os_realloc */
	return NULL;
}
