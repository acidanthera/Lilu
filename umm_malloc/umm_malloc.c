/* ----------------------------------------------------------------------------
 * umm_malloc.c - a memory allocator for embedded systems (microcontrollers)
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Ralph Hempel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 *
 * R.Hempel 2007-09-22 - Original
 * R.Hempel 2008-12-11 - Added MIT License biolerplate
 *                     - realloc() now looks to see if previous block is free
 *                     - made common operations functions            
 * R.Hempel 2009-03-02 - Added macros to disable tasking
 *                     - Added function to dump heap and check for valid free
 *                        pointer
 * R.Hempel 2009-03-09 - Changed name to umm_malloc to avoid conflicts with
 *                        the mm_malloc() library functions
 *                     - Added some test code to assimilate a free block
 *                        with the very block if possible. Complicated and
 *                        not worth the grief.
 * D.Frank 2014-04-02  - Fixed heap configuration when UMM_TEST_MAIN is NOT set,
 *                        added user-dependent configuration file umm_malloc_cfg.h
 * R.Hempel 2016-12-04 - Add support for Unity test framework
 *                     - Reorganize source files to avoid redundant content
 *                     - Move integrity and poison checking to separate file
 * ----------------------------------------------------------------------------
 */
 
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <string.h>

#include "umm_malloc.h"

#ifdef UMM_DEBUG
#define DBGLOG_DEBUG(format, ...) IOLog(format, ## __VA_ARGS__)
#else
#define DBGLOG_DEBUG(format, ...)
#endif

#define UMM_H_ATTPACKPRE
#define UMM_H_ATTPACKSUF __attribute__((__packed__))

#define UMM_CRITICAL_ENTRY()
#define UMM_CRITICAL_EXIT()

static char default_umm_heap[UMM_MALLOC_CFG_HEAP_SIZE];

/* ------------------------------------------------------------------------- */

UMM_H_ATTPACKPRE typedef struct umm_ptr_t {
  unsigned short int next;
  unsigned short int prev;
} UMM_H_ATTPACKSUF umm_ptr;


UMM_H_ATTPACKPRE typedef struct umm_block_t {
  union {
    umm_ptr used;
  } header;
  union {
    umm_ptr free;
    unsigned char data[4];
  } body;
} UMM_H_ATTPACKSUF umm_block;

#define UMM_FREELIST_MASK (0x8000)
#define UMM_BLOCKNO_MASK  (0x7FFF)

/* ------------------------------------------------------------------------- */

umm_block *umm_heap = NULL;
unsigned short int umm_numblocks = 0;

#define UMM_NUMBLOCKS (umm_numblocks)

/* ------------------------------------------------------------------------ */

#define UMM_BLOCK(b)  (umm_heap[b])

#define UMM_NBLOCK(b) (UMM_BLOCK(b).header.used.next)
#define UMM_PBLOCK(b) (UMM_BLOCK(b).header.used.prev)
#define UMM_NFREE(b)  (UMM_BLOCK(b).body.free.next)
#define UMM_PFREE(b)  (UMM_BLOCK(b).body.free.prev)
#define UMM_DATA(b)   (UMM_BLOCK(b).body.data)

/* ------------------------------------------------------------------------ */

static unsigned short int umm_blocks( size_t size ) {

  /*
   * The calculation of the block size is not too difficult, but there are
   * a few little things that we need to be mindful of.
   *
   * When a block removed from the free list, the space used by the free
   * pointers is available for data. That's what the first calculation
   * of size is doing.
   */

  if( size <= (sizeof(((umm_block *)0)->body)) )
    return( 1 );

  /*
   * If it's for more than that, then we need to figure out the number of
   * additional whole blocks the size of an umm_block are required.
   */

  size -= ( 1 + (sizeof(((umm_block *)0)->body)) );

  return( 2 + size/(sizeof(umm_block)) );
}

/* ------------------------------------------------------------------------ */
/*
 * Split the block `c` into two blocks: `c` and `c + blocks`.
 *
 * - `cur_freemask` should be `0` if `c` used, or `UMM_FREELIST_MASK`
 *   otherwise.
 * - `new_freemask` should be `0` if `c + blocks` used, or `UMM_FREELIST_MASK`
 *   otherwise.
 *
 * Note that free pointers are NOT modified by this function.
 */
static void umm_split_block( unsigned short int c,
    unsigned short int blocks,
    unsigned short int new_freemask ) {

  UMM_NBLOCK(c+blocks) = (UMM_NBLOCK(c) & UMM_BLOCKNO_MASK) | new_freemask;
  UMM_PBLOCK(c+blocks) = c;

  UMM_PBLOCK(UMM_NBLOCK(c) & UMM_BLOCKNO_MASK) = (c+blocks);
  UMM_NBLOCK(c)                                = (c+blocks);
}

/* ------------------------------------------------------------------------ */

static void umm_disconnect_from_free_list( unsigned short int c ) {
  /* Disconnect this block from the FREE list */

  UMM_NFREE(UMM_PFREE(c)) = UMM_NFREE(c);
  UMM_PFREE(UMM_NFREE(c)) = UMM_PFREE(c);

  /* And clear the free block indicator */

  UMM_NBLOCK(c) &= (~UMM_FREELIST_MASK);
}

/* ------------------------------------------------------------------------ */

static void umm_assimilate_up( unsigned short int c ) {

  if( UMM_NBLOCK(UMM_NBLOCK(c)) & UMM_FREELIST_MASK ) {
    /*
     * The next block is a free block, so assimilate up and remove it from
     * the free list
     */

    DBGLOG_DEBUG( "Assimilate up to next block, which is FREE\n" );

    /* Disconnect the next block from the FREE list */

    umm_disconnect_from_free_list( UMM_NBLOCK(c) );

    /* Assimilate the next block with this one */

    UMM_PBLOCK(UMM_NBLOCK(UMM_NBLOCK(c)) & UMM_BLOCKNO_MASK) = c;
    UMM_NBLOCK(c) = UMM_NBLOCK(UMM_NBLOCK(c)) & UMM_BLOCKNO_MASK;
  } 
}

/* ------------------------------------------------------------------------ */

static unsigned short int umm_assimilate_down( unsigned short int c, unsigned short int freemask ) {

  UMM_NBLOCK(UMM_PBLOCK(c)) = UMM_NBLOCK(c) | freemask;
  UMM_PBLOCK(UMM_NBLOCK(c)) = UMM_PBLOCK(c);

  return( UMM_PBLOCK(c) );
}

/* ------------------------------------------------------------------------- */

static void umm_init( void ) {
  /* init heap pointer and size, and memset it to 0 */
  umm_heap = (umm_block *)default_umm_heap;
  umm_numblocks = (UMM_MALLOC_CFG_HEAP_SIZE / sizeof(umm_block));

  /* setup initial blank heap structure */
  {
    /* index of the 0th `umm_block` */
    const unsigned short int block_0th = 0;
    /* index of the 1st `umm_block` */
    const unsigned short int block_1th = 1;
    /* index of the latest `umm_block` */
    const unsigned short int block_last = UMM_NUMBLOCKS - 1;

    /* setup the 0th `umm_block`, which just points to the 1st */
    UMM_NBLOCK(block_0th) = block_1th;
    UMM_NFREE(block_0th)  = block_1th;
    UMM_PFREE(block_0th)  = block_1th;

    /*
     * Now, we need to set the whole heap space as a huge free block. We should
     * not touch the 0th `umm_block`, since it's special: the 0th `umm_block`
     * is the head of the free block list. It's a part of the heap invariant.
     *
     * See the detailed explanation at the beginning of the file.
     */

    /*
     * 1th `umm_block` has pointers:
     *
     * - next `umm_block`: the latest one
     * - prev `umm_block`: the 0th
     *
     * Plus, it's a free `umm_block`, so we need to apply `UMM_FREELIST_MASK`
     *
     * And it's the last free block, so the next free block is 0.
     */
    UMM_NBLOCK(block_1th) = block_last | UMM_FREELIST_MASK;
    UMM_NFREE(block_1th)  = 0;
    UMM_PBLOCK(block_1th) = block_0th;
    UMM_PFREE(block_1th)  = block_0th;

    /*
     * latest `umm_block` has pointers:
     *
     * - next `umm_block`: 0 (meaning, there are no more `umm_blocks`)
     * - prev `umm_block`: the 1st
     *
     * It's not a free block, so we don't touch NFREE / PFREE at all.
     */
    UMM_NBLOCK(block_last) = 0;
    UMM_PBLOCK(block_last) = block_1th;
  }
}

/* ------------------------------------------------------------------------ */

void umm_free( void *ptr ) {

  unsigned short int c;

  /* If we're being asked to free a NULL pointer, well that's just silly! */

  if( (void *)0 == ptr ) {
    DBGLOG_DEBUG( "free a null pointer -> do nothing\n" );

    return;
  }

  /*
   * FIXME: At some point it might be a good idea to add a check to make sure
   *        that the pointer we're being asked to free up is actually within
   *        the umm_heap!
   *
   * NOTE:  See the new umm_info() function that you can use to see if a ptr is
   *        on the free list!
   */

  /* Protect the critical section... */
  UMM_CRITICAL_ENTRY();

  /* Figure out which block we're in. Note the use of truncated division... */

  c = (((char *)ptr)-(char *)(&(umm_heap[0])))/sizeof(umm_block);

  DBGLOG_DEBUG( "Freeing block %6i\n", c );

  /* Now let's assimilate this block with the next one if possible. */

  umm_assimilate_up( c );

  /* Then assimilate with the previous block if possible */

  if( UMM_NBLOCK(UMM_PBLOCK(c)) & UMM_FREELIST_MASK ) {

    DBGLOG_DEBUG( "Assimilate down to next block, which is FREE\n" );

    umm_assimilate_down(c, UMM_FREELIST_MASK);
  } else {
    /*
     * The previous block is not a free block, so add this one to the head
     * of the free list
     */

    DBGLOG_DEBUG( "Just add to head of free list\n" );

    UMM_PFREE(UMM_NFREE(0)) = c;
    UMM_NFREE(c)            = UMM_NFREE(0);
    UMM_PFREE(c)            = 0;
    UMM_NFREE(0)            = c;

    UMM_NBLOCK(c)          |= UMM_FREELIST_MASK;
  }

  /* Release the critical section... */
  UMM_CRITICAL_EXIT();
}

/* ------------------------------------------------------------------------ */

void *umm_malloc( size_t size ) {
  unsigned short int blocks;
  unsigned short int blockSize = 0;

  unsigned short int bestSize;
  unsigned short int bestBlock;

  unsigned short int cf;

  if (umm_heap == NULL) {
    umm_init();
  }

  /*
   * the very first thing we do is figure out if we're being asked to allocate
   * a size of 0 - and if we are we'll simply return a null pointer. if not
   * then reduce the size by 1 byte so that the subsequent calculations on
   * the number of blocks to allocate are easier...
   */

  if( 0 == size ) {
    DBGLOG_DEBUG( "malloc a block of 0 bytes -> do nothing\n" );

    return( (void *)NULL );
  }

  /* Protect the critical section... */
  UMM_CRITICAL_ENTRY();

  blocks = umm_blocks( size );

  /*
   * Now we can scan through the free list until we find a space that's big
   * enough to hold the number of blocks we need.
   *
   * This part may be customized to be a best-fit, worst-fit, or first-fit
   * algorithm
   */

  cf = UMM_NFREE(0);

  bestBlock = UMM_NFREE(0);
  bestSize  = 0x7FFF;

  while( cf ) {
    blockSize = (UMM_NBLOCK(cf) & UMM_BLOCKNO_MASK) - cf;

    DBGLOG_DEBUG( "Looking at block %6i size %6i\n", cf, blockSize );

#if defined UMM_BEST_FIT
    if( (blockSize >= blocks) && (blockSize < bestSize) ) {
      bestBlock = cf;
      bestSize  = blockSize;
    }
#elif defined UMM_FIRST_FIT
    /* This is the first block that fits! */
    if( (blockSize >= blocks) )
      break;
#else
#  error "No UMM_*_FIT is defined - check umm_malloc_cfg.h"
#endif

    cf = UMM_NFREE(cf);
  }

  if( 0x7FFF != bestSize ) {
    cf        = bestBlock;
    blockSize = bestSize;
  }

  if( UMM_NBLOCK(cf) & UMM_BLOCKNO_MASK && blockSize >= blocks ) {
    /*
     * This is an existing block in the memory heap, we just need to split off
     * what we need, unlink it from the free list and mark it as in use, and
     * link the rest of the block back into the freelist as if it was a new
     * block on the free list...
     */

    if( blockSize == blocks ) {
      /* It's an exact fit and we don't neet to split off a block. */
      DBGLOG_DEBUG( "Allocating %6i blocks starting at %6i - exact\n", blocks, cf );

      /* Disconnect this block from the FREE list */

      umm_disconnect_from_free_list( cf );

    } else {
      /* It's not an exact fit and we need to split off a block. */
      DBGLOG_DEBUG( "Allocating %6i blocks starting at %6i - existing\n", blocks, cf );

      /*
       * split current free block `cf` into two blocks. The first one will be
       * returned to user, so it's not free, and the second one will be free.
       */
      umm_split_block( cf, blocks,
          UMM_FREELIST_MASK/*new block is free*/);

      /*
       * `umm_split_block()` does not update the free pointers (it affects
       * only free flags), but effectively we've just moved beginning of the
       * free block from `cf` to `cf + blocks`. So we have to adjust pointers
       * to and from adjacent free blocks.
       */

      /* previous free block */
      UMM_NFREE( UMM_PFREE(cf) ) = cf + blocks;
      UMM_PFREE( cf + blocks ) = UMM_PFREE(cf);

      /* next free block */
      UMM_PFREE( UMM_NFREE(cf) ) = cf + blocks;
      UMM_NFREE( cf + blocks ) = UMM_NFREE(cf);
    }
  } else {
    /* Out of memory */

    DBGLOG_DEBUG(  "Can't allocate %5i blocks\n", blocks );

    /* Release the critical section... */
    UMM_CRITICAL_EXIT();

    return( (void *)NULL );
  }

  /* Release the critical section... */
  UMM_CRITICAL_EXIT();

  return( (void *)&UMM_DATA(cf) );
}

/* ------------------------------------------------------------------------ */

void *umm_realloc( void *ptr, size_t size ) {

  unsigned short int blocks;
  unsigned short int blockSize;

  unsigned short int c;

  size_t curSize;

  if (umm_heap == NULL) {
    umm_init();
  }

  /*
   * This code looks after the case of a NULL value for ptr. The ANSI C
   * standard says that if ptr is NULL and size is non-zero, then we've
   * got to work the same a malloc(). If size is also 0, then our version
   * of malloc() returns a NULL pointer, which is OK as far as the ANSI C
   * standard is concerned.
   */

  if( ((void *)NULL == ptr) ) {
    DBGLOG_DEBUG( "realloc the NULL pointer - call malloc()\n" );

    return( umm_malloc(size) );
  }

  /*
   * Now we're sure that we have a non_NULL ptr, but we're not sure what
   * we should do with it. If the size is 0, then the ANSI C standard says that
   * we should operate the same as free.
   */

  if( 0 == size ) {
    DBGLOG_DEBUG( "realloc to 0 size, just free the block\n" );

    umm_free( ptr );

    return( (void *)NULL );
  }

  /* Protect the critical section... */
  UMM_CRITICAL_ENTRY();

  /*
   * Otherwise we need to actually do a reallocation. A naiive approach
   * would be to malloc() a new block of the correct size, copy the old data
   * to the new block, and then free the old block.
   *
   * While this will work, we end up doing a lot of possibly unnecessary
   * copying. So first, let's figure out how many blocks we'll need.
   */

  blocks = umm_blocks( size );

  /* Figure out which block we're in. Note the use of truncated division... */

  c = (((char *)ptr)-(char *)(&(umm_heap[0])))/sizeof(umm_block);

  /* Figure out how big this block is... */

  blockSize = (UMM_NBLOCK(c) - c);

  /* Figure out how many bytes are in this block */

  curSize   = (blockSize*sizeof(umm_block))-(sizeof(((umm_block *)0)->header));

  /*
   * Ok, now that we're here, we know the block number of the original chunk
   * of memory, and we know how much new memory we want, and we know the original
   * block size...
   */

  if( blockSize == blocks ) {
    /* This space intentionally left blank - return the original pointer! */

    DBGLOG_DEBUG( "realloc the same size block - %i, do nothing\n", blocks );

    /* Release the critical section... */
    UMM_CRITICAL_EXIT();

    return( ptr );
  }

  /*
   * Now we have a block size that could be bigger or smaller. Either
   * way, try to assimilate up to the next block before doing anything...
   *
   * If it's still too small, we have to free it anyways and it will save the
   * assimilation step later in free :-)
   */

  umm_assimilate_up( c );

  /*
   * Now check if it might help to assimilate down, but don't actually
   * do the downward assimilation unless the resulting block will hold the
   * new request! If this block of code runs, then the new block will
   * either fit the request exactly, or be larger than the request.
   */

  if( (UMM_NBLOCK(UMM_PBLOCK(c)) & UMM_FREELIST_MASK) &&
      (blocks <= (UMM_NBLOCK(c)-UMM_PBLOCK(c)))    ) {

    /* Check if the resulting block would be big enough... */

    DBGLOG_DEBUG( "realloc() could assimilate down %i blocks - fits!\n\r", c-UMM_PBLOCK(c) );

    /* Disconnect the previous block from the FREE list */

    umm_disconnect_from_free_list( UMM_PBLOCK(c) );

    /*
     * Connect the previous block to the next block ... and then
     * realign the current block pointer
     */

    c = umm_assimilate_down(c, 0);

    /*
     * Move the bytes down to the new block we just created, but be sure to move
     * only the original bytes.
     */

    memmove( (void *)&UMM_DATA(c), ptr, curSize );

    /* And don't forget to adjust the pointer to the new block location! */

    ptr    = (void *)&UMM_DATA(c);
  }

  /* Now calculate the block size again...and we'll have three cases */

  blockSize = (UMM_NBLOCK(c) - c);

  if( blockSize == blocks ) {
    /* This space intentionally left blank - return the original pointer! */

    DBGLOG_DEBUG( "realloc the same size block - %i, do nothing\n", blocks );

  } else if (blockSize > blocks ) {
    /*
     * New block is smaller than the old block, so just make a new block
     * at the end of this one and put it up on the free list...
     */

    DBGLOG_DEBUG( "realloc %i to a smaller block %i, shrink and free the leftover bits\n", blockSize, blocks );

    umm_split_block( c, blocks, 0 );
    umm_free( (void *)&UMM_DATA(c+blocks) );
  } else {
    /* New block is bigger than the old block... */

    void *oldptr = ptr;

    DBGLOG_DEBUG( "realloc %i to a bigger block %i, make new, copy, and free the old\n", blockSize, blocks );

    /*
     * Now umm_malloc() a new one, copy the old data to the new block, and
     * free up the old block, but only if the malloc was sucessful!
     */

    if( (ptr = umm_malloc( size )) ) {
      memcpy( ptr, oldptr, curSize );
    }

    umm_free( oldptr );
  }

  /* Release the critical section... */
  UMM_CRITICAL_EXIT();

  return( ptr );
}

/* ------------------------------------------------------------------------ */

void *umm_calloc( size_t num, size_t item_size ) {
  void *ret;

  ret = umm_malloc((size_t)(item_size * num));
  if (ret)
      memset(ret, 0x00, (size_t)(item_size * num));

  return ret;
}

/* ------------------------------------------------------------------------ */

