#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include<stdint.h>
#include <ctype.h>
#include "hma.h"
//#include "moola.h"


using namespace std;
//namespace ramulator {

// Mz - hma configurations
hma_cfg_t hma_cfg;
//hma_mem_cfg_struct hma_cfg;

hma_mem_cfg_t hma_dram3d_cfg;
hma_mem_cfg_t hma_ddr4_cfg;
hma_mem_cfg_t hma_pcm_cfg;
int8_t kernel_time = 0;

// page transfer counters
int64_t npt_pcm_ddr;
int64_t npt_pcm_3ddram;
int64_t npt_ddr_3ddram;
int64_t npt_3ddram_ddr;
int64_t npt_3ddram_pcm;
int64_t npt_ddr_pcm;
int64_t *npt[6];

//Mz making epoch information global
int8_t hma_policy_time = 0;
int64_t epoch_time = 1;
int64_t epoch = 0;
double cpu_freq_ghz = 3.2;

long node_count = 0;
lru_struct_t *ls_bottom = NULL;    //reverse mig


void hma_inc_npt(int from, int to)
{
    if(from == 2 && to == 0)
        npt_pcm_3ddram++;
    if(from == 2 && to == 1)
        npt_pcm_ddr++;
    if(from == 1 && to == 0)
        npt_ddr_3ddram++;
    if(from == 0 && to == 1)
        npt_3ddram_ddr++;
    if(from == 0 && to == 2)
        npt_3ddram_pcm++;
    if(from == 1 && to == 2)
        npt_ddr_pcm++;
}

/* hash table fo the pages */
//struct page *ptable=NULL; // RAM_HMA: defined in Main.cpp

void sim_pause(void)
{
    char c;
    printf("\nPress enter to continue...\n\n");
    c=getchar();
}

void hma_config_and_init(hma_struct_t *hma, const Config& hmaconfig, uint64_t l1m_size, uint64_t l1m_bw, uint64_t l2m_size, uint64_t l2m_bw)
{
    //TODO: CPU FREQ from config files ramulator!!!!!
    int64_t i, frame_addr;

    /* set configuration */
    hma->config.prim_dom = 0;
    hma->config.thold = hmaconfig.get_hma_hotness_thold();
    hma->config.epoch = 0;
//    hma->config.t_epoch = hmaconfig.get_epoch_length() * cpu_freq_ghz * 1000000;  //t_epoch in cycles, get_epoch_length gives in ms
    hma->config.t_epoch = hmaconfig.get_epoch_length() * cpu_freq_ghz * 1000;  //t_epoch in cycles, get_epoch_length gives in us
    hma->config.num_mem = hmaconfig.get_num_memories();
    cout <<"\n"<< hma->config.num_mem << "\n";
    hma->config.page_size = hmaconfig.get_page_size();
    hma->config.page_transfer_limit = hmaconfig.get_page_transfer_limit();
    hma->config.lck_lock_length = hmaconfig.get_lock_length();
    hma->config.lck_hot_length = hmaconfig.get_hot_length();

    hma->config.mem_list = new mem_t[hma->config.num_mem];
    /* 3DDRAM */
    //mem_t *dram3d=(mem_t*)malloc(sizeof(mem_t));
    mem_t *dram3d = new mem_t;
    dram3d->memory = 0;
    dram3d->mem_dom = 0;
    dram3d->size = l1m_size; //TODO: check for conversion to bytes
    dram3d->lat = 160; //no. of cycles in 50ns @ 3.2GHz, previously hma_dram3d_cfg.lat*cpu_freq_ghz; TODO: changed based on timing models
    dram3d->bwidth = l1m_bw; //TODO: check for units
    dram3d->nframes = dram3d->size/hma->config.page_size;
    dram3d->free_frames = dram3d->nframes;
    dram3d->is_prim = 1;  //hma_dram3d_cfg.is_prim;

    dram3d->num_accesses = 0;
    dram3d->num_reads = 0;   /* counters for accesses */
    dram3d->num_writes = 0;
    dram3d->num_unknown = 0;
    dram3d->num_kern_accesses = 0;
    dram3d->num_kern_reads = 0;
    dram3d->num_kern_writes = 0;
    dram3d->free_frame_q.max = dram3d->nframes;
    for(i = 0; i < dram3d->free_frame_q.max; i++)
    {
        frame_addr = i * hma->config.page_size;
        dram3d->free_frame_q.ff.push_back(frame_addr);
    }

    /* DDR4 */
    //mem_t *ddr4=(mem_t*)malloc(sizeof(mem_t));
    mem_t *ddr4 = new mem_t;
    ddr4->memory=1;
    ddr4->mem_dom=1;    //hma_ddr4_cfg.mem_dom;
    ddr4->size=l2m_size;    //hma_ddr4_cfg.size*MB;
    ddr4->lat=256; //no. of cycles in 80ns(for pcm) @ 3.2GHz  //hma_ddr4_cfg.lat*cpu_freq_ghz;
    ddr4->bwidth=l2m_bw;    //hma_ddr4_cfg.bwidth;
    ddr4->nframes=ddr4->size/hma->config.page_size;
    ddr4->free_frames=ddr4->nframes;
    ddr4->is_prim=0;    //hma_ddr4_cfg.is_prim;
    ddr4->num_accesses=0;
    ddr4->num_reads=0;   /* counters for accesses */
    ddr4->num_writes=0;
    ddr4->num_unknown=0;
    ddr4->num_kern_accesses=0;
    ddr4->num_kern_reads=0;
    ddr4->num_kern_writes=0;
    ddr4->free_frame_q.max = ddr4->nframes;
    for(i = 0; i < ddr4->free_frame_q.max; i++)
    {
        frame_addr = i * hma->config.page_size;
        ddr4->free_frame_q.ff.push_back(frame_addr);
    }

    /* PCM */
    if(hma->config.num_mem == 3)
    {
        //mem_t *pcm=(mem_t*)malloc(sizeof(mem_t));
        mem_t *pcm = new mem_t;
        pcm->memory=2;
        pcm->mem_dom=hma_pcm_cfg.mem_dom;
        pcm->size=hma_pcm_cfg.size*MB;
        pcm->lat=hma_pcm_cfg.lat*cpu_freq_ghz;
        pcm->bwidth=hma_pcm_cfg.bwidth;
        pcm->nframes=pcm->size/hma->config.page_size;
        pcm->free_frames=pcm->nframes;
        pcm->is_prim=hma_pcm_cfg.is_prim;
        pcm->num_accesses=0;
        pcm->num_reads=0;   /* counters for accesses */
        pcm->num_writes=0;
        pcm->num_unknown=0;
        pcm->num_kern_accesses=0;
        pcm->num_kern_reads=0;
        pcm->num_kern_writes=0;
        pcm->free_frame_q.max = pcm->nframes;
        for(i = 0; i < pcm->free_frame_q.max; i++)
        {
            frame_addr = i * hma->config.page_size;
            pcm->free_frame_q.ff.push_back(frame_addr);
        }
        hma->config.mem_list[2]=*pcm;
    }

    /* add memories to mem_list */
    hma->config.mem_list[0]=*dram3d;
    hma->config.mem_list[1]=*ddr4;

    cout << "last l1m frame full addr: " << hma->config.mem_list[0].free_frame_q.ff.back() << endl;
    cout << "last l2m frame full addr: " << hma->config.mem_list[1].free_frame_q.ff.back() << endl;

    /* initialize */
    hma->stats.hot_pages = NULL;
    hma->stats.top_hot_pages = NULL;
    hma->stats.num_hp = 0;
    hma->stats.num_thp = 0;
    hma->stats.pages_migrated = 0;
    hma->stats.reverse_migration = 0;
    hma->stats.pages_addr_recon = 0;
	hma->stats.total_cline_invalidations = 0;
    hma->stats.hot_pages_migrated = 0;
    hma->stats.cold_pages_migrated = 0;
    hma->stats.same_page_selected = 0;
    hma->lru_list = NULL;
    hma->lru_list_tail = NULL;
    //onfly:
    hma->onfly.highest_l2m_hotness = hma->config.thold; //initialize l2m highest hotness with provided threshold
    hma->onfly.next_hot_pg_num = -1;
    hma->onfly.mig_status = 0;
    hma->onfly.mig_type = 0;
    hma->onfly.hot_pg_num = -1;
    hma->onfly.hot_frame_addr = -1;
    hma->onfly.hot_pg_core_id = -1;
    hma->onfly.cold_pg_num = -1;
    hma->onfly.cold_frame_addr = -1;
    hma->onfly.cold_pg_core_id = -1;
    hma->onfly.hot_partial_count = 0;
    hma->onfly.cold_partial_count = 0;
    hma->onfly.total_buffer_hit_count = 0;
    hma->onfly.no_mig_reqs = 0; 

    hma->onfly.ar_status = 0;
    hma->onfly.ar_page_1 = -1;
    hma->onfly.ar_page_2 = -1;
	hma->onfly.remap_table_size = 0;
    hma->onfly.reverse_migration = 0;

    npt_pcm_ddr = 0;
        npt[0]=&npt_pcm_ddr;
    npt_pcm_3ddram = 0;
        npt[1]=&npt_pcm_3ddram;
    npt_ddr_3ddram = 0;
        npt[2]=&npt_ddr_3ddram;
    npt_3ddram_ddr = 0;
        npt[3]=&npt_3ddram_ddr;
    npt_3ddram_pcm = 0;
        npt[4]=&npt_3ddram_pcm;
    npt_ddr_pcm = 0;
        npt[5]=&npt_ddr_pcm;

    hma_print_config(hma);
    hma_print_mem_list(hma);
}

void hma_print_mem_list(hma_struct_t *hma)
{
    int i;
    printf("\n#Memory Configuration#\n");
    printf("----------------------------------------\n");
    for(i=0; i<hma->config.num_mem; i++)
    {
        printf("Memory: %d\n",hma->config.mem_list[i].memory);
        printf("\tlevel: %d\n",hma->config.mem_list[i].mem_dom);
        printf("\tsize[bytes]: %ld\n",hma->config.mem_list[i].size);
        printf("\tlatency[ns]: %f\n",hma->config.mem_list[i].lat/cpu_freq_ghz);
        printf("\tbandwidth[GB/s]: %.2f\n",hma->config.mem_list[i].bwidth);
        printf("\t#frames: %ld\n",hma->config.mem_list[i].nframes);
        printf("\t#free frames: %ld\n",hma->config.mem_list[i].free_frames);
        printf("\tprimary?: %d\n",hma->config.mem_list[i].is_prim);
        printf("\n");
        /*if (i == 0)
        {
            int j;
            for(j = 0; j < hma->config.mem_list[i].free_frame_q.max; j++)
            {
                //cout << "\t" << hma->config.mem_list[i].free_frame_q.ff.back() << "\n";
                //hma->config.mem_list[i].free_frame_q.ff.pop_back();
                cout << "\t" << hma->config.mem_list[i].free_frame_q.ff.front() << "\n";
                hma->config.mem_list[i].free_frame_q.ff.pop_front();
            }
        }*/
    }
    printf("----------------------------------------\n");
}

void hma_print_npt(void)
{
    printf("NPT PCM-DDR:%ld PCM-3D:%ld DDR-3D:%ld 3D-DDR:%ld 3D-PCM:%ld DDR-PCM:%ld\n",
            npt_pcm_ddr, npt_pcm_3ddram, npt_ddr_3ddram, npt_3ddram_ddr, npt_3ddram_pcm, npt_ddr_pcm);
}

//RAM_HMA: modified
void hma_reset_stats(hma_struct_t *hma)
{
    //int i;
    struct req_page *p;

    npt_pcm_ddr = 0;
    npt_pcm_3ddram = 0;
    npt_ddr_3ddram = 0;
    npt_3ddram_ddr = 0;
    npt_3ddram_pcm = 0;
    npt_ddr_pcm = 0;


    // RAM_HMA: resest: iterate through all pages, clear the hot flags and epoch_count 
    for(p = ptable; p != NULL; p = (struct req_page*)(p->r_hh.next) )
    {
        p->hma_stat.is_hot = 0;
        p->hma_stat.is_top_hot = 0;
        p->hma_stat.epoch_count = 0;
    }


    // reset hma stats in the epoch 
    hma->stats.num_hp = 0;
    hma->stats.num_thp = 0;
    hma->stats.mem_epoch_time = 0;
    //onfly:
    hma->onfly.highest_l2m_hotness = hma->config.thold; //reset l2m highest hotness with provided threshold
    hma->onfly.next_hot_pg_num = -1;
    hma->onfly.mig_type = 0;
    hma->onfly.hot_pg_num = -1;
    hma->onfly.hot_frame_addr = -1;
    hma->onfly.cold_pg_num = -1;
    hma->onfly.cold_frame_addr = -1;

    //RAM_HMA: just deleting the list of pointers, not the actual pages they point to
    //since those are actual pages in hma structure 
    delete[] hma->stats.top_hot_pages;
    delete[] hma->stats.hot_pages;
}

void hma_print_config(hma_struct_t *hma)
{
    printf("\n#HMA Configuration#\n");
    printf("----------------------------------------\n");
    printf("Number of memories: %d\n",hma->config.num_mem);
    printf("Primary domain: %d\n",hma->config.prim_dom);
    printf("Epoch length [cycle]: %ld\n",hma->config.t_epoch);
    //printf("Epoch length [ms]: %f\n",hma->config.t_epoch/(1000000*cpu_freq_ghz));
    printf("Epoch length [us]: %f\n",hma->config.t_epoch/(1000 * cpu_freq_ghz));    // now epoch length in us
    printf("Hotness threshold : %d\n",hma->config.thold);
    printf("Pagesize [bytes]: %d\n",hma->config.page_size);
    printf("Page transfer limit : %d\n",hma->config.page_transfer_limit);
    printf("CPU frequency [GHz]: %f\n",cpu_freq_ghz);
    printf("----------------------------------------\n");
}

void hma_add_page_lru(hma_struct_t *hma, struct req_page *p, long clk)
{
    /* first element in LRU list */
    if(hma->lru_list == NULL)
    {
        node_count++;
        hma->lru_list = new lru_struct_t;
        hma->lru_list->next = NULL;
        hma->lru_list->prev = NULL;
        hma->lru_list_tail = hma->lru_list;
        hma->lru_list->pnum = p->pnum;
        hma->lru_list->access_clk = clk;
        p->lru_p = hma->lru_list;
        ls_bottom = hma->lru_list;
    }
    /* for all check if exists and relocate to the end */
    else
    {
        /* if not in LRU list, append*/
        if(p->lru_p == NULL)
        {
           //printf("No entry: pnum=%lld\n",p->pnum);
            node_count++;
            lru_struct_t *t = new lru_struct_t;
            t->next = NULL;
            t->prev = hma->lru_list_tail;
            t->pnum = p->pnum;
            t->access_clk = clk;
            hma->lru_list_tail->next = t;
            hma->lru_list_tail = t;
            p->lru_p = t;
            
        }
        /* it has a place in LRU list, p->lru_p knows where */
        else
        {
            // I am the last one
            if(p->lru_p->next == NULL)
            {
                p->lru_p->access_clk = clk;
            }
            // I am the first one
            else if (p->lru_p->prev == NULL)
            {
                hma->lru_list = p->lru_p->next;
                p->lru_p->next->prev = NULL;
                p->lru_p->next = NULL;
                p->lru_p->prev = hma->lru_list_tail;
                p->lru_p->access_clk = clk;
                hma->lru_list_tail->next = p->lru_p;
                hma->lru_list_tail = p->lru_p;
            }
            else
            {
            // If in the middle
                p->lru_p->prev->next = p->lru_p->next;
                p->lru_p->next->prev = p->lru_p->prev;
                p->lru_p->prev = hma->lru_list_tail;
                p->lru_p->access_clk = clk;
                hma->lru_list_tail->next = p->lru_p;
                hma->lru_list_tail = p->lru_p;
                p->lru_p->next = NULL;
            }

            // Sanity check
            if (p->lru_p->pnum != p->pnum)
                //cout<<"FATAL LRU pnum mismatch"<<flush<<"\n";
                //exit(0);
                fprintf(stderr, "FATAL, LRU pnum mismatch\n");
        }
    }
   /* 
    lru_struct_t2 *w;
    w=hma->lru_list;
    printf("\np->pnum=%lld, tail=%p, tail_pnum=%lld\n",p->pnum,hma->lru_list_tail, hma->lru_list_tail->pnum);
    int n=1;
    while(w!=NULL)
    {
        printf("%d:%lld->",n,w->pnum);
        w=w->next;
        n++;
    }
    printf("NULL\n");*/
}

struct req_page *hma_get_lru_page(hma_struct_t *hma)
{
    int64_t lru_pnum;
    struct req_page *lrup;
    lru_pnum = hma->lru_list->pnum;
    
    HASH_FIND(r_hh, ptable, &lru_pnum, sizeof(int64_t), lrup);

    hma->lru_list->next->prev = NULL;
    hma->lru_list = hma->lru_list->next;
    //free(lrup->lru_p);
    delete lrup->lru_p;
    lrup->lru_p = NULL;
    return lrup;
}

bool remove_node(hma_struct_t *hma, int64_t page_number)
{
    /*lru_struct_t *ls = ls_bottom;
    while(ls == hma->lru_list_tail)
    {
        if(ls->pnum == page_number)
        {
            if(ls->prev == NULL)    //first node
            {
                ls->next->prev = NULL;
                ls_bottom = ls->next;   
                //change bottom
            }
            else if(ls->next == NULL)    //last
            {
                ls->prev->next = NULL;
                hma->lru_list_tail = ls->prev;
                //also change tail
            }
            else
            {
                ls->next->prev = ls->prev;
                ls->prev->next = ls->next;
            }
            delete ls;
            return true;
        }        

        ls = ls->next;
    }  
    return false;*/

    struct req_page *lrup;

    HASH_FIND(r_hh, ptable, &page_number, sizeof(int64_t), lrup);
    
    if(lrup->lru_p->prev == NULL)
    {
        lrup->lru_p->next->prev = NULL;
        if(lrup->lru_p == hma->lru_list)
            hma->lru_list = lrup->lru_p->next;    //was hma->lru_list->next;
        delete lrup->lru_p;
        lrup->lru_p = NULL;
        return true;
    }    
    else if(lrup->lru_p->next == NULL)
    {
        lrup->lru_p->prev->next = NULL;
        if(lrup->lru_p == hma->lru_list_tail)
            hma->lru_list_tail = lrup->lru_p->prev;
        delete lrup->lru_p;
        lrup->lru_p = NULL;
        return true;
    }
    else
    {
        lrup->lru_p->prev->next = lrup->lru_p->next;
        lrup->lru_p->next->prev = lrup->lru_p->prev;
        delete lrup->lru_p;
        lrup->lru_p = NULL;
        return true;
    }
    return false;
}

long hma_search_lru_page(hma_struct_t *hma, int64_t page_number)
{
    /*struct req_page *lrup;
    int64_t lru_pnum;
    lru_struct_t *ls = ls_bottom;
    while(ls != hma->lru_list_tail)
    {
        lru_pnum = ls->pnum;
        //HASH_FIND(r_hh, ptable, &lru_pnum, sizeof(int64_t), lrup);
        if(lru_pnum == page_number)
            return ls->access_clk;          

        ls = ls->next;
    }  
    return -1;      //if nothing found */

    struct req_page *lrup;
    
    HASH_FIND(r_hh, ptable, &page_number, sizeof(int64_t), lrup);

    if(lrup->dom == 0)
        return lrup->lru_p->access_clk;
    else
        return -1;
}

double hma_calculate_bwtime(void)
{
    double hma_bwtime;
    hma_bwtime = 0;
   
    if(hma.config.num_mem == 3)
    {
        hma_bwtime += (double)(npt_pcm_ddr*hma.config.page_size*1.0)/
                    ((double)hma.config.mem_list[2].bwidth*GB*1.0);
        hma_bwtime += (double)(npt_pcm_3ddram*hma.config.page_size*1.0)/
                    ((double)hma.config.mem_list[2].bwidth*GB*1.0);
        hma_bwtime += (double)(npt_3ddram_pcm*hma.config.page_size*1.0)/
                    ((double)hma.config.mem_list[2].bwidth*GB*0.25); // Assuming PCM write Bwidth = 1/4 PCM read Bwidth
        hma_bwtime += (double)(npt_ddr_pcm*hma.config.page_size*1.0)/
                    ((double)hma.config.mem_list[2].bwidth*GB*0.25); // Assuming PCM write Bwidth = 1/4 PCM read Bwidth
    }
    hma_bwtime += (double)(npt_ddr_3ddram*hma.config.page_size*1.0)/
                    ((double)hma.config.mem_list[1].bwidth*GB*1.0);
    hma_bwtime += (double)(npt_3ddram_ddr*hma.config.page_size*1.0)/
                    ((double)hma.config.mem_list[1].bwidth*GB*1.0);

    return hma_bwtime;
}

double hma_calculate_lattime(void)
{
    double hma_lattime = 0;
    if(npt_3ddram_ddr || npt_ddr_3ddram)
        hma_lattime += hma.config.mem_list[0].lat + hma.config.mem_list[1].lat;
    if(hma.config.num_mem == 3)
    {
        if(npt_pcm_3ddram || npt_3ddram_pcm)
            hma_lattime += hma.config.mem_list[0].lat + hma.config.mem_list[2].lat;
        if(npt_pcm_ddr || npt_ddr_pcm)
            hma_lattime += hma.config.mem_list[2].lat + hma.config.mem_list[1].lat;
    }
    return hma_lattime;
}

//TODO POLICY FOR PCM
///* RAM_HMA: modifying
int64_t hma_default_policy(hma_struct_t *hma, long clock)
{
    // hot pages are known 
    // top hot pages are known 
    // lru list is up to date 
    // all statistics are up to date 
    // simply transfer the hot pages to mem0 and use lru to replace 
    struct req_page *p;
    int i;
    int64_t pages_migrated_hot = 0;
    int64_t pages_migrated_cold = 0;
    int64_t time = 0, cache_oh_per_epoch = 0, mem_oh_per_epoch = 0;

    // iterate only through top N pages, so you don't exceed mem0 capacity 
    // mempod: first we want to see the access behavior, so lets not transfer anything, commenting out "for" loop
    for(i = 0; ((i < hma->stats.num_thp) && (pages_migrated_hot < hma->config.page_transfer_limit)); i++)
    {
        p = hma->stats.top_hot_pages[i];
        
        if(p->dom != 0) // which means l2m
        {
            // put flushing here 

            // move it to mem0, replace with lru if neccessary 
            if(hma->config.mem_list[0].free_frames > 0)     // there are free frames in l1m
            {
                //RAM_HMA: we do not model cache overhead
                //cache_oh_per_epoch += hma_flush_caches(p);

                // return the old frame to the free list of page's current domain 
                hma->config.mem_list[p->dom].free_frame_q.ff.push_back(p->frame_addr);
                hma->config.mem_list[p->dom].free_frames++;
                hma_inc_npt(p->dom, 0);

                // take a free frame from the head of the l1m's free list
                // and put it as the current frame_addr for the page
                p->frame_addr = hma->config.mem_list[0].free_frame_q.ff.front();
                hma->config.mem_list[0].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
                hma->config.mem_list[0].free_frames--;                  // take care of the count
                // now change the domain
                p->dom = 0;
                pages_migrated_hot += 1;
            }
            else    // we need to swap between l1m and l2m
            {
                struct req_page *lrup;
                // get lru page from l1m which is not in top N hot pages 
                lrup = hma_get_lru_page(hma);

                int64_t l1m_frame, l2m_frame;
                l1m_frame = lrup->frame_addr;
                l2m_frame = p->frame_addr;

                // no frame is being freed from any memory, since we are swapping here
                lrup->dom = p->dom;             // l1m lrup page moved to l2m
                lrup->frame_addr = l2m_frame;
                hma_inc_npt(0, p->dom);

                p->dom = 0;                     // l2m page moved to l1m's lrup page's frame
                p->frame_addr = l1m_frame;
                hma_inc_npt(p->dom, 0);

                //RAM_HMA: we do not model cache overhead
                //cache_oh_per_epoch += hma_flush_caches(p);
                //cache_oh_per_epoch += hma_flush_caches(lrup);
                pages_migrated_hot += 1;
                pages_migrated_cold += 1;
            }
            // don't forget to add it to lru list 
//            printf("ADDED LRU: %lld\n",p->pnum);
            hma_add_page_lru(hma, p, clock);
        }
    }


    // RAM_HMA: need to re-do this part
    /*
    // latency in cycles is added to the cycles used by bwtime; bwidth is original in GB/s so we need to convert 
    // guard: time set to 0 if no pages were transfered; otherwise it will hold latency 
    if (pages_migrated != 0){
        // get time in seconds spent on transfer 
        double bwtime = hma_calculate_bwtime();
        double lattime = hma_calculate_lattime();
//        double bwtime = (double)(pages_migrated*4096.0)/((double)hma->config.mem_list[1].bwidth*GB*1.0);
        // convert to CYCLES BECAUSE OF MOOLA
        bwtime *= 1000000000*cpu_freq_ghz;
        mem_oh_per_epoch = (timing_t)(lattime + bwtime);
        time = (timing_t)(mem_oh_per_epoch + cache_oh_per_epoch);
    }
    */
    
    printf("Overhead cycles total: %ld, oh cache: %ld, oh memtransfer: %ld, hot page: %ld, top hot page: %ld, hot_pages_transfered: %ld, cold_pages_transfered: %ld\n",time, cache_oh_per_epoch, mem_oh_per_epoch, hma->stats.num_hp, hma->stats.num_thp, pages_migrated_hot, pages_migrated_cold);
    // also increment statistics 
    hma->stats.total_pages_transfered += (pages_migrated_hot + pages_migrated_cold);
    return time;    // currently time = 0
}
//*/


void hma_print_top_hot_pages(hma_struct_t *hma)
{
    /* print top hot pages */
    int i;
    struct req_page *p;
    printf("\nTOP N HOT PAGES: %ld\n", hma->stats.num_thp);
    printf("----------------------------------------\n");
    printf("\tpage\t\tepoch count\t\ttotal count\t\tdomain\t\tpinned?\n");
    for(i=0; i<hma->stats.num_thp; i++)
    {
        p=hma->stats.top_hot_pages[i];
        printf("%16ld %16ld %20ld %20d %10d\n", p->pnum, p->hma_stat.epoch_count, p->hma_stat.total_count, p->dom, p->hma_stat.is_l3_locked);
    }
}

void hma_print_hot_pages(hma_struct_t *hma)
{
    /* print hot pages */
    int i;
    struct req_page *p;
    printf("\nHOT PAGES: %ld\n", hma->stats.num_hp);
    printf("----------------------------------------\n");
    printf("Epoch\tpage\t\tepoch count\t\ttotal count\t\tdomain\t\tpinned?\t\tcons_lock\t\tcons_hot\n");
    for(i = 0; i<hma->stats.num_hp; i++)
    {
        p = hma->stats.hot_pages[i];
        printf("%ld %16ld %16ld %20ld %20d %10d %5d %5d\n", hma->config.epoch, p->pnum, p->hma_stat.epoch_count, p->hma_stat.total_count, p->dom, p->hma_stat.is_l3_locked, p->hma_stat.cons_locked, p->hma_stat.cons_hot);
    }
    /*
    printf("Epoch\tpage\t\tepoch count\t\ttotal count\t\tdomain\t\tpinned?\n");
    for(i=0; i<hma->stats.num_hp; i++)
    {
        p=hma->stats.hot_pages[i];
        printf("%8lld %16lld %16lld %20lld %20d %10d %5d %5d\n",epoch, p->pnum, p->epoch_count, p->total_count, p->dom, p->is_l3_locked, p->cons_locked, p->cons_hot);
    }
    */
}

void print_all_lru_list_pages(hma_struct_t *hma)
{
    if(hma->lru_list != NULL)
    {
        lru_struct_t *l1m = NULL; 
        int64_t pnum;
        struct req_page * r;
        printf("\nLRU_LIST PAGES:");
        printf("---------------------\n");
        printf("Epoch\t\tpage\t\tepoch count\t\ttotal count\t\tdomain\n");
        for(l1m = hma->lru_list; l1m != NULL; l1m = l1m->next)
        {
            pnum = l1m->pnum;
            HASH_FIND(r_hh, ptable, &pnum, sizeof(int64_t), r);
            if(r == NULL)
            {
                printf("There is some problem in the LRU list..... exiting\n");
                exit(0);
            }
            else
                printf("%8ld %16ld %16ld %20ld %5d\n",hma->config.epoch, r->pnum, r->hma_stat.epoch_count, r->hma_stat.total_count, r->dom);
        }
    }
}

/* sort comparison functions for HASH_SRT */
int ht_pnum_sort(struct req_page *a, struct req_page *b)
{
    /* ASCENDING for page numbers */
    if (a->pnum < b->pnum)
        return -1;
    else if (a->pnum > b->pnum)
        return 1;
    else
        return 0;
}

int ht_tcount_sort(struct req_page *a, struct req_page *b)
{
    /* DESCENDING for total access count */
    if (a->hma_stat.total_count < b->hma_stat.total_count)
        return 1;
    else if (a->hma_stat.total_count > b->hma_stat.total_count)
        return -1;
    else
        return 0;
}


int ht_ecount_sort(struct req_page *a, struct req_page *b)
{
    /* DESCENDING for epoch access count */
    if (a->hma_stat.epoch_count < b->hma_stat.epoch_count)
        return 1;
    else if (a->hma_stat.epoch_count > b->hma_stat.epoch_count)
        return -1;
    else
        return 0;
}

/* convert a string to %ull */
unsigned long long int toint(char a[])
{
    int c, sign = 0, offset;
    long long int n;

    if (a[0] == '-')
    {
        sign = -1;
    }
    if (sign == -1)
    {
        offset = 1;
    }
    else
    {
        offset = 0;
    }

    n = 0;

    for (c = offset; a[c] != '\0'; c++)
    {
        if(isdigit(a[c]))
        {
            n = n * 10 + a[c] - '0';
        }
        else if(a[c] != '\n')
        {
            fprintf(stderr,"Error: Conversion from char to int failed. %c not a valid digit.\n",a[c]);
            exit(EXIT_FAILURE);
        }
    }

    if (sign == -1) {
        n = -n;
    }
    return n;
}

void print_usage(void)
{
    fprintf(stderr, "Usage: hmasim -i TRACE_FILE\n");
}

void hma_set_hot_pages(hma_struct_t *hma)
{
    HASH_SRT(r_hh, ptable, ht_ecount_sort);

    if(hma->stats.num_hp <= hma->config.mem_list[0].nframes)
    {
        hma->stats.num_thp = hma->stats.num_hp;
    }
    else
    {
        hma->stats.num_thp = hma->config.mem_list[0].nframes;
    }


    hma->stats.top_hot_pages = new req_page* [hma->stats.num_thp];  // *remember* both top_hot and hot page list will contain only pointers
    hma->stats.hot_pages = new req_page* [hma->stats.num_hp];       // to actual pages in hma, so no need to free the pages

    int i = 0;
    for(i = 0; i < hma->stats.num_thp; i++)
        hma->stats.top_hot_pages[i] = NULL; //  actual initiation will be done when we go through the page table 
    for(i = 0; i < hma->stats.num_hp; i++)
        hma->stats.hot_pages[i] = NULL;

    struct req_page *p;
    int count = 0;

    //printf("\nALL PAGES:\n");
    //printf("Epoch\tpage\tdomain\tepoch_count\ttotal_count\tacc_time_0\tacc_time_1\n");

    for(p = ptable; p != NULL; p = (struct req_page*)(p->r_hh.next) )
    {
        if(p->hma_stat.is_hot)
        {
            p->hma_stat.cons_hot += 1;
            if(hma->config.epoch < MAX_EPOCH_HIST)
                p->hma_stat.hot_epoch[hma->config.epoch] = 1;
            // if it is hot add to hp list
            hma->stats.hot_pages[count] = p;
            // add only N hot pages to thp list 
            if(count < hma->stats.num_thp){
                hma->stats.top_hot_pages[count] = p;
                p->hma_stat.is_top_hot = 1;
                // bump it on the LRU stack
                // dummy clock for reverse migration shashank
                long clock = -1;    //this function is not called in onfly 
                if(p->dom == 0)
                    hma_add_page_lru(hma, p, clock);                
            }
            count++;
        }
        else
        {
            p->hma_stat.cons_hot = 0;
        }
        //if(p->hma_stat.epoch_count > 10)
        //printf("%ld\t%ld\t%d\t%ld\t%ld\t%d\t%d\n",hma->config.epoch, p->pnum, p->dom, p->hma_stat.epoch_count, p->hma_stat.total_count, p->hma_stat.touch_time[0], p->hma_stat.touch_time[1]);
    }
    // RAM_HMA: deleted locking part
    
/*
    hma_print_top_hot_pages(hma);
    hma_print_hot_pages(hma);
    print_all_lru_list_pages(hma);
*/

}
//*/
//* RAM_HMA: Modified one
//only called once per new page allocation
struct req_page * hma_handler(hma_struct_t *hma, int64_t orig_req_addr, int core_id)
{
    //timing_t access_time = 0;
    struct req_page *p;

    int64_t pagenum = orig_req_addr/hma->config.page_size;
    HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), p);

    //printf("Pg: %ld, orig_add: %ld\n",pagenum, orig_req_addr);

    int64_t mem_type = CHECK_TARGET_MEM(orig_req_addr);
        
    // if no mapping exists, means minor pf 
    if (p == NULL)
    {
        p = new req_page;
        p->pnum = pagenum;
        
        //p->pnum_new = pagenum;
        //p->is_hot = 0;
        p->th_id = core_id;
        p->req_time = 333;
        p->inst_comp_count = 333;
        p->onfly_pair_pnum = -1;

        p->hma_stat.total_count = 0;
        p->hma_stat.epoch_count = 0;
        p->hma_stat.mig_times_count = 0;
        p->hma_stat.is_hot = 0;
        p->hma_stat.mig_count = 0;
        p->hma_stat.migration_ping = 0;
        p->hma_stat.migrated_location = 0;
        p->hma_stat.already_migrated=0;
        p->hma_stat.is_top_hot = 0;
        p->hma_stat.cons_hot = 0;
        p->hma_stat.cons_locked = 0;
        p->hma_stat.is_l3_locked = 0;
        p->lru_p = NULL;
        
        // mempod:
        p->hma_stat.curr_index = 0;
        //p->hma_stat.touch_time[0] = acc_cycle % hma->config.t_epoch;

        // Mz
        int e;
        for(e = 0; e < MAX_EPOCH_HIST; e++){
            p->hma_stat.hot_epoch[e] = 0;
        }

        //printf("Pg-: %ld, orig_add: %ld, dom: %ld, frames: %ld, %ld\n",pagenum, orig_req_addr, mem_type, hma->config.mem_list[0].free_frames, hma->config.mem_list[1].free_frames);

        /*RAM_HMA: new one, we will assign the domain depending on: 
            -the original request address
            -free frame availability
            -also find the actual frame no. of target memory and return it*/
        if (mem_type == 0) // as per orig_req_addr it should go to l1m, so check if there are any frre frame to alloc in l1m 
        {
            if(hma->config.mem_list[0].free_frames > 0) // memlist[0] is l1m and it has free frame
            {
                // take a free frame from the head of the l1m's free list
                // and put it as the current frame_addr for the page
                p->frame_addr = hma->config.mem_list[0].free_frame_q.ff.front();
                hma->config.mem_list[0].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
                hma->config.mem_list[0].free_frames--;                  // take care of the count
                p->dom = 0;
                //printf("l1m assignmnet: pg_num %ld, frame_addr %ld, free frames left %ld \n", p->pnum, p->frame_addr, hma->config.mem_list[0].free_frames);
            }
            else if(hma->config.mem_list[1].free_frames > 0)    // try to allocate from l2m
            {
                p->frame_addr = hma->config.mem_list[1].free_frame_q.ff.front();
                hma->config.mem_list[1].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
                hma->config.mem_list[1].free_frames--;                  // take care of the count
                p->dom = 1;
            }
            else if(hma->config.num_mem == 3)   // no free frame in l1m or l2m, so try allocate from l3m
            {
                if(hma->config.mem_list[2].free_frames > 0)    // try to allocate from l3m
                {
                    p->frame_addr = hma->config.mem_list[2].free_frame_q.ff.front();
                    hma->config.mem_list[2].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
                    hma->config.mem_list[2].free_frames--;                  // take care of the count
                    p->dom = 2;
                }
                else
                    printf("OUT OF MEMORY\n");
            }
            else
                printf("OUT OF MEMORY\n");

        }
        else if (mem_type == 1) // as per orig_req_addr it should go to l2m, so check if there are any frre frame to alloc in l2m 
        {
            if(hma->config.mem_list[1].free_frames > 0)    // need to allocate from l2m
            {
                p->frame_addr = hma->config.mem_list[1].free_frame_q.ff.front();
                hma->config.mem_list[1].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
                hma->config.mem_list[1].free_frames--;                  // take care of the count
                p->dom = 1;
                //printf("l2m assignmnet: pg_num %ld, frame_addr %ld, free frames left %ld \n", p->pnum, p->frame_addr, hma->config.mem_list[0].free_frames);
            }
            else if(hma->config.num_mem == 3)   // try allocate from l3m
            {
                if(hma->config.mem_list[2].free_frames > 0)    // no free frame in l1m, so need to allocate from l2m
                {
                    p->frame_addr = hma->config.mem_list[2].free_frame_q.ff.front();
                    hma->config.mem_list[2].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
                    hma->config.mem_list[2].free_frames--;                  // take care of the count
                    p->dom = 2;
                }
            }
            else if(hma->config.mem_list[0].free_frames > 0) // finally try to allocate from l1m
            {
                // take a free frame from the head of the l1m's free list
                // and put it as the current frame_addr for the page
                p->frame_addr = hma->config.mem_list[0].free_frame_q.ff.front();
                hma->config.mem_list[0].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
                hma->config.mem_list[0].free_frames--;                  // take care of the count
                p->dom = 0;
            }
            else
                printf("OUT OF MEMORY\n");
        }
        else if (mem_type == 3) // as per orig_req_addr it should go to l3m, so check if there are any frre frame to alloc in l3m 
        {
            //TODO:;
            // need to code later
        }

        HASH_ADD(r_hh, ptable, pnum, sizeof(int64_t), p);
    }// new page allocation done

    return p;
}
/*
bool onfly_fill_mig_stage_q(hma_struct_t *hma)
{
    int64_t req_pnum, req_pagenum, cur_pref_req_blk_addr;
    int64_t hot_pg_frame_addr, cold_pg_frame_addr;
    req_page* r = NULL;
    int j, p_hit = 0;

    //// generate request for new hot pg in mig stage q, make 32 reqs
    if( hot_mig_stage_q.size() == 0 )  
    {
        hot_pg_frame_addr = hma->onfly.hot_frame_addr;  //this is actual byte-level starting addr of the frame, so no need ot add any bits

        for(j = 0; j < 32; j++)
        {
            hot_mig_stage_q.ms_q.push_back(hot_pg_frame_addr);
            hot_pg_frame_addr += 64;

        }
    }
    else
    {
        //check why the hot_q is not cleared from last itme
        #ifdef MZ_ONFLY_DEBUG
            printf("Hot Mig stage q is not empty!!\n");
        #endif
    }

    if ( hma->onfly.mig_type == 2 ) // only for two-way migration we need to move a cold page
    {
    //// generate request for new cold pg in mig stage q, make 32 reqs

        if( cold_mig_stage_q.size() == 0 )  
        {
            cold_pg_frame_addr = hma->onfly.cold_frame_addr;  //this is actual byte-level starting addr of the frame, so no need ot add any bits

            for(j = 0; j < 32; j++)
            {
                cold_mig_stage_q.ms_q.push_back(cold_pg_frame_addr);
                cold_pg_frame_addr += 64;

            }
        }
        else
        {
            //check why the hot_q is not cleared from last itme
            #ifdef MZ_ONFLY_DEBUG
                printf("Cold Mig stage q is not empty!!\n");
            #endif
        }
    }

    return true;
}
*/

int hma_onfly_opt_hotcold_check_and_init(hma_struct_t *hma, long clock)
{

	// First check if the remap table is already full or not
	if(hma->onfly.remap_table_size > (REMAP_TABLE_MAX_SIZE - 2)) // 1022 is ok
	{
		return 0;	// no delay overhead
	}

    // NEW Part for new hot page search logic
    if (hma->onfly.next_hot_pg_num == -1)
        return 0; // no hot page found for migration

    int64_t hot_pnum = hma->onfly.next_hot_pg_num;
    struct req_page *p;
    
    HASH_FIND(r_hh, ptable, &hot_pnum, sizeof(int64_t), p);

    if(p == NULL) {
        cout << "Problem in hot page init.. exiting.\n";
        exit(0);
    }

    // Then check if the page is under addr recon
    if( (p->pnum == hma->onfly.ar_page_1) || (p->pnum == hma->onfly.ar_page_2) )
	{
        return 0;   // no delay overhead
	}

	// Check if it is already in the remap table
	if (p->hma_stat.already_migrated) {
		hma->stats.same_page_selected++;
		return 201;	//in this case there will be overhead for checking remap table for hot page bu no migration will start
	}
	
    //1 + 2:
    if(p->dom == 1) //current page is in l2m
    {
        //if(false)
        if(hma->config.mem_list[0].free_frames > 0)     // there are free frames in l1m
        {
            // indicate start of a probable migration
            // put a flag saying this is one way migration (since there is free frame in l1m)
            // so cold_pg_num does not play any role
            hma->onfly.mig_type = 1;    //mig_type 0 means no migration is happening, 1 means one-way, 2 means two-way

            // put the current hot page's info (which is going to be migrated) in the global hma struct
            hma->onfly.hot_pg_num = p->pnum;    // ***remember, page no. does not incluse last 11 bits (2KBpage)
            hma->onfly.hot_frame_addr = p->frame_addr;  // ***but frame addr is the full byte level addr
            hma->onfly.hot_pg_core_id = p->th_id;    // taking core_id from page information

            // put the current free frame's info (into where the hot page is going to be migrated) in the global hma struct
            // currently it is a free frame in l1m
            hma->onfly.cold_pg_num = 0;
            hma->onfly.cold_frame_addr = hma->config.mem_list[0].free_frame_q.ff.front();
            hma->onfly.cold_pg_core_id = -1;    // dummy number
            hma->config.mem_list[0].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
            hma->config.mem_list[0].free_frames--;                  // take care of the count

            // update the hot page's onfly pair info, after checking/updating the previous pair info
            p->onfly_pair_pnum = -1;    // it is one way migration, so no pair
			p->hma_stat.already_migrated = 1;	// since this page is already selected
        }
        else    // we need to swap between l1m and l2m
        {
            // get lru page from l1m which is not in top N hot pages 
            struct req_page *lrup, *lrup_2;
            lrup = hma_get_lru_page(hma);

            if(lrup == NULL) {
                cout << "no lru cold page found in onfly_hotcold" << endl;
                return 202;	 //in this case there will be overhead for checking remap table for hot+cold page bu no migration will start
            }

            // need to check if lrup is currently under addr recon process
            if( ((lrup->pnum == hma->onfly.ar_page_1) || (lrup->pnum == hma->onfly.ar_page_2)) || (lrup->hma_stat.already_migrated) ) {
				hma->stats.same_page_selected++;
                hma_add_page_lru(hma, lrup, clock);
                lrup_2 = hma_get_lru_page(hma);

                if(lrup_2 == NULL) {
                    cout << "no lru cold page found in onfly_hotcold" << endl;
                    return 202;	//in this case there will be overhead for checking remap table for hot+cold page bu no migration will start
                }

                if(( (lrup_2->pnum == hma->onfly.ar_page_1) || (lrup_2->pnum == hma->onfly.ar_page_2)) ||(lrup_2->hma_stat.already_migrated) ) {
					hma->stats.same_page_selected++;
                    hma_add_page_lru(hma, lrup_2, clock);
                    return 202;	//in this case there will be overhead for checking remap table for hot+cold page bu no migration will start
                }
				lrup = lrup_2;	// since lrup_2 is the correct lru page
            }

            // indicate start of a probable migration
            // put a flag saying this is two way migration (since there is no free frame in l1m)
            // so we need a cold page to migrate from l1m to l2m to make room for hot page
            hma->onfly.mig_type = 2;

            // put the current hot page's info (which is going to be migrated) in the global hma struct
            hma->onfly.hot_pg_num = p->pnum;
            hma->onfly.hot_frame_addr = p->frame_addr;
            hma->onfly.hot_pg_core_id = p->th_id;    // taking core_id from page information

            // *** here we need to be sure if it is cold enough, if not may need to device other algo ***
            // put the current cold page's info (which is being swapped with the hot page) in the global hma struct
            hma->onfly.cold_pg_num = lrup->pnum;
            hma->onfly.cold_frame_addr = lrup->frame_addr;
            hma->onfly.cold_pg_core_id = lrup->th_id;    // taking core_id from page information

            // check and update onfly pair info for both hot and cold page, we do not care who the oldpair was
            p->onfly_pair_pnum = lrup->pnum;    // it is two way migration, so the pair is lrup
            lrup->onfly_pair_pnum = p->pnum;    // it is two way migration, so the pair is lrup

			p->hma_stat.already_migrated = 1;	// since this page is already selected
			lrup->hma_stat.already_migrated = 1;	// since this page is already selected
			//printf("two_way_hotcold_init_and_check: mig_type %d, hot_pg %ld, hot_frame %ld, hot_dom %d, cold_pg %ld, cold_frame %ld, cold_dom %d\n", hma->onfly.mig_type, p->pnum , p->frame_addr, p->dom, lrup->pnum, lrup->frame_addr, lrup->dom );

        }   //two way migration
    }
    else
    {
        //something is wrong;
        cout << "page domain wrong in onfly_hotcold" << endl;
        return 0;
    }

    //printf("ONFLY_INIT: mig_type %d, hot_pg %ld, hot_frame %ld, cold_pg %ld, cold_frame %ld \n", hma->onfly.mig_type,  hma->onfly.hot_pg_num , hma->onfly.hot_frame_addr,  hma->onfly.cold_pg_num, hma->onfly.cold_frame_addr );   
    //3. Will be done at processor side, since now we will fill migration staging q with hot and cold pg RD reqs.

    // Reset the next hot page info
    hma->onfly.highest_l2m_hotness = hma->config.thold; //resetting l2m highest hotness with provided threshold;
    hma->onfly.next_hot_pg_num = -1;
    //printf("will migrate with count %ld, with pnum %ld\n", p->hma_stat.mig_count, p->pnum);
    p->hma_stat.mig_count = 0; // resetting is ok?
    return 203;	//start normal migration with necessary Remap table and OS check delay
}

int hma_onfly_hotcold_check_and_init(hma_struct_t *hma, req_page *p, long clock)
{

	// First check if the remap table is already full or not
	if(hma->onfly.remap_table_size > (REMAP_TABLE_MAX_SIZE - 2)) // 1022 is ok
	{
		return 0;	// no delay overhead
	}

    // Then check if the page is under addr recon
    if( (p->pnum == hma->onfly.ar_page_1) || (p->pnum == hma->onfly.ar_page_2) )
	{
        return 0;   // no delay overhead
	}

	// Check if it is already in the remap table
	if (p->hma_stat.already_migrated) {
		hma->stats.same_page_selected++;
		return 201;	//in this case there will be overhead for checking remap table for hot page bu no migration will start
	}
	
    //1 + 2:
    if(p->dom == 1) //current page is in l2m
    {
        if(hma->config.mem_list[0].free_frames > 0)     // there are free frames in l1m
        {
            // indicate start of a probable migration
            // put a flag saying this is one way migration (since there is free frame in l1m)
            // so cold_pg_num does not play any role
            hma->onfly.mig_type = 1;    //mig_type 0 means no migration is happening, 1 means one-way, 2 means two-way

            // put the current hot page's info (which is going to be migrated) in the global hma struct
            hma->onfly.hot_pg_num = p->pnum;    // ***remember, page no. does not incluse last 11 bits (2KBpage)
            hma->onfly.hot_frame_addr = p->frame_addr;  // ***but frame addr is the full byte level addr
            hma->onfly.hot_pg_core_id = p->th_id;    // taking core_id from page information

            // put the current free frame's info (into where the hot page is going to be migrated) in the global hma struct
            // currently it is a free frame in l1m
            hma->onfly.cold_pg_num = 0;
            hma->onfly.cold_frame_addr = hma->config.mem_list[0].free_frame_q.ff.front();
            hma->onfly.cold_pg_core_id = -1;    // dummy number
            hma->config.mem_list[0].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
            hma->config.mem_list[0].free_frames--;                  // take care of the count

            // update the hot page's onfly pair info, after checking/updating the previous pair info
            p->onfly_pair_pnum = -1;    // it is one way migration, so no pair
			p->hma_stat.already_migrated = 1;	// since this page is already selected
            cout << "one way:: hot page: " << p->pnum << ", pair is: " << p->onfly_pair_pnum << "\n";
        }
        else    // we need to swap between l1m and l2m
        {
            // get lru page from l1m which is not in top N hot pages 
            struct req_page *lrup, *lrup_2;
            lrup = hma_get_lru_page(hma);

            if(lrup == NULL) {
                cout << "no lru cold page found in onfly_hotcold" << endl;
                return 202;	 //in this case there will be overhead for checking remap table for hot+cold page bu no migration will start
            }

            // need to check if lrup is currently under addr recon process
            if( ((lrup->pnum == hma->onfly.ar_page_1) || (lrup->pnum == hma->onfly.ar_page_2)) || (lrup->hma_stat.already_migrated) ) {
				hma->stats.same_page_selected++;
                hma_add_page_lru(hma, lrup, clock);
                lrup_2 = hma_get_lru_page(hma);

                if(lrup_2 == NULL) {
                    cout << "no lru cold page found in onfly_hotcold" << endl;
                    return 202;	//in this case there will be overhead for checking remap table for hot+cold page bu no migration will start
                }

                if(( (lrup_2->pnum == hma->onfly.ar_page_1) || (lrup_2->pnum == hma->onfly.ar_page_2)) ||(lrup_2->hma_stat.already_migrated) ) {
					hma->stats.same_page_selected++;
                    hma_add_page_lru(hma, lrup_2, clock);
                    return 202;	//in this case there will be overhead for checking remap table for hot+cold page bu no migration will start
                }
				lrup = lrup_2;	// since lrup_2 is the correct lru page
            }

            // indicate start of a probable migration
            // put a flag saying this is two way migration (since there is no free frame in l1m)
            // so we need a cold page to migrate from l1m to l2m to make room for hot page
            hma->onfly.mig_type = 2;

            // put the current hot page's info (which is going to be migrated) in the global hma struct
            hma->onfly.hot_pg_num = p->pnum;
            hma->onfly.hot_frame_addr = p->frame_addr;
            hma->onfly.hot_pg_core_id = p->th_id;    // taking core_id from page information

            // *** here we need to be sure if it is cold enough, if not may need to device other algo ***
            // put the current cold page's info (which is being swapped with the hot page) in the global hma struct
            hma->onfly.cold_pg_num = lrup->pnum;
            hma->onfly.cold_frame_addr = lrup->frame_addr;
            hma->onfly.cold_pg_core_id = lrup->th_id;    // taking core_id from page information

            // check and update onfly pair info for both hot and cold page, we do not care who the oldpair was
            p->onfly_pair_pnum = lrup->pnum;    // it is two way migration, so the pair is lrup
            lrup->onfly_pair_pnum = p->pnum;    // it is two way migration, so the pair is lrup

			p->hma_stat.already_migrated = 1;	// since this page is already selected
			lrup->hma_stat.already_migrated = 1;	// since this page is already selected
			printf("hotcold_init_and_check: mig_type %d, hot_pg %ld, hot_frame %ld, hot_dom %d, cold_pg %ld, cold_frame %ld, cold_dom %d\n", hma->onfly.mig_type, p->pnum , p->frame_addr, p->dom, lrup->pnum, lrup->frame_addr, lrup->dom );

        }   //two way migration
    }
    else
    {
        //something is wrong;
        cout << "page domain wrong in onfly_hotcold" << endl;
        return 0;
    }

    //printf("ONFLY_INIT: mig_type %d, hot_pg %ld, hot_frame %ld, cold_pg %ld, cold_frame %ld \n", hma->onfly.mig_type,  hma->onfly.hot_pg_num , hma->onfly.hot_frame_addr,  hma->onfly.cold_pg_num, hma->onfly.cold_frame_addr );   
    //3. Will be done at processor side, since now we will fill migration staging q with hot and cold pg RD reqs.
    return 203;	//start normal migration with necessary Remap table and OS check delay
}

bool hma_onfly_hotcold_init(hma_struct_t *hma, req_page *p, int64_t acc_cycle, long clock)
{
	// First check if the remap table is already full or not
	if(hma->onfly.remap_table_size > (REMAP_TABLE_MAX_SIZE - 2)) // 1022 is ok
		return false;

    // Then check if the page is under addr recon
    if( (p->pnum == hma->onfly.ar_page_1) || (p->pnum == hma->onfly.ar_page_2) )
        return false;
	
    //1 + 2:
    if(p->dom == 1) //current page is in l2m
    {
        if(hma->config.mem_list[0].free_frames > 0)     // there are free frames in l1m
        {
            // indicate start of a probable migration
            // put a flag saying this is one way migration (since there is free frame in l1m)
            // so cold_pg_num does not play any role
            hma->onfly.mig_type = 1;    //mig_type 0 means no migration is happening, 1 means one-way, 2 means two-way

            // put the current hot page's info (which is going to be migrated) in the global hma struct
            hma->onfly.hot_pg_num = p->pnum;    // ***remember, page no. does not incluse last 11 bits (2KBpage)
            hma->onfly.hot_frame_addr = p->frame_addr;  // ***but frame addr is the full byte level addr
            hma->onfly.hot_pg_core_id = p->th_id;    // taking core_id from page information

            // put the current free frame's info (into where the hot page is going to be migrated) in the global hma struct
            // currently it is a free frame in l1m
            hma->onfly.cold_pg_num = 0;
            hma->onfly.cold_frame_addr = hma->config.mem_list[0].free_frame_q.ff.front();
            hma->onfly.cold_pg_core_id = -1;    // dummy number
            hma->config.mem_list[0].free_frame_q.ff.pop_front();    // take off the head element, since it is used now
            hma->config.mem_list[0].free_frames--;                  // take care of the count

            // update the hot page's onfly pair info, after checking/updating the previous pair info
            if(p->onfly_pair_pnum != -1) {
                struct req_page *old_pair;
                int64_t pagenum = p->onfly_pair_pnum;
                HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), old_pair);

                if(old_pair == NULL) {
                    cout <<"Old pair page not found!\n";
                    exit(0);
                }
                if(old_pair->onfly_pair_pnum == p->pnum) {
                    old_pair->onfly_pair_pnum = -1;
                }
                p->onfly_pair_pnum = -1;    // it is one way migration, so no pair
            }
        }
        else    // we need to swap between l1m and l2m
        {
            // get lru page from l1m which is not in top N hot pages 
            struct req_page *lrup;
            lrup = hma_get_lru_page(hma);

            if(lrup == NULL) {
                cout << "no lru cold page found in onfly_hotcold" << endl;
                return false;
            }

            // need to check if lrup is currently under addr recon process
            if( (lrup->pnum == hma->onfly.ar_page_1) || (lrup->pnum == hma->onfly.ar_page_2) ) {
                hma_add_page_lru(hma, lrup, clock);
                struct req_page *lrup_2;
                lrup_2 = hma_get_lru_page(hma);

                if(lrup_2 == NULL) {
                    cout << "no lru cold page found in onfly_hotcold" << endl;
                    return false;
                }

                if( (lrup_2->pnum == hma->onfly.ar_page_1) || (lrup_2->pnum == hma->onfly.ar_page_2) ) {
                    hma_add_page_lru(hma, lrup_2, clock);
                    return false;
                }
            }

            // indicate start of a probable migration
            // put a flag saying this is two way migration (since there is no free frame in l1m)
            // so we need a cold page to migrate from l1m to l2m to make room for hot page
            hma->onfly.mig_type = 2;

            // put the current hot page's info (which is going to be migrated) in the global hma struct
            hma->onfly.hot_pg_num = p->pnum;
            hma->onfly.hot_frame_addr = p->frame_addr;
            hma->onfly.hot_pg_core_id = p->th_id;    // taking core_id from page information

            // *** here we need to be sure if it is cold enough, if not may need to device other algo ***
            // put the current cold page's info (which is being swapped with the hot page) in the global hma struct
            hma->onfly.cold_pg_num = lrup->pnum;
            hma->onfly.cold_frame_addr = lrup->frame_addr;
            hma->onfly.cold_pg_core_id = lrup->th_id;    // taking core_id from page information

            // check and update onfly pair info for both hot and cold page
            // for hot page
            if(p->onfly_pair_pnum != -1) {
                struct req_page *old_pair;
                int64_t pagenum = p->onfly_pair_pnum;
                HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), old_pair);

                if(old_pair == NULL) {
                    cout <<"Old pair page not found!\n";
                    exit(0);
                }
                if(old_pair->onfly_pair_pnum == p->pnum) {
                    old_pair->onfly_pair_pnum = -1;
                }
                p->onfly_pair_pnum = lrup->pnum;    // it is two way migration, so the pair is lrup
            }//if pair
            else p->onfly_pair_pnum = lrup->pnum;    // it is two way migration, so the pair is lrup

            // for cold page
            if(lrup->onfly_pair_pnum != -1) {
                struct req_page *old_pair;
                int64_t pagenum = lrup->onfly_pair_pnum;
                HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), old_pair);

                if(old_pair == NULL) {
                    cout <<"Old pair page not found!\n";
                    exit(0);
                }
                if(old_pair->onfly_pair_pnum == lrup->pnum) {
                    old_pair->onfly_pair_pnum = -1;
                }
                lrup->onfly_pair_pnum = p->pnum;    // it is two way migration, so the pair is lrup
            }//if pair
            else lrup->onfly_pair_pnum = p->pnum;    // it is two way migration, so the pair is lrup


        }   //two way migration
    }
    else
    {
        //something is wrong;
        cout << "page domain wrong in onfly_hotcold" << endl;
        return false;
    }

    //printf("ONFLY_INIT: mig_type %d, hot_pg %ld, hot_frame %ld, cold_pg %ld, cold_frame %ld \n", hma->onfly.mig_type,  hma->onfly.hot_pg_num , hma->onfly.hot_frame_addr,  hma->onfly.cold_pg_num, hma->onfly.cold_frame_addr );   
    //3. Will be done at processor side, since now we will fill migration staging q with hot and cold pg RD reqs.
    return true;
}


void hma_epoch_count_keeper(hma_struct_t *hma, req_page *p, int64_t acc_cycle)
{
    // RAM_HMA: we are sure the page is being accessed, so updat ethe counters 
    p->hma_stat.total_count++;
    p->hma_stat.epoch_count++;
    
    if((p->hma_stat.is_hot == 0) && (p->hma_stat.epoch_count >= hma->config.thold))
    {
        p->hma_stat.is_hot = 1;
        hma->stats.num_hp++;
    }

    if(p->dom == 0)
        hma_add_page_lru(hma, p, acc_cycle);

    if (p!=NULL)
    {
        hma->config.mem_list[p->dom].num_accesses++;
    }
    else
    {
        hma->stats.failed_accesses++;
    }

    return;
}

int hma_onfly_count_keeper(hma_struct_t *hma, req_page *p, int64_t acc_cycle, int req_target_mem)
{
    p->hma_stat.total_count++;
    //p->hma_stat.epoch_count++;			//don't need this in onfly**
    p->hma_stat.mig_count++;				//onfly**

    if(p->dom == 0 && req_target_mem == 0)
    {
		if(p->pnum != hma->onfly.cold_pg_num)	// since this page already been selected as a cold page which is going to be evicted from l1m
	        hma_add_page_lru(hma, p, acc_cycle);   // otherwise makes it most recently accessed page
    }

    if (p!=NULL)    {
        hma->config.mem_list[p->dom].num_accesses++;
    }
    else    {
        hma->stats.failed_accesses++;
    }

	//need to check the hot_page or cold_page threshold check here
    if((p->dom == 1) && (p->hma_stat.mig_count >= hma->config.thold))		//onfly**  //I guess this is where hot page is found out but not best way
    {
	    hma->stats.num_hp++;
            //printf("before crossing current count %ld, old highest count %ld, current pnum %ld, old pnum %ld\n", p->hma_stat.mig_count, hma->onfly.highest_l2m_hotness, p->pnum, hma->onfly.next_hot_pg_num);
        if( (p->hma_stat.mig_count > hma->onfly.highest_l2m_hotness) && ((p->pnum != hma->onfly.hot_pg_num) &&  (p->pnum != hma->onfly.cold_pg_num)) ) {
            //printf("after crossing current count %ld, old highest count %ld, current pnum %ld, old pnum %ld\n", p->hma_stat.mig_count, hma->onfly.highest_l2m_hotness, p->pnum, hma->onfly.next_hot_pg_num);
            hma->onfly.highest_l2m_hotness = p->hma_stat.mig_count;
            hma->onfly.next_hot_pg_num = p->pnum;
        }
        return 1;
    }
	/*//need to check the hot_page or cold_page threshold check here
    if((p->dom == 1) && (p->hma_stat.mig_count >= hma->config.thold))		//onfly**  //I guess this is where hot page is found out but not best way
    {
	    hma->stats.num_hp++;
        return 1;
    }*/
    
    return 0;
}

int64_t hma_policy_invoker(hma_struct_t *hma, long clock)
{
    int64_t policy_time;

    hma_set_hot_pages(hma);
/*
    //        printf("Hot pages set!\n");
    //        printf("Epoch: %d, nhp: %lld, nthp: %lld, etime: %lld, ttime: %lld\n",sim.epoch, hma.stats.num_hp, hma.stats.num_thp, sim.epoch_time, sim.total_time);
    //        hma_print_hot_pages(&hma);
    //       hma_print_top_hot_pages(&hma);
*/
    policy_time = hma_default_policy(hma, clock);
    //double ratio;
/*
//    ratio =(double) tlb_miss/(double)tlb_acc;
//    ratio = ratio*100.0;
//    printf("Epoch: %d, nhp: %lld, nthp: %lld, ecycles: %lld, tcycles: %lld\n",epoch, hma.stats.num_hp, hma.stats.num_thp, epoch_time, sim_time);
    printf("Epoch: %d, nhp: %lld, nthp: %lld, ecycles: %lld, tcycles: %lld, policy cycles for mem transfer part %lld\n",epoch, hma.stats.num_hp, hma.stats.num_thp, epoch_time, sim_time, policy_time);
    printf("HPinTLB %d\n",hp_in_tlb[epoch]);

    hma_print_npt();*/

    hma_reset_stats(hma);
    return 1;
}

