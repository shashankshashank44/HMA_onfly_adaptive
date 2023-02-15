#ifndef __hma_h
#define __hma_h

//#include <stdio.h>
//#include <unistd.h>
//#include <stdlib.h>
//#include <limits.h>
#include "Config.h"
#include "uthash.h"
#include "mz_lru.h"
#include <list>
#include <vector>

using namespace ramulator;
using namespace std;
//namespace ramulator
//{

/* sizes in bytes */
#define GB 1073741824L
#define MB 1048576L
#define KB 1024L
#define PCM_OFFSET 1073741824
typedef struct sim_struct
{
    unsigned long long total_time;
    unsigned long long epoch_time;
    int epoch;
}sim_struct_t;

/* timing type, large enough to hold ns */
//TODO check int64_t and unsigned long long
typedef unsigned long long timing_t;

/* possible memory domains */
//typedef enum {DRAM3D, DDR4, PCM} mem_id_t;

/* lru_list nodes */
/*RAM_HMA: no more used */
typedef struct lru_struct2
{
    struct lru_struct2 *next;
    struct lru_struct2 *prev;
    int64_t pnum;
}lru_struct_t2;

/* memory packets are used to communicate with the memory domains */
typedef struct mem_packet
{
    int addr_type;
    char rwu;
    int64_t addr;
    int size;
    int flags;
    int64_t tick;
    int packet_id;
}mem_packet_t;

/* page structure to hold info about each page */
/* RAM_HMA: this page structure is not used, instead req_page structure defined in mz_lru.h has been used */
#define MAX_EPOCH_HIST 4
struct page
{
    int64_t pnum;        /* key */
    int64_t total_count; /* total access count */
    int64_t epoch_count; /* epoch access count  */
    int64_t mig_count;	/* keeps hotness count until migration */
    short is_hot;                       /* is the page hot */
    short is_top_hot;                       /* is the page TOP hot */
    short cons_hot;                     /* for how many cons was it hot */
    short cons_locked;                  /* for how many cons epochs was it locked */
    short is_l3_locked;                 /* MZ is it locked in L3 */
    int dom;                            /* current residing domain */
    lru_struct_t2 *lru_p;                /* pointer to the position in the lru stack */
    int hot_epoch[MAX_EPOCH_HIST];                /* which epoch was hot page; 32768 epochs should be enough for now */
    UT_hash_handle hh;                  /* makes this structure hashable */
};

/* hash table fo the pages */
//struct page *ptable=NULL;

/*RAM_HMA: belwo q will be used to hold free lists*/
struct Queue_free_frame {
    list<int64_t> ff;
    unsigned int max; 
    unsigned int size() {return ff.size();}
};

/* memory struct - to represent memory objects */
typedef struct mem_struct
{
    int memory;
    int mem_dom;
    int64_t size;    /* size in bytes */
    int lat;                        /* latency in ns */
    double bwidth;                  /* bandiwdth in GB/s */
    int64_t nframes;          /* number of frames = size/page_size */
    int64_t free_frames;      /* number of free frames */
    short is_prim;                  /* is it primary domain */

    int64_t num_accesses;
    int64_t num_reads;   /* counters for accesses */
    int64_t num_writes;
    int64_t num_unknown;
    int64_t num_kern_accesses;
    int64_t num_kern_reads;
    int64_t num_kern_writes;
    Queue_free_frame free_frame_q;  /* for each memory type, a free frame q will be kept*/
}mem_t;


/* hma struct - holds configuration, stats, etc. */
struct hma_struct
{
    struct
    {
        int prim_dom;           /* primary domain */
        int thold;              /* hotness treshold */
        int64_t epoch;  /*  keep total epoch count */
        int64_t t_epoch;  /* epoch time */
        int num_mem;
        int page_size;
		int page_transfer_limit;
        mem_t *mem_list;        /* array of memories */
//        void (*policy) (struct hma_struct*);       /* func. pointer to the policy */
        
        int lck_hot_length;     /* locking specific stuff */
        int lck_lock_length;
    }config;
    struct
    {
        struct req_page** hot_pages;    //  use req_page instead of page to conform with previous prefetch work
        struct req_page** top_hot_pages;
        int64_t num_hp;
        int64_t num_thp;
        int64_t total_mem_time;
        int64_t mem_epoch_time;
        int64_t ll_time;
        int64_t total_pages_transfered;
        int64_t pages_migrated = 0;
        int64_t pages_migrated_temp = 0;    //interval (adaptive migration)
        int64_t reverse_migration;
        int64_t pages_addr_recon;
        int64_t total_cline_invalidations;
        int64_t hot_pages_migrated=0;
        int64_t cold_pages_migrated=0;
		int64_t same_page_selected;
        unsigned int failed_accesses;   /* just to keep track of possible not mapped pages */ 
    }stats;
    struct
    {
        int highest_l2m_hotness;
        int mig_status;
        int ar_status;
        int mig_type;
        int64_t hot_pg_num;
        int64_t hot_frame_addr;
        int hot_pg_core_id;
        int64_t cold_pg_num;
        int64_t cold_frame_addr;
        int cold_pg_core_id;
		int64_t hot_partial_count;
		int64_t cold_partial_count;
		int64_t total_buffer_hit_count;
        int64_t no_mig_reqs;
        int64_t ar_page_1;
        int64_t ar_page_2;
		int remap_table_size;	// holds the current size of the remap table
        int64_t next_hot_pg_num;
        int reverse_migration;  //flag to check if it is reverse migration
    }onfly;
    
    std::vector<pair<int64_t, int>> threshold_reach;    //this vector keeps track of all pages crossed the threshold, and the migration status at that time.
    lru_struct_t *lru_list; // lru_struct_t defined in mz_lru.h
    lru_struct_t *lru_list_tail;
};
typedef struct hma_struct hma_struct_t;

struct hma_cfg_struct
{
    int prim_dom;           /* primary domain */
    int thold;              /* hotness treshold */
    int64_t t_epoch;  /* epoch time */
    int num_mem;
    int page_size;
    int page_transfer_limit;
    int lock_length;
    int hot_length;
};
struct hma_mem_cfg_struct
{
    int mem_dom;
    int size;
    int lat;
    int bwidth;
    int is_prim;
};

typedef struct hma_cfg_struct hma_cfg_t;
typedef struct hma_mem_cfg_struct hma_mem_cfg_t;

/* hash table fo the pages */
extern struct req_page *ptable; //  use req_page instead of page to conform with previous prefetch work
extern hma_cfg_t hma_cfg;
//extern hma_mem_cfg_struct hma_cfg;
extern hma_mem_cfg_t hma_dram3d_cfg;
extern hma_mem_cfg_t hma_ddr4_cfg;
extern hma_mem_cfg_t hma_pcm_cfg;

/* fully associative TLB */
//#define TLB_SIZE 1024
/*
typedef struct tlb_struct{
    int free_tlb_entries;
    lru_struct_t2 *tlb_list;
    lru_struct_t2 *tlb_tail;
    int64_t tlb_acc;
    int64_t tlb_miss;
}tlb_struct_t;
*/
//extern tlb_struct_t tlb[8];
/* test case how many Top Hot Pages in TLB at the end of epoch*/
extern int hp_in_tlb[MAX_EPOCH_HIST];




void sim_pause(void);
//void hma_config_and_init(hma_struct_t *hma);
void hma_config_and_init(hma_struct_t *hma, const Config& hmaconfig, uint64_t l1m_size, uint64_t l1m_bw, uint64_t l2m_size, uint64_t l2m_bw);
void hma_print_mem_list(hma_struct_t *hma);
void hma_reset_stats(hma_struct_t *hma);
void hma_print_config(hma_struct_t *hma);
void hma_add_page_lru(hma_struct_t *hma, struct req_page *p, long clk);
bool remove_node(hma_struct_t *hma, int64_t page_number);
long hma_search_lru_page(hma_struct_t *hma, int64_t page_number);
struct req_page *hma_get_lru_page(hma_struct_t *hma);
int64_t hma_default_policy(hma_struct_t *hma, long clock);
void hma_print_top_hot_pages(hma_struct_t *hma);
void hma_print_hot_pages(hma_struct_t *hma);
int ht_pnum_sort(struct req_page *a, struct req_page *b);
int ht_tcount_sort(struct req_page *a, struct req_page *b);
int ht_ecount_sort(struct req_page *a, struct req_page *b);
unsigned long long int toint(char a[]);
void print_usage(void);
void hma_set_hot_pages(hma_struct_t *hma);
struct req_page * hma_handler(hma_struct_t *hma, int64_t orig_req_addr, int core_id);
void hma_print_npt(void);
int64_t hma_flush_caches(struct req_page *p);

/*RAM_HMA functions*/
void hma_inc_npt(int from, int to);
int64_t hma_policy_invoker(hma_struct_t *hma, long clock);
void print_all_lru_list_pages(hma_struct_t *hma);
//For two types of hma we need two types of count keeper
void hma_epoch_count_keeper(hma_struct_t *hma, req_page *p, int64_t acc_cycle);
int hma_onfly_count_keeper(hma_struct_t *hma, req_page *p, int64_t acc_cycle, int req_target_mem);

/*onfly functions*/
bool hma_onfly_hotcold_init(hma_struct_t *hma, req_page *p, int64_t acc_cycle, long clock);
int hma_onfly_hotcold_check_and_init(hma_struct_t *hma, req_page *p, long clock);
int hma_onfly_opt_hotcold_check_and_init(hma_struct_t *hma, long clock);




extern  hma_struct_t hma;                   //Mz added
extern int8_t hma_policy_time;              //Mz added
extern int64_t epoch_time;
extern int64_t epoch;

/* page transfer counters */
extern int64_t npt_pcm_ddr;
extern int64_t npt_pcm_3ddram;
extern int64_t npt_ddr_3ddram;
extern int64_t npt_3ddram_ddr;
extern int64_t npt_3ddram_pcm;
extern int64_t npt_ddr_pcm;
extern int64_t *npt[6]; 
extern  int8_t kernel_time;                        // is it kernel time

//}   //namesapce ramulator ends

#endif

