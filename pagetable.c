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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pagetable.h"
#include "sim.h"
#include "swap.h"


// The top-level page table (also known as the 'page directory pointer table')
static pdpt_entry_t pdpt[PTRS_PER_PDPT];

// Counters for various events.
// Your code must increment these when the related events occur.
size_t hit_count = 0;
size_t miss_count = 0;
size_t ref_count = 0;
size_t evict_clean_count = 0;
size_t evict_dirty_count = 0;

/*
 * Initializes the top-level page table.
 * This function is called once at the start of the simulation.
 * For the simulation, there is a single "process" whose reference trace is
 * being simulated, so there is just one top-level page table. To keep things
 * simple, we use a global array of 'page directory pointer table entries'.
 *
 * In a real OS, each process would have its own page directory, which would
 * need to be allocated and initialized as part of process creation.
 */
void init_page_tables(void)
{
	// Set all entries in top-level page table to 0, which ensures valid
	// bits are all 0 initially.
	for (size_t i = 0; i < PTRS_PER_PDPT; ++i) {
		pdpt[i].pdp = 0;// sets all bits, including valid, to zero
	}
}

// For simulation, we allocate second-level page tables using malloc
static pdpt_entry_t init_page_directory(void)
{
	pd_entry_t *pd;
	// Allocating aligned memory ensures the low bits in the pointer must
	// be zero, so we can use them to store our status bits, like PAGE_VALID
	if (posix_memalign((void **)&pd, PAGE_SIZE, PTRS_PER_PD * sizeof(pd_entry_t)) != 0) {
		perror("Failed to allocate aligned memory for page directory");
		exit(1);
	}

	// Initialize all entries in second-level page table
	for (size_t i = 0; i < PTRS_PER_PD; ++i) {
		pd[i].pde = 0;// sets all bits, including valid, to zero
	}

	pdpt_entry_t new_entry;
	// Mark the new page directory entry as valid
	new_entry.pdp = (uintptr_t)pd | PAGE_VALID;

	return new_entry;
}

// For simulation, we allocate third-level page tables using malloc
static pd_entry_t init_page_table(void)
{
	pt_entry_t *pt;
	// Allocating aligned memory ensures the low bits in the pointer must
	// be zero, so we can use them to store our status bits, like PAGE_VALID
	if (posix_memalign((void **)&pt, PAGE_SIZE, PTRS_PER_PT * sizeof(pt_entry_t)) != 0) {
		perror("Failed to allocate aligned memory for page table");
		exit(1);
	}

	// Initialize all entries in third-level page table
	for (size_t i = 0; i < PTRS_PER_PT; ++i) {
		pt[i].frame = 0;// sets all bits, including valid, to zero
		pt[i].swap_offset = INVALID_SWAP;
	}

	pd_entry_t new_entry;
	// Mark the new page directory entry as valid
	new_entry.pde = (uintptr_t)pt | PAGE_VALID;

	return new_entry;
}

/*
 * Initializes the content of a (simulated) physical memory frame when it
 * is first allocated for some virtual address. Just like in a real OS, we
 * fill the frame with zeros to prevent leaking information across pages.
 *
 * In our simulation, we also store the the virtual address itself in the
 * page frame to help with error checking.
 */
static void init_frame(int frame, vaddr_t vaddr)
{
	// Calculate pointer to start of frame in (simulated) physical memory
	char *mem_ptr = &physmem[frame * SIMPAGESIZE];
	// Calculate pointer to location in page where we keep the vaddr
	vaddr_t *vaddr_ptr = (vaddr_t *)(mem_ptr + sizeof(size_t));

	memset(mem_ptr, 0, SIMPAGESIZE); // zero-fill the frame
	*vaddr_ptr = vaddr & PAGE_MASK;  // record the vaddr for error checking
}

/*
 * Allocates a frame to be used for the virtual page represented by p.
 * If all frames are in use, calls the replacement algorithm's evict_func to
 * select a victim frame. Writes victim to swap if needed, and updates
 * page table entry for victim to indicate that virtual page is no longer in
 * (simulated) physical memory.
 *
 * Counters for evictions should be updated appropriately in this function.
 */
static int allocate_frame(pt_entry_t *pte)
{
	int frame = -1;
	for (size_t i = 0; i < memsize; ++i) {
		if (!coremap[i].in_use) {
			frame = i;
			break;
		}
	}

	if (frame == -1) { // Didn't find a free page.
		// Call replacement algorithm's evict function to select victim
		frame = evict_func();
		assert(frame != -1);

		// All frames were in use, so victim frame must hold some page
		// Write victim page to swap, if needed, and update page table

		// TODO: IMPLEMENTATION NEEDED
		int new_num = 0;
		if(!(coremap[frame].pte->frame & PAGE_DIRTY)){
			// if page is not dirty
			 evict_clean_count = evict_clean_count + 1;
		} else {
			// if page is dirty
			new_num = swap_pageout(frame, coremap[frame].pte->swap_offset);
			evict_dirty_count = evict_dirty_count + 1;
			// check if we swap
			if (new_num != -1) {
				coremap[frame].pte->swap_offset = new_num;
			} else {
				exit(1);
			}
		}

		unsigned int temp_frame1 = coremap[frame].pte->frame & ~PAGE_VALID;
		unsigned int temp_frame2 = temp_frame1 & ~PAGE_DIRTY;
		coremap[frame].pte->frame = temp_frame2 | PAGE_ONSWAP;

	}

	// Record information for virtual page that will now be stored in frame
	coremap[frame].in_use = true;
	coremap[frame].pte = pte;

	return frame;
}

/*
 * Locate the physical frame number for the given vaddr using the page table.
 *
 * If the entry is invalid and not on swap, then this is the first reference
 * to the page and a (simulated) physical frame should be allocated and
 * initialized (using init_frame).
 *
 * If the entry is invalid and on swap, then a (simulated) physical frame
 * should be allocated and filled by reading the page data from swap.
 *
 * Counters for hit, miss and reference events should be incremented in
 * this function.
 */
char *find_physpage(vaddr_t vaddr, char type)
{
	pt_entry_t *pte = NULL;// Pointer to the page table entry for vaddr

	// To keep compiler happy - remove when you have a real use
	(void)vaddr;
	(void)type;
	(void)allocate_frame;
	(void)init_page_directory;
	(void)init_page_table;
	(void)init_frame;

	// TODO: IMPLEMENTATION NEEDED
	// Use top-level page table and vaddr to get pointer to 2nd-level page table
	unsigned idx = PDPT_INDEX(vaddr);
	if(pdpt[idx].pdp == 0){
		// allocate second level
		pdpt[idx] = init_page_directory(); 
	}
	//pointer to the second level page table
	pd_entry_t *pg2 = (pd_entry_t *)(pdpt[idx].pdp & PAGE_MASK);
	unsigned idx2 = PD_INDEX(vaddr);

	// Use 2nd-level page table and vaddr to get pointer to 3rd-level page table
	if(pg2[idx2].pde == 0){
		// allocate third level
		pg2[idx2] = init_page_table(); //allocate additional page table 
	}
	//pointer to the third level page table
	pt_entry_t *pt = (pt_entry_t *)(pg2[idx2].pde & PAGE_MASK);

	// Use 3rd-level page table and vaddr to get pointer to page table entry
	unsigned idx3 = PT_INDEX(vaddr);
	pte = &pt[idx3];

	// Check if pte is valid or not, on swap or not, and handle appropriately
	// (Note that the first acess to a page should be marked DIRTY)
	if (pte->frame & PAGE_VALID) {
		// is valid
		hit_count = hit_count + 1;
	} else {

		int frame = allocate_frame(pte);
		if(!(pte->frame & PAGE_ONSWAP)){
			// not on swap
			init_frame(frame, vaddr);
			int tempframe3 = frame << PAGE_SHIFT;
			pte->frame = tempframe3 | PAGE_DIRTY;
			pte->frame = pte->frame | PAGE_ONSWAP;

		} else {
			// on swap
			if (swap_pagein(frame, pte->swap_offset) == 0) {
				int tempframe1 = frame << PAGE_SHIFT;
				int tempframe2 = tempframe1 & (~PAGE_DIRTY);
				pte->frame = tempframe2 | PAGE_ONSWAP;
			} else {
				exit(1);
			}
		}

		miss_count = miss_count + 1;
	}

	// Make sure that pte is marked valid and referenced. Also mark it
	// dirty if the access type indicates that the page will be written to.

	int temp = pte->frame | PAGE_VALID;
	pte->frame = temp | PAGE_REF;
	
	if (type == 'M' || type == 'S') {
		pte->frame = pte->frame | PAGE_DIRTY;
	}
	ref_count = ref_count + 1;

	// Call replacement algorithm's ref_func for this page
	ref_func(pte);

	// Return pointer into (simulated) physical memory at start of frame
	return &physmem[(pte->frame >> PAGE_SHIFT) * SIMPAGESIZE];
}


static void print_page_table(pt_entry_t *pt)
{
	int first_invalid = -1, last_invalid = -1;

	for (int i = 0; i < PTRS_PER_PT; ++i) {
		if (!(pt[i].frame & PAGE_VALID) && !(pt[i].frame & PAGE_ONSWAP)) {
			if (first_invalid == -1) {
				first_invalid = i;
			}
			last_invalid = i;
		} else {
			if (first_invalid != -1) {
				printf("\t\t[%d] - [%d]: INVALID\n",
				       first_invalid, last_invalid);
				first_invalid = last_invalid = -1;
			}
			printf("\t\t[%d]: ", i);
			if (pt[i].frame & PAGE_VALID) {
				printf("VALID, ");
				if (pt[i].frame & PAGE_DIRTY) {
					printf("DIRTY, ");
				}
				int framenum = pt[i].frame >> PAGE_SHIFT;
				char *page = &physmem[framenum * SIMPAGESIZE];
				printf("in frame %d, version %zu, vaddr %zu\n", framenum,
				       *(size_t *)page, *(vaddr_t *)(page + sizeof(size_t)));
			} else {
				assert(pt[i].frame & PAGE_ONSWAP);
				printf("ONSWAP, at offset %lu\n", (unsigned long)pt[i].swap_offset);
			}
		}
	}
	if (first_invalid != -1) {
		printf("\t\t[%d] - [%d]: INVALID\n", first_invalid, last_invalid);
		first_invalid = last_invalid = -1;
	}
}

static void print_page_directory(pd_entry_t *pd)
{
	int first_invalid = -1, last_invalid = -1;

	for (int i = 0; i < PTRS_PER_PD; ++i) {
		if (!(pd[i].pde & PAGE_VALID)) {
			if (first_invalid == -1) {
				first_invalid = i;
			}
			last_invalid = i;
		} else {
			if (first_invalid != -1) {
				printf("\t[%d]: INVALID\n  to\n[%d]: INVALID\n",
				       first_invalid, last_invalid);
				first_invalid = last_invalid = -1;
			}
			pt_entry_t *pt = (pt_entry_t *)(pd[i].pde & PAGE_MASK);
			printf("\t[%d]: %p\n", i, pt);
			print_page_table(pt);
		}
	}
}

void print_page_tables(void)
{
	int first_invalid = -1, last_invalid = -1;

	for (int i = 0; i < PTRS_PER_PDPT; ++i) {
		if (!(pdpt[i].pdp & PAGE_VALID)) {
			if (first_invalid == -1) {
				first_invalid = i;
			}
			last_invalid = i;
		} else {
			if (first_invalid != -1) {
				printf("[%d]: INVALID\n  to\n[%d]: INVALID\n",
				       first_invalid, last_invalid);
				first_invalid = last_invalid = -1;
			}
			pd_entry_t *pd = (pd_entry_t *)(pdpt[i].pdp & PAGE_MASK);
			printf("[%d]: %p\n", i, pd);
			print_page_directory(pd);
		}
	}
}


void free_page_tables(void)
{
	for (int i = 0; i < PTRS_PER_PDPT; ++i) {
		if (pdpt[i].pdp & PAGE_VALID) {
			pd_entry_t *pd = (pd_entry_t *)(pdpt[i].pdp & PAGE_MASK);

			for (int j = 0; j < PTRS_PER_PD; ++j) {
				if (pd[j].pde & PAGE_VALID) {
					pt_entry_t *pt = (pt_entry_t *)(pd[j].pde & PAGE_MASK);
					free(pt);
				}
			}

			free(pd);
		}
	}
}
