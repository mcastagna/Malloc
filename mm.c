#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE 5120
#define MINIMUM 32

#define MAX(x,y) ((x) > (y)? (x):(y))

#define PACK(size, alloc) ((size)|(alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_REALLOC(p) ((GET(p) & 0x2)>>1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

 #define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
 #define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp)-DSIZE)))

#define NEXT_FREE(bp) (*(void **)(bp + DSIZE))
#define PREV_FREE(bp) (*(void **)(bp))
#define DRFP(bp) (*(void **)(bp))
#define SEP 5120
#define BUCKETS 10

static void mm_checkfree();
static char *heap_listp;
static void **free_listp;

int getIndex(size_t size)
{
    int i = size/SEP;
    if (i > BUCKETS-1)
        i = BUCKETS-1;
    return i;
}
static void insertBlock(void *bp)
{
    int i;
    size_t size = GET_SIZE(HDRP(bp));
    i = getIndex(size);
    NEXT_FREE(bp) = *(free_listp+i);
    if (*(free_listp+i) != NULL)
        PREV_FREE(*(free_listp+i)) = bp;
    PREV_FREE(bp) = NULL;
    *(free_listp+i) = bp;
 
}
static void removeBlock(void *bp)
{
    int i;
    size_t size = GET_SIZE(HDRP(bp));
    i = getIndex(size);
    
    if (PREV_FREE(bp))
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
    else
        *(free_listp+i) = NEXT_FREE(bp);

    if (NEXT_FREE(bp))
        PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
    PREV_FREE(bp) = NULL;
    NEXT_FREE(bp) = NULL;
}
    
static void *coalesce(void *bp)
{
    size_t prev = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev && next){
        insertBlock(bp);
        return bp;
    }
    else if (prev && !next){
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        removeBlock(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev && next){
        removeBlock(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else{
        removeBlock(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        removeBlock(PREV_BLKP(bp));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));

    }
    insertBlock(bp);
 
    return bp;
}
static void *extend_heap(size_t bytes)
{
    char *bp;
    size_t size = bytes;

    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1));

    return coalesce(bp);
}
/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(24*WSIZE)) == (void *)-1)
        return -1;
    free_listp = (void **)heap_listp;
    int i;
    for (i = 0; i < BUCKETS; i++)
        DRFP(heap_listp+i*DSIZE) = NULL;
    
    heap_listp+=10*DSIZE;

    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3*WSIZE), PACK(0,1));

    heap_listp+=DSIZE;

    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;
    return 0;
}

static void place(void *bp, size_t asize)
{
    size_t bsize = GET_SIZE(HDRP(bp));

    if (bsize - asize >= MINIMUM){
        removeBlock(bp);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        PUT(HDRP(NEXT_BLKP(bp)), PACK(bsize - asize, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(bsize - asize, 0));
        insertBlock(NEXT_BLKP(bp));
    }
    else
    {
        PUT(HDRP(bp), PACK(bsize, 1));
        PUT(FTRP(bp), PACK(bsize, 1));
        removeBlock(bp);
    }
}

static void *find_fit(size_t asize)
{
    int i = getIndex(asize);
    int k;
    char *bp;
    for (k = i; k < BUCKETS; k++){
        for (bp = *(free_listp+k); bp != NULL; bp=NEXT_FREE(bp)){
            if (GET_SIZE(HDRP(bp)) >= asize)
                return bp;
        }        
    }

    
    return NULL;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;
    asize = MAX(ALIGN(size + DSIZE), MINIMUM);

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    size_t asize, oldSize;
    
    oldSize = GET_SIZE(HDRP(oldptr));
    asize = MAX(ALIGN(size + DSIZE), MINIMUM);

    if (asize <= oldSize)     
        return ptr;

    //Check to see if space in next block
    size_t nextalloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t nextsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    if (!nextalloc && oldSize + nextsize >= asize){
        removeBlock(NEXT_BLKP(ptr));
        PUT(HDRP(ptr), PACK(oldSize+nextsize,1));
        PUT(FTRP(ptr), PACK(oldSize+nextsize,1));    
        return ptr;
    }

    char *bp;

    //Do malloc here with added space and no split block
    asize+=1024;
    if ((bp = find_fit(asize)) != NULL) {
        size_t bsize = GET_SIZE(HDRP(bp));
        PUT(HDRP(bp), PACK(bsize, 1));
        PUT(FTRP(bp), PACK(bsize, 1));
        removeBlock(bp);
    }
    else{
        size_t extendsize = MAX(asize, CHUNKSIZE);
        if ((bp = extend_heap(extendsize)) == NULL)
            return NULL;
        size_t bsize = GET_SIZE(HDRP(bp));
        PUT(HDRP(bp), PACK(bsize, 1));
        PUT(FTRP(bp), PACK(bsize, 1));
        removeBlock(bp);      
    }

    memcpy(bp, oldptr, oldSize-DSIZE);
    mm_free(oldptr);
    return bp;
}


void mm_checkheap(int verbose) 
{
    char *bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp=NEXT_BLKP(bp)){
        printf("Address: %d, Size: %d, Alloc: %d\n", (int)(bp - heap_listp + DSIZE), GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));
    }

    for (bp = heap_listp; *bp != 1; bp+=4){
        printf("Address %d: %d\n", (int)(bp - heap_listp + DSIZE), (int)*bp);
    }
	return;
}











