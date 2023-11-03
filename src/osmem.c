// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"

block_meta_t head;
int heap_prealloc_done = 0;

/**
 * Initialize the head of the circular list. The head will be a permanent,
 * free block, without a payload. It will only serve as the starting point
 * for any traversal of the list.
*/
void head_init() {
	head.size = 0;
	head.status = STATUS_ALLOC;
	head.prev = &head;
	head.next = &head;
}

void list_add_last(block_meta_t *block) {
	block_meta_t *last = head.prev;

	last->next = block;
	block->prev = last;
	block->next = &head;
	head.prev = block;
}

void list_remove_node(block_meta_t *block) {
	block->prev->next = block->next;
	block->next->prev = block->prev;
}

block_meta_t *find_free_heap_block(size_t size) {
	block_meta_t *iterator = head.next;

	while (iterator != &head) {
		if (iterator->status == STATUS_FREE && iterator->size >= size) {
			return iterator;
		}
		iterator = iterator->next;
	}

	return NULL;
}

block_meta_t *alloc_on_heap(size_t size) {
	block_meta_t *block = sbrk(META_BLOCK_SIZE + ALIGN(size));

	if (block == (void *) -1) {
		return NULL;
	}

	block->size = size;
	block->status = STATUS_ALLOC;
	list_add_last(block);

	return block;
}

block_meta_t *map_block_in_mem(size_t size) {
	size_t requested_size = (META_BLOCK_SIZE + ALIGN(size));
	block_meta_t *block = mmap(NULL, requested_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	
	if (block == MAP_FAILED) {
		return NULL;
	}

	block->size = size;
	block->status = STATUS_MAPPED;
	list_add_last(block);

	return block;
}

void *os_malloc(size_t size)
{
	if (size <= 0) {
		return NULL;
	}

	if (head.status != STATUS_ALLOC) {
		head_init();
	}

	if (size < MMAP_THRESHOLD) {
		block_meta_t *block = find_free_heap_block(size);

		if (!block) {
			block = alloc_on_heap(size);
			if (!block) {
				return NULL;
			}
		}

		return ((char *)block + META_BLOCK_SIZE);
	} else {
		block_meta_t *block = map_block_in_mem(size);
		if (!block) {
			return NULL;
		}
		return ((char *)block + META_BLOCK_SIZE);
	}

	return NULL;
}

block_meta_t *search_block_to_free(void *ptr) {
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
		list_remove_node(block);
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
	/* TODO: Implement os_calloc */
	return NULL;
}

void *os_realloc(void *ptr, size_t size)
{
	/* TODO: Implement os_realloc */
	return NULL;
}
