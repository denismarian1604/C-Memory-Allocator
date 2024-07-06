// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include "block_meta.h"
#include "block_functions.h"

// variable which points to the starting block of the list
struct block_meta *start_block;
// variable which indicates if the heap has been used
int heap_preallocated;
// function that returns the minimum of two numbers
size_t min(size_t a, size_t b)
{
	return (a < b) ? a : b;
}

void *os_malloc(size_t size)
{
	// make sure all the free blocks are coalesced
	coalesce_blocks(start_block);
	//  calculate the padding for the useful information
	size_t padding;

	if (size % 8 == 0)
		padding = 0;
	else
		padding = 8 - (size % 8);

	//  calculate total size of the block
	size_t total_size = META_SIZE + META_PADDING + size + padding;

	//  check for valid size
	if (size <= 0)
		return NULL;

	struct block_meta *block;

	//  there was nothing allocated until now on the heap
	//  and the total size is smaller than the threshold
	if (!heap_preallocated && total_size < MMAP_THRESHOLD) {
		// call brk to preallocate MMAP_THRESHOLD bytes
		void *res = sbrk(MMAP_THRESHOLD);

		struct block_meta *prev_block = find_last_block(start_block);
		//  if the allocation failed, return NULL
		if (res == (void *)(-1)) {
			errno = -((long long)res);
			return NULL;
		}

		heap_preallocated = 1;

		//  the allocated block is now res
		block = (struct block_meta *)res;
		// if the first_block was not set,
		// block is now the first_block
		if (!start_block)
			start_block = block;

		block->status = STATUS_ALLOC;
		block->size = total_size;
		block->prev = prev_block;

		if (prev_block)
			prev_block->next = block;

		//  use rem_block to store the remaining block after
		//  allocating the total_size block, if the remaining size is enough
		if (MMAP_THRESHOLD - total_size >= META_SIZE + META_PADDING + 8) {
			struct block_meta *rem_block;

			rem_block = (struct block_meta *)((char *)res + total_size);

			block->next = rem_block;

			rem_block->status = STATUS_FREE;
			rem_block->size = MMAP_THRESHOLD - total_size;
			rem_block->prev = block;
			rem_block->next = NULL;
		} else {
			block->next = NULL;
			//  if the block is not split, size stays at MMAP_THRESHOLD
			block->size = MMAP_THRESHOLD;
		}
	} else if (total_size >= MMAP_THRESHOLD) {
		//  get the last block to update its next block
		//  if there is a last block other than NULL
		struct block_meta *prev_block = find_last_block(start_block);
		//  the size of our block is greater than MMAP_THRESHOLD
		//  therefore we use mmap
		//  call mmap
		void *res = mmap(NULL, total_size, PROTS, MAPS, -1, 0);

		if (res == MAP_FAILED) {
			errno = -((long long)res);
			return NULL;
		}

		block = (struct block_meta *)res;

		block->status = STATUS_MAPPED;
		block->size = total_size;
		block->prev = prev_block;
		block->next = NULL;

		// if the first_block was not set,
		// block is now the first_block
		if (!start_block)
			start_block = block;

		if (prev_block)
			prev_block->next = block;
	} else if (heap_preallocated && total_size < MMAP_THRESHOLD) {
		//  there was something allocated until now
		//  if the size is smaller than MMAP_THRESHOLD
		struct block_meta *block;

		//  look for available block
		block = find_available_block(start_block, total_size);

		//  an available block was found
		if (block) {
			//  if the remaining size is enough
			//  to fit another structure block_meta and 1 byte of memory
			//  split the block
			if (block->size - total_size >= META_SIZE + META_PADDING + 8) {
				struct block_meta *rem_block;

				rem_block = (struct block_meta *)((char *)block + total_size);

				rem_block->status = STATUS_FREE;
				rem_block->size = block->size - total_size;
				rem_block->prev = block;
				rem_block->next = block->next;

				if (block->next)
					block->next->prev = rem_block;

				block->next = rem_block;
				//  only if the block will be split
				//  update its size
				block->size = total_size;
			}
				block->status = STATUS_ALLOC;
		} else {
			//  try expanding in the last available free block
			//  if it is the last available block
			//  get the last block
			struct block_meta *prev_block = find_last_heap_block(start_block);

			//  last block is free so it can be expanded
			if (prev_block && prev_block->status == STATUS_FREE) {
				size_t needed_space = total_size - prev_block->size;

				void *res = sbrk(needed_space);

				if (res == (void *)(-1)) {
					errno = -((long long)res);
					return NULL;
				}

				prev_block->size = total_size;
				prev_block->status = STATUS_ALLOC;

				block = prev_block;
			} else {
				//  no available block was found
				//  call sbrk to alloc MMAP_THRESHOLD bytes
				void *res = sbrk(total_size);

				//  if the allocation failed, return NULL
				if (res == (void *)(-1)) {
					errno = -((long long)res);
					return NULL;
				}

				//  the allocated block is now res
				block = (struct block_meta *)res;

				block->status = STATUS_ALLOC;
				block->size = total_size;
				block->prev = prev_block;
				block->next = prev_block->next;

				prev_block->next = block;
			}
		}

			if (!start_block)
				start_block = block;

			//  return the address where the useful information begins
			return (void *)((char *)block + META_SIZE);
		}
		//  return the address where the useful information begins
		return (void *)((char *)block + META_SIZE);
}

void os_free(void *ptr)
{
	struct block_meta *block = find_block(start_block, ptr);

	if (!block)
		return;
	// if the block is on the heap, just mark it as FREE
	if (block->status == STATUS_ALLOC) {
		block->status = STATUS_FREE;
	} else if (block->status == STATUS_MAPPED) {
		//  remove block from the list
		if (block->prev)
			block->prev->next = block->next;
		else
			start_block = block->next;

		if (block->next)
			block->next->prev = block->prev;
		else if (block->prev)
			block->prev->next = NULL;

		//  free the block
		munmap(block, block->size);
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	// make sure all the free blocks are coalesced
	coalesce_blocks(start_block);
	//  calculate the padding for the useful information
	size_t padding;

	if ((size * nmemb) % 8 == 0)
		padding = 0;
	else
		padding = 8 - ((size * nmemb) % 8);

	//  calculate total size of the block
	size_t total_size = META_SIZE + META_PADDING + size * nmemb + padding;

	//  check for valid size
	if (size * nmemb <= 0)
		return NULL;

	struct block_meta *block;

	//  there was nothing allocated until now
	//  and the total size is smaller than the page size
	if (!heap_preallocated && total_size < (size_t)PAGE_SIZE) {
		//  call brk to prealloc MMAP_THRESHOLD bytes
		void *res = sbrk(MMAP_THRESHOLD);

		struct block_meta *last_block = find_last_block(start_block);

		//  if the allocation failed, return NULL
		if (res == (void *)(-1)) {
			errno = -((long long)res);
			return NULL;
		}

		heap_preallocated = 1;
		//  the allocated block is now res
		block = (struct block_meta *)res;

		// if there was no start block, it is now set to block
		if (!start_block)
			start_block = block;

		block->status = STATUS_ALLOC;
		block->size = total_size;
		block->prev = last_block;

		if (last_block)
			last_block->next = block;

		//  use rem_block to store the remaining block after
		//  allocating the total_size block, if the remaining size is enough
		if (MMAP_THRESHOLD - total_size >= META_SIZE + META_PADDING + 8) {
			struct block_meta *rem_block;

			rem_block = (struct block_meta *)((char *)res + total_size);

			block->next = rem_block;

			rem_block->status = STATUS_FREE;
			rem_block->size = MMAP_THRESHOLD - total_size;
			rem_block->prev = block;
			rem_block->next = NULL;
		} else {
			block->next = NULL;
			//  if block is not split, size stays at PAGE_SIZE
			block->size = PAGE_SIZE;
		}
	} else if (total_size >= (size_t)PAGE_SIZE) {
		//  get the last block to update its next block
		//  if there is a last block other than NULL
		struct block_meta *prev_block = find_last_block(start_block);

		//  the size of our block is greater than PAGE_SIZE
		//  therefore we use mmap
		//  call mmap
		void *res = mmap(NULL, total_size, PROTS, MAPS, -1, 0);

		if (res == MAP_FAILED) {
			errno = -((long long)res);
			return NULL;
		}

		block = (struct block_meta *)res;

		block->status = STATUS_MAPPED;
		block->size = total_size;
		block->prev = prev_block;
		block->next = NULL;

		if (!start_block)
			start_block = block;

		if (prev_block)
			prev_block->next = block;
	} else if (heap_preallocated && total_size < (size_t)PAGE_SIZE) {
		//  there was something allocated until now
		struct block_meta *block;

		//  look for available block
		block = find_available_block(start_block, total_size);

		//  an available block was found
		if (block) {
			//  if the remaining size is enough
			//  to fit another structure block_meta and 1 byte of memory
			//  split the block
			if (block->size - total_size >= META_SIZE + META_PADDING + 8) {
				struct block_meta *rem_block;

				rem_block = (struct block_meta *)((char *)block + total_size);

				rem_block->status = STATUS_FREE;
				rem_block->size = block->size - total_size;
				rem_block->prev = block;
				rem_block->next = block->next;

				if (block->next)
					block->next->prev = rem_block;

				block->next = rem_block;

				//  only if the block will be split
				//  update its size
				block->size = total_size;
			}
				block->status = STATUS_ALLOC;
		} else {
			//  try expanding in the last available free block
			//  if it is the last available block

			//  get the last block
			struct block_meta *prev_block = find_last_heap_block(start_block);

			//  last block is free so it can be expanded
			if (prev_block->status == STATUS_FREE) {
				size_t needed_space = total_size - prev_block->size;

				void *res = sbrk(needed_space);

				if (res == (void *)(-1)) {
					errno = -((long long)res);
					return NULL;
				}

				prev_block->size = total_size;
				prev_block->status = STATUS_ALLOC;

				block = prev_block;
			} else {
				//  no available block was found
				//  call sbrk to alloc MMAP_THRESHOLD bytes
				void *res = sbrk(total_size);

				//  if the allocation failed, return NULL
				if (res == (void *)(-1)) {
					errno = -((long long)res);
					return NULL;
				}

				//  the allocated block is now res
				block = (struct block_meta *)res;

				block->status = STATUS_ALLOC;
				block->size = total_size;
				block->prev = prev_block;
				block->next = prev_block->next;

				prev_block->next = block;
			}
		}

		if (!start_block)
			start_block = block;

		//  set the memory to 0
		memset((void *)((char *)block + META_SIZE), 0, size * nmemb);
		//  return the address where the useful information begins
		return (void *)((char *)block + META_SIZE);
	}
	//  set the information to 0
	memset((void *)((char *)block + META_SIZE), 0, size * nmemb);
	//  return the address where the useful information begins
	return (void *)((char *)block + META_SIZE);
}

void *os_realloc(void *ptr, size_t size)
{
	// make sure all the free blocks are coalesced
	coalesce_blocks(start_block);

	struct block_meta *block = find_block(start_block, ptr);

	//  if the address is in a STATUS_FREE block
	//  the realloc is not valid
	if (block && block->status == STATUS_FREE) {
		return NULL;
	} else if (ptr == NULL) {
		//  if the pointer is NULL, call malloc
		return os_malloc(size);
	} else if (size == 0) {
		//  if the size is 0, the block needs to be freed
		os_free(ptr);
		return NULL;
	}
	//  calculate the padding for the useful information
	size_t padding;

	if (size % 8 == 0)
		padding = 0;
	else
		padding = 8 - (size % 8);

	//  calculate total size of the block
	size_t total_size = META_SIZE + META_PADDING + size + padding;

	//  if the block is not on the heap
	//  relocate it
	if (block->status == STATUS_MAPPED) {
		void *res = os_malloc(size);

		if (!res)
			return NULL;

		memcpy(res, ptr, min(size, block->size));
		os_free(ptr);

		// return the new starting address
		return res;
	}

	//  if it is on the heap and size is smaller
	if (total_size <= block->size) {
		// check if the block should be split
		if (block->size - total_size >= META_SIZE + META_PADDING + 8) {
			struct block_meta *rem_block;

			rem_block = (struct block_meta *)((char *)block + total_size);

			rem_block->size = block->size - total_size;
			rem_block->prev = block;
			rem_block->next = block->next;
			rem_block->status = STATUS_FREE;

			if (block->next)
				block->next->prev = rem_block;

			block->size = total_size;
			block->next = rem_block;
		}
		// return the same starting address
		return ptr;
	}

	// if the block is on the heap
	if (block->status == STATUS_ALLOC) {
		//  first try expanding the block
		size_t needed_space = total_size - block->size;

		if (!block->next) {
			if (total_size < MMAP_THRESHOLD) {
				//  allocate the difference
				void *res = sbrk(needed_space);

				if (res == (void *)(-1)) {
					errno = -((long long)res);
					return NULL;
				}

				block->size = total_size;
				// return the same starting address
				return ptr;
			}
		}

		// check for a free block right next to the current block
		if (block->next && block->next->status == STATUS_FREE
			&& block->size + block->next->size >= total_size) {
			// check to see if the block should be split
			if (block->size + block->next->size
				- total_size >= META_SIZE + 8) {
				struct block_meta *rem_block, *next_block;

				rem_block = (struct block_meta *)((char *)block + total_size);
				next_block = block->next->next;

				rem_block->size = block->size + block->next->size - total_size;
				rem_block->prev = block;
				rem_block->next = next_block;
				rem_block->status = STATUS_FREE;

				block->size = total_size;

				if (next_block)
					next_block->prev = rem_block;
				block->next = rem_block;
			} else {
				block->size += block->next->size;
				block->next = block->next->next;

				if (block->next)
					block->next->prev = block;
			}
			// return the same starting address
			return ptr;
		}
		//  if the block cannot be expanded
		void *res = os_malloc(size);

		if (!res)
			return NULL;

		res = memmove(res, ptr, min(size, block->size));
		os_free(ptr);

		return res;
	}

	return NULL;
}
