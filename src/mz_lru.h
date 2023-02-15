#ifndef __mz_lru_h
#define __mz_lru_h

#include "uthash.h"
#include <stdint.h>
#include <stdio.h>
#include <vector>
//
namespace ramulator
{

#define INIT_VAL -1
//#define debug_GHB

// here set_blk_index corresponds to the bit no. to be set in the bit_vector. set_blk_index is blk index so starts from 0, block 0 is LSB.

#define SET_BIT(req, set_blk_index)\
        req->buffer.bit_vector = (req->buffer.bit_vector |  (1ULL << set_blk_index));
//        printf("DEF SET: Current req is %lld, req_bit_vector is %llx, mask is %llx\n",req->pnum, req->buffer.bit_vector,  (1 << set_blk_index));

//check_blk_index starts from 0.
#define CHECK_BIT(req, check_blk_index)\
        ( ( req->buffer.bit_vector &  ( (1ULL<<check_blk_index) | ( (1ULL<<check_blk_index) - 1) )) >> check_blk_index ) ? 1 : 0;

//check_target_memory determines the target memory by looking at the 32 MSBs
#define CHECK_TARGET_MEM(addr)\
        (( addr & (~( (1ULL<<30) - 1)) ) > 0ULL) ? 1 : 0;

#define SET_BIT_BUFF(bv, set_blk_index)\
        bv = ( bv |  (1ULL << set_blk_index));

#define CHECK_BIT_BUFF(bv, check_blk_index)\
        ( ( bv &  ( (1ULL<<check_blk_index) | ( (1ULL<<check_blk_index) - 1) )) >> check_blk_index ) ? 1 : 0;
//        printf("Afetr changing %lld\n",( addr & (~( (1ULL<<30) - 1)) ) );

//	** ONFLY params **
#define REMAP_TABLE_MAX_SIZE 1024
#define REMAP_TABLE_DELAY 10 //for fully-assoc 1024 entry cache
#define HC_BUFFER_DELAY 2


/* lru_list nodes */
struct lru_struct_t
{
    lru_struct_t *next;
    lru_struct_t *prev;
    int64_t pnum;
    long access_clk;
};

//lru_struct_t *ls_bottom = NULL;    //reverse mig

//Mzamd Use tlb[0] as the prefetch buffer
///51200 2K page is 100 MB
//tlb size in pages

#define TLB_SIZE 4096
struct tlb_struct_t{
    int free_tlb_entries;
    lru_struct_t *tlb_list;
    lru_struct_t *tlb_tail;
    int64_t tlb_acc;
    int64_t tlb_miss;
};
//extern tlb_struct_t tlb[1];

#define blk_size_in_bits  6 //this is default transaction granulairty,2^6=64 (cacheline size). Same as tx_bits

//BELOW TWO ARE RELATED calculate carefully
#define blk_index_in_bits  5 //if page size is 2048 and blk size is 64, then 5 bits are used to index the blk, 2^5=32 blks in each page
//give entry size in bytes
#define ENTRY_SIZE 2048
#define ENTRY_SIZE_IN_BITS 11

///*//////// 32MB
//BELOW TWO ARE REALTED
// give cache size in bytes
#define CACHE_SIZE 33554432 //32MB
//Mz: buffer starts at
#define L1_M_SIZE 992 //in MB
#define L2_TAG_DELAY 2
//*/


//give way associativity, for dir map give 1
#define WAY_ASSOC 4
#define IND(r,c) ( (r * WAY_ASSOC) + c)

struct dir_map_cache_entry_t{
    int64_t  resident_addr;
    uint64_t bit_vector;    // will be used by writes
    int64_t acc_time;       // will be used for lru
    int64_t mem_addr;
    int dirty;              // if the prefetched page contains write backs
};
//Dir map cahce ends

//Mzamd GHB starts////////
#define IND_TABLE_SIZE 512
#define GHB_SIZE 512

//Mzamd lru struct for keeping GHB index
struct lru_ind_entry_t
{
    lru_ind_entry_t *next;
    lru_ind_entry_t *prev;
    int64_t ghb_array_ind;
    int64_t delta_key_1;
    int64_t delta_key_2;
};

struct lru_ind_table_t
{
    lru_ind_entry_t *ind_head;
    lru_ind_entry_t *ind_tail;
    int free_ind_entries;
};
//extern lru_ind_table_t ind_table;

//Mzamd fifo q of ghb entries
struct struct_ghb_t
{
    int64_t pg_num;
    int64_t link_index;
    int self_index;
};

//Mzamd GHB ends ///////////////

struct Buffer{          //onfly
  std::vector<std::pair<long,int>> bf_req;
  unsigned int max = 32;
  unsigned int bf_req_size() {return bf_req.size();}
};


#define MAX_EPOCH_HIST 4
struct req_page
{
    int64_t pnum;        /* key */
    int64_t frame_addr;  // for HMA to keep the actual frame addr inside each memory, byte level, always starts from begining of the frame
    int dom;                // current residing domain,  dom+page_frame_no will give the full location
    int64_t th_id;
    int64_t req_time;
    int64_t inst_comp_count;
    int64_t onfly_pair_pnum;    // used to keep hot cold pair info for onfly
    int64_t prev_frame_no;      //rev migration
    struct
    {
        int64_t mig_times_count;
        int64_t total_count; /* total access count */
        int64_t epoch_count; /* epoch access count  */
	    int64_t mig_count;
        int64_t migration_ping;
        short migrated_location;    //if 0, page is in its original memory; if 1, it is in new dom
        short already_migrated;
        short is_hot;                       /* is the page hot */
        short is_top_hot;                       /* is the page TOP hot */
        short cons_hot;                     /* for how many cons was it hot */
        short cons_locked;                  /* for how many cons epochs was it locked */
        short is_l3_locked;                 /* MZ is it locked in L3 */
        int hot_epoch[MAX_EPOCH_HIST];                /* which epoch was hot page; 32768 epochs should be enough for now */
        int curr_index;
        std::vector<int64_t> unique_addr;
        std::vector<std::pair<int64_t, short>> onfly_detail_count;
    }hma_stat;
    
    lru_struct_t *lru_p;                /* pointer to the position in the lru stack */
    UT_hash_handle r_hh;                /* makes this structure hashable */
};

//req_page* req_pg_table = NULL;

} //namespace ramulator

#endif
