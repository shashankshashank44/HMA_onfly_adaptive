#ifndef __PROCESSOR_H
#define __PROCESSOR_H

#include "Cache.h"
#include "Config.h"
#include "Memory.h"
#include "Request.h"
#include "Statistics.h"
#include "pbuff.h"
#include "hma.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <ctype.h>
#include <functional>

namespace ramulator 
{

class Trace {
public:
    Trace(const char* trace_fname);
    // trace file format 1:
    // [# of bubbles(non-mem instructions)] [read address(dec or hex)] <optional: write address(evicted cacheline)>
    bool get_unfiltered_request(long& bubble_cnt, long& req_addr, Request::Type& req_type);
    bool get_filtered_request(long& bubble_cnt, long& req_addr, Request::Type& req_type);

    //Mz: adding new format for trace file: bubble_cnt, pc, addr
    bool get_request_new_format(long& bubble_cnt, long& req_addr, Request::Type& req_type);

	//Mz: adding new format to read pin trace file, to be able to use ramulator caches
    bool get_request_pin_format(long& bubble_cnt, long& req_addr, Request::Type& req_type, uint64_t& req_addr_temp, int coreid);
	//Mz: adding function to separte virtual address coming from each core and then 
	//allocate va to pa assuming a fixed physical memory size and basic round robin allocation 
	//between HBM and PCM.
	bool flat_allocate_va_to_pa(uint64_t req_orig_va, uint64_t& req_flat_allocated_pa, uint64_t req_coreid); 

    // trace file format 2:
    // [address(hex)] [R/W]
    bool get_dramtrace_request(long& req_addr, Request::Type& req_type);



private:
    std::ifstream file;
    std::string trace_name;
};


class Window {
public:
    int ipc = 4;
    int depth = 128;

    Window() : ready_list(depth), addr_list(depth, -1), delay_list(depth, 0), mig_list(depth, 0), mask(0)  {}   //mask will be overwritten
    Window(Buffer* hb, Buffer* cb) : ready_list(depth), addr_list(depth, -1), delay_list(depth, 0), mig_list(depth, 0), mask(0)    {
        hot_buffer = hb;
        cold_buffer = cb;
    }
    Buffer* hot_buffer = NULL;
    Buffer* cold_buffer = NULL;

    bool is_full();
    bool is_empty();
    void insert(bool ready, long addr);
    void set_ready(long addr);
    void set_ready_optimized(long addr, long delayed_time);  //number of bitwise ANDs minimized

    void mig_insert(bool ready, long addr, int delays, int mig);
    long retire(long core_clk);
    long mig_retire();
    void set_ready_with_delay(long addr, uint64_t cdt);
    void set_ready_for_mig_buffer(long addr, long cdt);
    void mig_set_ready(long addr, int target_memory_type );

//private:
    int load = 0;
    int head = 0;
    int tail = 0;
    std::vector<bool> ready_list;
    std::vector<long> addr_list;
    std::vector<long> delay_list;
    std::vector<long> mig_list;
    uint64_t mask;
    long current_mig_buffer_hits = 0;

};


class Core {
public:
    long clk = 0;
    long retired = 0;
    int id = 0;
    function<bool(Request)> send;

    Core(const Config& configs, int coreid,
        const char* trace_fname,
        function<bool(Request)> send_next, Cache* llc,
        std::shared_ptr<CacheSystem> cachesys, MemoryBase& memory);
	//Mz: For simple hybrid memory, two types of memories
    Core(const Config& configs, const Config& configs_l2, int coreid,
        const char* trace_fname,
        function<bool(Request)> send_next, Cache* llc,
        std::shared_ptr<CacheSystem> cachesys, MemoryBase& memory, MemoryBase& memory_l2);
    void tick();
    void receive(Request& req);
    double calc_ipc();
    bool finished();
    bool has_reached_limit();
    function<void(Request&)> callback;

    bool no_core_caches = true;
    bool no_shared_cache = true;
    bool is_hma_onfly =  false;

    // Mz for onfly
    void receive_for_mig_buffer(long r_addr, long temp_clk);

	// Mz: For zen:  (per core = 2 threads)  L1 D-Cache 32K, 8-way; L2 Cache 512K, 8-way
	// So we will have: L1 D-Cache 16K, 8-way; L2 Cache 256K, 8-way (per simulation core)

    /* Basic 
    int l1_size = 1 << 15;
    int l1_assoc = 1 << 3;
    */

    //new:
    int l1_size = 1 << 15;  //l1 32KB
    int l1_assoc = 1 << 2;  //4-way as most of the papers
    //Basic:
    int l1_blocksz = 1 << 6;
    int l1_mshr_num = 16;

	//old values are fine
    int l2_size = 1 << 18;
    int l2_assoc = 1 << 3;
    int l2_blocksz = 1 << 6;
    int l2_mshr_num = 16;
    std::vector<std::shared_ptr<Cache>> caches;
    Cache* llc;


    ScalarStat cpu_inst;
    ScalarStat record_cycs;
    ScalarStat record_insts;
    long expected_limit_insts;
    // This is set true iff expected number of instructions has been executed or all instructions are executed.
    bool reached_limit = false;;

	//Mz:
	uint64_t get_memory_count();
	uint64_t get_hybrid_memory_count();
    short ar_flag;
    short core_halt;
	int os_halt_cycle;
    long total_core_ar_conflict;
    long total_core_ar_conflict_cycles;
    ScalarStat ar_halt_cycles;

     //Mz 500M trace input
    long tot_bbl_cnt;
    int b_ind;

private:
    Trace trace;
    Window window;

    long bubble_cnt;
    long req_addr = -1;
	// Mz: for reading pin format traces
    uint64_t req_addr_temp = 0;
    Request::Type req_type;
    bool more_reqs;
    long last = 0;

    ScalarStat memory_access_cycles;
    ScalarStat memory_l2_access_cycles;
//    ScalarStat cpu_inst;
    MemoryBase& memory;
    MemoryBase& memory_l2;
};

//Mz: creating new class for migration controller

class Migration_controller {
public:
    long mig_clk = 0;
    long mig_start_clk = 0;
    long ar_start_clk = 0;
    long ar_next_free_clk = 0;
	long ar_next_half_way_wait_clk = 0;
    long migp_next_free_clk = 0;
    long total_mig_cycle = 0;
    long total_ar_cycle = 0;
    long total_ar_cycle_ar1 = 0;
    long total_ar_cycle_ar2 = 0;
    long total_no_of_ar_process = 0;
    long total_no_of_ar1 = 0;
    long total_no_of_ar2 = 0;
    long adaptive_mig_cycle = 0;
    long adaptive_mig_cycle_recheck = 0;
    long adaptive_mig_count = 0;

	int next_core_id = 0;
    long retired = 0;
    int id = 0;

    // size related
    pair<uint64_t, int> memory_page_info;
    int lines_in_page;

    // statistics
    int64_t mig_delay = 0;    // No mig delay in CPU cycle, need to check later if there should be any delay
    long mig_retired = 0;
    long mig_retired_current = 0;
    long total_mig_buffer_hits = 0;
    long mig_pair_count = 0;
    //function<bool(Request)> send_mig_bypass;
    int64_t hot_buffer_hits_current_mig = 0;
    int64_t cold_buffer_hits_current_mig = 0;
	//Mz: For simple hybrid memory, two types of memories
    Migration_controller(function<bool(Request)> send_memory, function<bool(Request)> send_memory_l2, function<bool(Request)> send_mig, function<bool(Request)> send_mig_l2, std::vector<std::shared_ptr<Core>> mig_cores, Buffer* hb, Buffer *cb, Cache* llc, MemoryBase& memory, MemoryBase& memory_l2);

    void hybrid_tick_hma_onfly();
    void mig_receive(Request& req);
    function<void(Request&)> mig_callback;

    Window* mig_window = NULL;
    Buffer* hot_buffer = NULL;
    Buffer* cold_buffer = NULL;    

    Request::Type req_type;
    Cache* llc;
    std::vector<std::shared_ptr<Core>> mig_cores;

    function<bool(Request)> send_memory;
    function<bool(Request)> send_memory_l2;
    function<bool(Request)> send_mig;
    function<bool(Request)> send_mig_l2;

    MemoryBase& memory;
    MemoryBase& memory_l2;

    // MigC components:
    struct Queue_mig_staging {
        list<int64_t> ms_pg_addr_q, pg_backup;
        list<int64_t> ms_fr_addr_q, fr_backup;
        unsigned int max = 32; //2KB page, 32 64 byte requests
        unsigned int pg_q_size() {return ms_pg_addr_q.size();}
        unsigned int fr_q_size() {return ms_fr_addr_q.size();}
    };

    Queue_mig_staging hot_mig_stage_q; 
    Queue_mig_staging cold_mig_stage_q; 
    
    vector<  pair< pair<int64_t, int64_t>, pair<bool, int64_t> >  > remap_table;					//old address, new address, currently onfly?
    //int64_t remap_table_max = 1024;

    // migration functions
    int reverse_migration();
    bool onfly_fill_mig_stage_q(hma_struct_t *hma);
    int onfly_mig_send_read();
    int onfly_mig_send_write();
    int onfly_mig_start();
    bool onfly_mig_demand_pending();
    void onfly_mig_completion();
    void onfly_mig_print();
    void onfly_mig_receive_clearing_demand(long req_addr, long temp_clk, int cid);

	// Page migration params
    int migp_bz = 0;
    int migp_wait = 0;
    int mig_stage_201_bz_cycle = REMAP_TABLE_DELAY;    
    int mig_stage_202_bz_cycle = 4 +  2*(REMAP_TABLE_DELAY); 

    int mig_stage_203_bz_cycle = 4 +  2*(REMAP_TABLE_DELAY);    // Need to search 2 times in remap table to select two entry. OS send/rec for two
    int mig_stage_203_wait_cycle = 150;    // OS query delay for hot and cold page in parallel running in two cores

    // Address Recon params and functions
    // addr recon delay, for two remap entries
    int ar_page_1_core_id = -1;
    int ar_page_2_core_id = -1;
    int ar_bz = 0;
    int ar_wait = 0;

    // 11 = 1 way, status 1; 21= 2 way, status 2 and so on.
    int ar_stage_11_bz_cycle = 2 + 2*(REMAP_TABLE_DELAY);    // Need to search 2 times in remap table to select one entry. OS send/rec for one
    int ar_stage_21_bz_cycle = 4 +  2*(REMAP_TABLE_DELAY);    // Need to search 2 times in remap table to select two entry. OS send/rec for two
    int ar_stage_12_bz_cycle = 2;    // DiDi send and recv for one ar 
    int ar_stage_22_bz_cycle = 4;    // DiDi  
    int ar_stage_13_bz_cycle = 64;    // cache flush send and recv communication for one ar (32 lines) 
    int ar_stage_23_bz_cycle = 128;    //  
    int ar_stage_14_bz_cycle = 2;    // OS send and recv. for one ar  
    int ar_stage_24_bz_cycle = 4;    //  
    
    int ar_stage_11_wait_cycle = 4480;    // OS page RMAP delay, ftrace tool given delay for page_reference_one function is 1.4 micro sec
    int ar_stage_21_wait_cycle = 8960;    
    int ar_stage_12_wait_cycle = 300;    // DiDi delay 80/0.3125 + 2 + 21 = 279 
    int ar_stage_22_wait_cycle = 300;    // DiDi delay 80/0.3125 + 2 + 21 = 279 
    int ar_stage_13_wait_cycle = 23;    // 21+2, as pipelined atmost 23 from last inv [old: 736; cache flush delay 32 * 1 * (2+21) = 736] 
    int ar_stage_23_wait_cycle = 23;    //  21+2,  as pipelined [old: 1472; cache flush delay 32 * 2 * (2+21) = 1472 
    int ar_stage_14_wait_cycle = 150;    // OS update delay 1*150 
    int ar_stage_24_wait_cycle = 300;    // OS update delay 2*150 = 300 
    bool onfly_started = false;
    int64_t migrated_hbm_access = 0;    //whole benchmark
    int64_t migrated_hbm_access_temp = 0;   //just for interval
    int64_t remap_access = 0;
    bool pause_migration = false;
    vector<int> access_migration_ratio;
    vector<int> remap_access_ratio;
    vector<int> threshold_history;
    vector<int> window_mig_count;
    int onfly_ar_init();    
	void onfly_page_mig_initial_overhead(int status);
    /*
    ScalarStat migc_inst;
    ScalarStat migc_record_cycs;
    ScalarStat migc_record_insts;
    */
};
//end of migc

class Processor {
public:
    Processor(const Config& configs, vector<const char*> trace_list,
        function<bool(Request)> send, MemoryBase& memory);
    Processor(const Config& configs, const Config& configs_l2, vector<const char*> trace_list, function<bool(Request)> send, function<bool(Request)> send_l2, MemoryBase& memory, MemoryBase& memory_l2);    //heterogeneous two memory basic, no transfer
    Processor(const Config& configs, const Config& configs_l2, vector<const char*> trace_list, function<bool(Request)> send, function<bool(Request)> send_l2, MemoryBase& memory, MemoryBase& memory_l2, string hma_type);    //heterogeneous two memory basic, hma transfer, epoch based
    Processor(const Config& configs, const Config& configs_l2, vector<const char*> trace_list, function<bool(Request)> send, function<bool(Request)> send_l2, function<bool(Request)> send_mig, function<bool(Request)> send_mig_l2, MemoryBase& memory, MemoryBase& memory_l2, string hma_type);    //heterogeneous two memory basic, hma transfer, onfly based

    void tick();

    void hybrid_tick();	//Mz
    void hybrid_tick_hma_epoch();	//Mz
    void hybrid_tick_hma_onfly();	//Mz

    void receive(Request& req);
    bool finished();
    bool has_reached_limit();

    //std::vector<std::unique_ptr<Core>> cores;
    std::vector<std::shared_ptr<Core>> cores;
    std::vector<double> ipcs;
    double ipc = 0;

    // When early_exit is true, the simulation exits when the earliest trace finishes.
    bool early_exit;

    bool no_core_caches = true;
    bool no_shared_cache = true;

/*
    int l3_size = 1 << 23;
    int l3_assoc = 1 << 3;
*/
    // Mz: Zen has 8MB 16-way L3 for 4 cores, each core runs two threads (treated as two cores in simulation)
    // In our 8 core system we should have 8MB L3 then
    // int l3_size = 1 << 23;
    // int mshr_per_bank = 16;
    // In our 16 core system we should have 16MB then
    int l3_size = 1 << 24; 
    int l3_assoc = 1 << 4;
    int l3_blocksz = 1 << 6;
    int mshr_per_bank = 32; //changed

	// Mz: adding for hma
	string hma_type = "Default"; // this is by default, will be overwritten by hma_type	
	// Mz: adding statistical params
	double total_cpu_instructions = 0;
	void print_stat();
	void print_hybrid_stat();
	void onfly_mig_print();
    std::shared_ptr<CacheSystem> cachesys;
    Cache llc;

    // For onfly
    Migration_controller* migc = NULL;
    Buffer* hot_buffer = new Buffer;
    Buffer* cold_buffer = new Buffer;    

    ScalarStat cpu_cycles;
};

}
#endif /* __PROCESSOR_H */
