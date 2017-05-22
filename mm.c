#define debug 0
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

//#include "mm_ts.c"
#include "memlib.h"
#include "mm.h"
#include "tree.h"
#include "list.h"

/*this implementation of malloc uses a rb_tree to keep track of free blocks
 *in addition to the tree, this implemenation makes each block a part of a duplicate size list
 *if there is a duplicate size ready to be stored, then it will not be stored in the tree, instead
 *it will be stored in the form of a list meaning it will be stored after the block with the same
 *size in the tree. Add block operation and remove block operation is straightforward, change
 *block pointers if the block is a duplicate, otherwise pop it from the tree or insert it into the tree
 *
 *Malloc has a profiling sysmtem(meaning give more size than requested so that it can deal with the
 *extreme case in binary file) to make sure the space utilization to be the best
 *
 *extend heap only expands the heap when needed with the minimal size possible
 *
 *Realloc will look through possible locations to maximumize the utilization so that it can avoid
 *calling extendheap or mm_malloc when it's possible
 *
 *mm_free coalesces and free blocks.
 *              
 */

struct boundary_tag {
    int inuse:1;        // inuse bit
    int size:31;        // size of block, in words
};

/* FENCE is used for heap prologue/epilogue. */
const struct boundary_tag FENCE = { .inuse = 1, .size = 0 };
//typedef struct rb_tree rb_tree;
//typedef struct block block;
/* A C struct describing the beginning of each block. 
 * For implicit lists, used and free blocks have the same 
 * structure, so one struct will suffice for this example.
 * If each block is aligned at 4 mod 8, each payload will
 * be aligned at 0 mod 8.
 */
struct block {
    struct boundary_tag header; /* offset 0, at address 4 mod 8 */
    //struct node elem;
    union { 					/* offset 4, at address 0 mod 8 */
        char payload[0];
        RB_ENTRY(block) node;

    };
    struct block* prev;
    struct block* next;
};

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Doubleword size (bytes) */
#define MIN_BLOCK_SIZE_WORDS 8 /* Minimum block size in words */
#define CHUNKSIZE  (1<<4)  /* Extend heap by this amount (words) */
#define PADDING 3   /*padding to find a block */
#define DIV 84     /*magic number for profiling*/
#define PT 2        /*define profiling timer*/
#define MAX(x, y) ((x) > (y)? (x) : (y)) 

static int block_compare(struct block * a, struct block * b) {

    return a->header.size - b->header.size;   
}


/* initialize the root of RB tree*/

RB_HEAD(rb_tree, block) tree;
RB_GENERATE_STATIC(rb_tree, block, node, block_compare);
static void add_free_block(struct block*);
static void remove_free_block(struct block*);
//static void print_tree(struct block*);
/*prfofiling sizes*/
static size_t first;
static size_t second;
static size_t first_timer;
static size_t second_timer;

/* Return size of block is free */
static size_t blk_size(struct block *blk) { 
    return blk->header.size; 
}

/* Given a block, obtain pointer to next block.
   Not meaningful for right-most block. */
static struct block *next_blk(struct block *blk) {
    assert(blk_size(blk) != 0);
    return (struct block *)((size_t *)blk + blk->header.size);
}

/* Given a block, obtain previous's block footer.
   Works for left-most block also. */
static struct boundary_tag * prev_blk_footer(struct block *blk) {
    return &blk->header - 1;
}

/* Given a block, obtain pointer to previous block.
   Not meaningful for left-most block. */
static struct block *prev_blk(struct block *blk) {
    struct boundary_tag *prevfooter = prev_blk_footer(blk);
    //if ((void *)(blk - 1) < mem_heap_lo()) return NULL;
    assert(prevfooter->size != 0);
    return (struct block *)((size_t *)blk - prevfooter->size);
}

/* Given a block, obtain its footer boundary tag */
static struct boundary_tag * get_footer(struct block *blk) {
    return (void *)((size_t *)blk + blk->header.size) 
        - sizeof(struct boundary_tag);
}

/* Set a block's size and inuse bit in header and footer */
static void set_header_and_footer(struct block *blk, int size, int inuse) {
    blk->header.inuse = inuse;
    blk->header.size = size;
    *get_footer(blk) = blk->header;    /* Copy header to footer */
}

/* Mark a block as used and set its size. */
static void mark_block_used(struct block *blk, int size) {
    set_header_and_footer(blk, size, 1);
}

/* Mark a block as free and set its size. */
static void mark_block_free(struct block *blk, int size) {
    set_header_and_footer(blk, size, 0);
}

/* Return if block is free */
static bool blk_free(struct block *blk) { 
    return !blk->header.inuse; 
}

/* memory checking tool for examing free blocks in the tree along with other list member of that size*/
    void
print_tree(struct block *bp)
{
    struct block *left, *right;
    if (bp == NULL) {
        printf("nil");
        return;
    }
    left = RB_LEFT(bp, node);
    right = RB_RIGHT(bp, node);
    if (left == NULL && right == NULL) {
        while (bp != NULL){
            printf("%d", bp->header.size);
            bp = bp->next;
            if (bp!=NULL)
                printf("->");
        }
    }
    else {
        while (bp != NULL){
            printf("%d", bp->header.size);
            bp = bp->next;
            if (bp!=NULL)
                printf("->");
        }
        printf("(");
        print_tree(left);
        printf(",");
        print_tree(right);
        printf(")");
    }
}

/* 
 * The remaining routines are internal helper routines 
 */
/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static struct block *coalesce(struct block *bp) 
{
    //print_tree(bp);
    bool prev_alloc = prev_blk_footer(bp)->inuse;
    bool next_alloc = !blk_free(next_blk(bp));
    size_t size = blk_size(bp);

    if (prev_alloc && next_alloc) {            /* Case 1 */
        return bp;
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
        //printf("case 2 \n");	
        struct block* next =  next_blk(bp);
        remove_free_block(next);
        mark_block_free(bp, size + blk_size(next));
        return bp;
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        //printf("case 3 \n");
        struct block* prev =  prev_blk(bp);
        remove_free_block(prev);
        mark_block_free(prev, size + blk_size(prev));
        return prev;
    }

    else {                                     /* Case 4 */
        //printf("case 4 \n");
        struct block* prev =  prev_blk(bp);
        struct block* next =  next_blk(bp);
        remove_free_block(next);
        remove_free_block(prev);
        mark_block_free(prev, 
                size + blk_size(next) + blk_size(prev));
        return prev;

    }

    return bp;
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static struct block *extend_heap(size_t words) 
{
    void *bp;

    /* Allocate an even number of words to maintain alignment */
    words = (words + 1) & ~1;
    if ((long)(bp = mem_sbrk(words * WSIZE)) == -1)  
        return NULL;

    /* Initialize free block header/footer and the epilogue header.
     * Note that we scoop up the previous epilogue here. */
    struct block * blk = bp - sizeof(FENCE);
    mark_block_free(blk, words);
    next_blk(blk)->header = FENCE;

    // return blk;
    return coalesce(blk);
}

/**
 * intialize the memory 
 */
int mm_init (void) {
    RB_INIT(&tree);
    struct boundary_tag * initial = mem_sbrk(2 * sizeof(struct boundary_tag));
    if (initial == (void *)-1)
        return -1;
    first = DIV;
    second = DIV - 2;
    first_timer = second_timer = 0;
    /* We use a slightly different strategy than suggested in the book.
     * Rather than placing a min-sized prologue block at the beginning
     * of the heap, we simply place two fences.
     * The consequence is that coalesce() must call prev_blk_footer()
     * and not prev_blk() - prev_blk() cannot be called on the left-most
     * block.
     */
    initial[0] = FENCE;                     /* Prologue footer */
    initial[1] = FENCE;
    struct block * blk = (struct block*)&initial[1];
    if (extend_heap(CHUNKSIZE) == NULL)  {
        return -1;
    }
    add_free_block(blk);
    return 0;
}

/* 
 * find_fit - Find a fit for a block with asize words 
 */
static struct block *find_fit(size_t asize)
{
    struct block temp; 
    temp.header.size = asize;

    struct block *bp = RB_NFIND(rb_tree, &tree, &temp);
    if (bp != NULL) remove_free_block(bp);
    return bp;
}

static void add_free_block(struct block* bp) {

    if (bp != 0) {

        struct block* blk = RB_FIND(rb_tree, &tree, bp);

        if (blk == NULL) {              //case 1: no duplicate

            bp->prev = NULL;
            bp->next = NULL;
            RB_INSERT(rb_tree, &tree, bp);
        }
        else {
            struct block* next = blk->next;
            if (next != NULL) {         //case 2: with duplicates
                next->prev = bp;       
            }
            bp->next = next;
            blk->next = bp;
            bp->prev = blk;
        }
    }
    return;
}

static void remove_free_block(struct block* bp) {

    if (bp != 0) {

        if (bp->prev != NULL) {                 //case1: current block is in a duplicate list, but not the first node
            if (bp->next != NULL) {

                bp->next->prev = bp->prev;
                bp->prev->next = bp->next;
                bp->prev = NULL;
                bp->next = NULL;
                //bp->elem.node = NULL;
                return;
            }

            bp->prev->next = NULL;
            bp->prev = NULL;
            //bp->elem.node = NULL;
            return;
        }
        else {                                  //case2: current block is in a duplicate list, and it has nodes in its next
            if (bp->next != NULL) {
                struct block* blk = bp->next;

                blk->prev = NULL;
                bp->next = NULL;
                //bp->elem.node = NULL;

                RB_REMOVE(rb_tree, &tree, bp);
                RB_INSERT(rb_tree, &tree, blk);

                return;
            }

            //bp->elem.node = NULL;             //case3: current block is a lonely child
            RB_REMOVE(rb_tree, &tree, bp);
            return;
        }
    }
    return;
}

/* 
 * place - Place block of asize words at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
static void place(struct block *bp, size_t asize)
{
    size_t csize = blk_size(bp);
    //printf("placing a size into the table size %d\n", asize);

    if ((csize - asize) >= MIN_BLOCK_SIZE_WORDS) { 
        mark_block_used(bp, asize);
        bp = next_blk(bp); 
        mark_block_free(bp, csize-asize);
        add_free_block(bp);
    }
    else { 
        mark_block_used(bp, csize);
    }
}

/**
 * allocate a chunk of memory from map.
 */
void *mm_malloc (size_t size) {
    size_t awords;      /* Adjusted block size in words */
    size_t extendwords;  /* Amount to extend heap if no fit */
    struct block *bp;      

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /*profiling*/
    if (size == first + second);
    else {
        if (size == first) first_timer++;
        else if (size == second) second_timer++;
        else if (size > first) {
            first = size;
            first_timer = 0;
        }
        else if (size < second) {
            second = size;
            second_timer = 0;
        }
        if (first_timer >= PT && second_timer >= PT) {
            if (size == first)
                size += second;
        }
    }

    /* Adjust block size to include overhead and alignment reqs. */
    size += 2 * sizeof(struct boundary_tag);    /* account for tags */
    size = (size + DSIZE - 1) & ~(DSIZE - 1);   /* align to double word */
    awords = MAX(MIN_BLOCK_SIZE_WORDS, size/WSIZE);                                   /* respect minimum size */
    if ( (bp = find_fit(awords)) != NULL) {		/* if found a place for the block then place it*/
        //iassert(awords <= bp->header.size);
        place(bp, awords);
        return bp->payload;
    }

    /* maximimze the coalesce utility*/
    struct boundary_tag* prev_boundary_tag = (struct boundary_tag*)(mem_heap_hi() - sizeof(struct boundary_tag) - PADDING);
    if (!prev_boundary_tag->inuse) {
        extendwords = awords - prev_boundary_tag->size;
    }
    else {
        extendwords = awords;
    }

    if ((bp = extend_heap(extendwords)) == NULL)  
        return NULL;
    //mark_block_used(bp, bp->header.size);
    place(bp, awords);
    //     print_tree(RB_ROOT(&tree));
    return bp->payload;
}

/**
 * free the memory 
 */
void mm_free (void *ptr) {

    //printf("freeing a block\n");
    if (ptr == 0) 
        return;

    /* Find block from user pointer */
    struct block *blk = ptr - offsetof(struct block, payload);
    mark_block_free(blk, blk_size(blk));
    struct block* newblk = coalesce(blk);
    add_free_block(newblk);
    //add_free_block(blk);
}


/**
 * reallocate the memory
 */
void *mm_realloc(void *ptr, size_t size) {

    //printf("reallocating the memory with size%d\n", size);
    size_t oldsize;
    void *newptr;
    size_t raw_size = size;
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(size);
    }
    size += 2 * sizeof(struct boundary_tag);    /* account for tags */
    size = (size + DSIZE - 1) & ~(DSIZE - 1);   /* align to double word */
    struct block *oldblock = ptr - offsetof(struct block, payload);
    oldsize = blk_size(oldblock)*WSIZE;   
    struct block * next = next_blk(oldblock);
    if (size <= oldsize) return ptr; 
    if (blk_free(next)) {                                           //case when the next block is free to use
        size_t temp = next->header.size*WSIZE;
        if (temp + oldsize - size > WSIZE*MIN_BLOCK_SIZE_WORDS) {  //check if split is needed

            remove_free_block(next);
            mark_block_used(oldblock, size/WSIZE);
            next = next_blk(oldblock);
            mark_block_free(next, temp/WSIZE + oldsize/WSIZE - size/WSIZE);
            add_free_block(next);
        }
        else if (temp + oldsize-size >= 0) {                      //case when split is not needed
            remove_free_block(next);
            mark_block_used(oldblock, next->header.size + oldsize/4);
        }
        else if (next_blk(next)->header.size == 0) {              //check if the next block is the end of the heap, so we can request more memory to it.
            remove_free_block(next);
            extend_heap(size/WSIZE - oldsize/WSIZE - next->header.size);
            remove_free_block(next);
            mark_block_used(oldblock, size/WSIZE);
        }
        else if (!prev_blk_footer(oldblock)->inuse && prev_blk_footer(oldblock)->size*WSIZE > 24  && prev_blk_footer(oldblock)->size*WSIZE + oldsize + temp > size ) {     //case when we can use space from the previous block and the next block

            remove_free_block(next);
            struct block* prev = prev_blk(oldblock);
            size_t prev_size = prev->header.size;
            if (temp + oldsize + blk_size(prev) - size > WSIZE*MIN_BLOCK_SIZE_WORDS) {      //check if we need to split
                remove_free_block(prev);
                memcpy(prev->payload, ptr, oldsize);
                mark_block_used(prev, size/WSIZE); 
                next = next_blk(prev);
                mark_block_free(next, temp/WSIZE + oldsize/WSIZE+prev_size - size/WSIZE);
                add_free_block(next);
                return prev->payload;
            }
            remove_free_block(prev);                                    //memcpy the old data to the previous block
            memcpy(prev->payload, ptr, oldsize);
            mark_block_used(prev, temp/WSIZE + oldsize/WSIZE+prev_size);
            return prev->payload;
        }
        else {      //else ust call malloc

            newptr = mm_malloc(raw_size);
            if (!newptr) return 0;
            if (size < oldsize) oldsize = size;
            memcpy(newptr, ptr, oldsize);

            /* Free the old block. */
            mm_free(ptr);
            return newptr;
        }
        return  ptr;
    }
    else if (next->header.size == 0) {      //case when the current block is the last block in the heap
        extend_heap(size/WSIZE - oldsize/WSIZE);
        mark_block_used(oldblock, size/WSIZE);
        return ptr;
    }
    else if (!prev_blk_footer(oldblock)->inuse && prev_blk_footer(oldblock)->size*WSIZE + oldsize > size) {     //case when the previous block can be used
        struct block* prev = prev_blk(oldblock);
        size_t prev_size = prev->header.size;
        if (oldsize + blk_size(prev) - size > WSIZE*MIN_BLOCK_SIZE_WORDS) {

            remove_free_block(prev);
            memcpy(prev->payload, ptr, oldsize);
            mark_block_used(prev, size/WSIZE);
            next = next_blk(prev);
            mark_block_free(next, oldsize/WSIZE+prev_size- size/WSIZE);
            add_free_block(next);
            return prev->payload;
        }
        remove_free_block(prev);
        memcpy(prev->payload, ptr, oldsize);
        mark_block_used(prev, oldsize/WSIZE+prev_size);
        return prev->payload;
    }
    else {                  //nothing can be used, call malloc
        newptr = mm_malloc(raw_size);
        if (!newptr) return 0;
        if (size < oldsize) oldsize = size;
        memcpy(newptr, ptr, oldsize);

        /* Free the old block. */
        mm_free(ptr);
        return newptr;
    }
}

team_t team = {

    .teamname = "twocringeguys",
    .name1 = "shuaicheng zhang",
    .id1 = "zshuai8@vt.edu",
    .name2 = "tianchen peng",
    .id2 = "ptian94@vt.edu"
};
