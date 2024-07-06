// SPDX-License-Identifier: BSD-3-Clause

#include "block_functions.h"

struct block_meta *find_last_block(struct block_meta *start_block)
{
	struct block_meta *block = start_block;

	while (block && block->next)
		block = block->next;

	return block;
}

struct block_meta *find_available_block(struct block_meta *start, size_t size)
{
	struct block_meta *block = start, *best_block = NULL;

	int diff = -1;

	while (block) {
		if (block->status == STATUS_FREE && block->size >= size) {
			if (diff == -1) {
				diff = block->size - size;
				best_block = block;
			} else if (block->size - size < (size_t)diff) {
				diff = block->size - size;
				best_block = block;
			}
		}
		block = block->next;
	}

	return best_block;
}

struct block_meta *find_block(struct block_meta *start_block, void *ptr)
{
	if (!ptr)
		return NULL;

	struct block_meta *block = start_block;

	while (block) {
		if ((void *)((char *)block + META_SIZE) == ptr)
			return block;
		block = block->next;
	}

	return NULL;
}

struct block_meta *find_last_heap_block(struct block_meta *start_block)
{
	struct block_meta *block = start_block, *found = NULL;

	while (block) {
		if (block->status == STATUS_MAPPED)
			return found;
		found = block;
		block = block->next;
	}

	return found;
}

void sort_blocks(struct block_meta *start_block)
{
	//  move all mapped blocks to the end of the list
	struct block_meta *block = start_block;

	while (block) {
		if (block->status == STATUS_MAPPED && block->next) {
			struct block_meta *next_block = block->next;
			struct block_meta *prev_block = block->prev;

			if (prev_block)
				prev_block->next = next_block;
			else if (next_block)
				start_block = next_block;

			if (next_block)
				next_block->prev = prev_block;

			struct block_meta *last_block = find_last_block(start_block);

			if (last_block && last_block != block) {
				last_block->next = block;
				block->prev = last_block;
				block->next = NULL;
			}
		}

		block = block->next;
	}
}

void coalesce_blocks(struct block_meta *start_block)
{
	sort_blocks(start_block);

	struct block_meta *block = start_block;

	while (block) {
		if (block->status == STATUS_FREE) {
			//  if the next block is free
			//  coalesce the blocks
			if (block->next && block->next->status == STATUS_FREE) {
				block->size += block->next->size;
				block->next = block->next->next;

				if (block->next)
					block->next->prev = block;
			} else {
				block = block->next;
			}
		} else {
			block = block->next;
		}
	}
}
