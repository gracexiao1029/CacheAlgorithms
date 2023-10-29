/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Andrew Peterson, Karen Reid, Alexey Khrabrov
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2021 Karen Reid
 */

#include "pagetable.h"
#include "sim.h"
#include <stddef.h>

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>

// MRU cache that contains frames in the doubly linked list
// structure, record the current frame number and the next 
// frame reference.
typedef struct mru_item {
	unsigned int curr_ref;
	struct mru_item *next_ref;
} mru_item;

mru_item *head;
mru_item *tail;

/* Page to evict is chosen using the accurate MRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int mru_evict(void)
{
	// If the head is null, then return -1
	if (head == NULL) {
		return -1;
	}

	// The current frame pointer to the head
	mru_item *curr_frame = head;
	mru_item *prev_ref = NULL;

	// Look for the next frame while the current frame is not NULL
	unsigned int head_ref = head -> curr_ref;
	while (curr_frame != NULL) {
		// If the current frame is not the most recently used frame
		if (curr_frame -> curr_ref != head_ref) {
			// The current frame is not most recently used, go to the next frame
			prev_ref = curr_frame;
			curr_frame = curr_frame -> next_ref;
		} else {// The current frame is least recently used
			// If the current frame is the head, make head the next reference of the frame
			if (curr_frame == head) {
				// Add new frame to the head
				head = curr_frame -> next_ref;
			}
			// If the current frame is the tail
			if (curr_frame == tail) {
				tail = prev_ref;
			}
			return head_ref;
		}
	}
	// Return the frame number at the head that is removed
	return head_ref;
}

/* This function is called on each access to a page to update any information
 * needed by the MRU algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void mru_ref(pt_entry_t *pte)
{
	mru_item *first_item = (mru_item*)malloc(sizeof(mru_item));
	
	// The current frame pointer to the head
	mru_item *curr_frame = head;
	mru_item *prev_ref = NULL;
	
	// Check the duplication, remove if the frame exists in the list
	while (curr_frame != NULL) {
		unsigned int pte_frame = pte -> frame >> PAGE_SHIFT;
		if (curr_frame -> curr_ref != pte_frame) {
			prev_ref = curr_frame;
			curr_frame = curr_frame -> next_ref;
		} else {
			// Neither head nor tail
			if (curr_frame != head && curr_frame != tail) {
				prev_ref -> next_ref = curr_frame -> next_ref;
			}
			if (curr_frame == head) {
				// Add new frame to the head
				head = curr_frame -> next_ref;
			}
			// If the current frame is the tail
			if (curr_frame == tail) { 
				tail = prev_ref;
				prev_ref -> next_ref = curr_frame -> next_ref;
			}
			break;
		}
	}

	first_item -> curr_ref = pte -> frame >> PAGE_SHIFT;
	first_item -> next_ref = head;
	head = first_item;

	// If the head is null, then assign head to be first_item
	if(head == NULL) {
		head = first_item;
	}
}

/* Initialize any data structures needed for this replacement algorithm. */
void mru_init(void)
{
	//TODO
	head = NULL;
	tail = NULL;
}

/* Cleanup any data structures created in mru_init(). */
void mru_cleanup(void)
{
	//TODO
}
