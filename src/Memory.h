#ifndef __MEMORY_H
#define __MEMORY_H

#include "Config.h"
#include "DRAM.h"
#include "Request.h"
#include "Controller.h"
#include "SpeedyController.h"
#include "Statistics.h"
#include "GDDR5.h"
#include "HBM.h"
#include "LPDDR3.h"
#include "LPDDR4.h"
#include "WideIO2.h"
#include "DSARP.h"
#include "hma.h"
#include <vector>
#include <functional>
#include <cmath>
#include <cassert>
#include <tuple>
#include <stdint.h>

using namespace std;

namespace ramulator
{
//check_target_memory determines the target memory by looking at the 32 MSBs
//#define CHECK_TARGET_MEM(addr)        (( addr & (~( (1ULL<<30) - 1)) ) > 0ULL) ? 1 : 0;
//#define L2M_START_OFFSET 1073741824

#define ADJUST_VA(req_orig_va, req_coreid)\
                ( (req_coreid<<60ULL) + (req_orig_va & 0x0fffffffffffffff) );

class MemoryBase{
public:
    MemoryBase() {}
    virtual ~MemoryBase() {}
    virtual double clk_ns() = 0;
    virtual void tick() = 0;
    virtual bool send(Request req) = 0;
    virtual int pending_requests() = 0;
    virtual void finish(void) = 0;
    virtual void finish_l2(void) = 0;
    virtual long page_allocator(long addr, int coreid, uint64_t addr_temp) = 0;	//Mz
	virtual long flat_allocate_va_to_pa(uint64_t req_orig_va, uint64_t req_coreid) = 0;
    virtual void record_core(int coreid) = 0;
    virtual uint64_t provide_stat() = 0;	//Mz
    virtual void override_memory_page_size(int size) = 0;	//Mz
    virtual pair<uint64_t, int> provide_page_info() = 0;	//Mz

};

template <class T, template<typename> class Controller = Controller >
class Memory : public MemoryBase
{
protected:
  ScalarStat dram_capacity;
  ScalarStat num_dram_cycles;
  ScalarStat num_incoming_requests;
  VectorStat num_read_requests;
  VectorStat num_write_requests;
  ScalarStat ramulator_active_cycles;
  VectorStat incoming_requests_per_channel;
  VectorStat incoming_read_reqs_per_channel;

  ScalarStat physical_page_replacement;
  ScalarStat maximum_bandwidth;
  ScalarStat in_queue_req_num_sum;
  ScalarStat in_queue_read_req_num_sum;
  ScalarStat in_queue_write_req_num_sum;
  ScalarStat in_queue_req_num_avg;
  ScalarStat in_queue_read_req_num_avg;
  ScalarStat in_queue_write_req_num_avg;

#ifndef INTEGRATED_WITH_GEM5
  VectorStat record_read_requests;
  VectorStat record_write_requests;
#endif
    //MZ onfly
  ScalarStat num_onfly_incoming_read_requests;
  ScalarStat num_onfly_incoming_write_requests;


  long max_address;
public:
    enum class Type {
        ChRaBaRoCo,
        RoBaRaCoCh,
        MAX,
    } type = Type::RoBaRaCoCh;

    enum class Translation {
      None,
      Random,
	  Flat,
      MAX,
    } translation = Translation::None;

    std::map<string, Translation> name_to_translation = {
      {"None", Translation::None},
      {"Random", Translation::Random},
      {"Flat", Translation::Flat},
    };

    vector<int> free_physical_pages;
    long free_physical_pages_remaining;
    map<pair<int, long>, long> page_translation;

	// Mz: pin trace format translation
	
	// Default memory page size params, can be overwritten by HMA_EPOCH or HMA_ONFLY mode by providing the page size in HMA-config.cfg file
    uint64_t ROW_SHIFT = 11;  // assuming default page size 2KB, same as row buffer
	int BASIC_PG_SZ_IN_KB = 2;
    int no_of_lines_in_page = 32;

    uint64_t L2_START_OFFSET = L2M_START_OFFSET;  // 1GB, First 1 GB is fast L1 memory, so L2 memory final addresses must start at offset of 1GB
    uint64_t L1_MAX_SIZE_MB = 1024;
	uint64_t total_memory_incoming_rw_requests = 0; 

    //Mz: needed for va to pa flat address allocation
    uint64_t l1_max_pages = L1_MAX_SIZE_MB * 1024 / BASIC_PG_SZ_IN_KB;
    uint64_t l1_count = 0, l2_count = 0;
    int chunk_size_in_pages = 8;    //by deafult page size is 2KB so assign 8 such pages at a time from l1m/l2m
    short l1_flag = 1, l2_flag = 0, l2_permanent_flag = 0;
    map<uint64_t, uint64_t> l1_pte_map, l2_pte_map;
	uint64_t tot_rw_transactions = 0;
	////////////////////////////////////////////////////////

    vector<Controller<T>*> ctrls;
    T * spec;
    vector<int> addr_bits;

    int tx_bits;
	//hma
	uint64_t hma_mem_size;
	uint64_t hma_mem_bw;
	int has_hma;

    Memory(const Config& configs, vector<Controller<T>*> ctrls)
        : ctrls(ctrls),
          spec(ctrls[0]->channel->spec),
          addr_bits(int(T::Level::MAX))
    {
        // make sure 2^N channels/ranks
        // TODO support channel number that is not powers of 2
        int *sz = spec->org_entry.count;
        assert((sz[0] & (sz[0] - 1)) == 0);
        assert((sz[1] & (sz[1] - 1)) == 0);
        // validate size of one transaction
        int tx = (spec->prefetch_size * spec->channel_width / 8);
        tx_bits = calc_log2(tx);
        assert((1<<tx_bits) == tx);
        // If hi address bits will not be assigned to Rows
        // then the chips must not be LPDDRx 6Gb, 12Gb etc.
        if (type != Type::RoBaRaCoCh && spec->standard_name.substr(0, 5) == "LPDDR")
            assert((sz[int(T::Level::Row)] & (sz[int(T::Level::Row)] - 1)) == 0);

        max_address = spec->channel_width / 8;

        for (unsigned int lev = 0; lev < addr_bits.size(); lev++) {
          addr_bits[lev] = calc_log2(sz[lev]);
            max_address *= sz[lev];
        }

        addr_bits[int(T::Level::MAX) - 1] -= calc_log2(spec->prefetch_size);

         //hma
		hma_mem_size = (uint64_t) max_address;
		int *hma_sz = spec->org_entry.count;
		hma_mem_bw = spec->speed_entry.rate * 1e6 * spec->channel_width * hma_sz[int(T::Level::Channel)] / 8;

		if(configs["trace_type"] == "HMA_EPOCH")
			has_hma = 1;
		else has_hma = 0;

        // Initiating translation
        if (configs.contains("translation")) {
          translation = name_to_translation[configs["translation"]];
        }
        if (translation != Translation::None) {
          // construct a list of available pages
          // TODO: this should not assume a 4KB page!
          free_physical_pages_remaining = max_address >> 12;

          free_physical_pages.resize(free_physical_pages_remaining, -1);
        }

        dram_capacity
            .name("dram_capacity")
            .desc("Number of bytes in simulated DRAM")
            .precision(0)
            ;
        dram_capacity = max_address;

        num_dram_cycles
            .name("dram_cycles")
            .desc("Number of DRAM cycles simulated")
            .precision(0)
            ;
        num_incoming_requests
            .name("incoming_requests")
            .desc("Number of incoming requests to DRAM")
            .precision(0)
            ;
        num_read_requests
            .init(configs.get_core_num())
            .name("read_requests")
            .desc("Number of incoming read requests to DRAM per core")
            .precision(0)
            ;
        num_write_requests
            .init(configs.get_core_num())
            .name("write_requests")
            .desc("Number of incoming write requests to DRAM per core")
            .precision(0)
            ;
        incoming_requests_per_channel
            .init(sz[int(T::Level::Channel)])
            .name("incoming_requests_per_channel")
            .desc("Number of incoming requests to each DRAM channel")
            ;
        incoming_read_reqs_per_channel
            .init(sz[int(T::Level::Channel)])
            .name("incoming_read_reqs_per_channel")
            .desc("Number of incoming read requests to each DRAM channel")
            ;

        ramulator_active_cycles
            .name("ramulator_active_cycles")
            .desc("The total number of cycles that the DRAM part is active (serving R/W)")
            .precision(0)
            ;
        physical_page_replacement
            .name("physical_page_replacement")
            .desc("The number of times that physical page replacement happens.")
            .precision(0)
            ;
        maximum_bandwidth
            .name("maximum_bandwidth")
            .desc("The theoretical maximum bandwidth (Bps)")
            .precision(0)
            ;
        in_queue_req_num_sum
            .name("in_queue_req_num_sum")
            .desc("Sum of read/write queue length")
            .precision(0)
            ;
        in_queue_read_req_num_sum
            .name("in_queue_read_req_num_sum")
            .desc("Sum of read queue length")
            .precision(0)
            ;
        in_queue_write_req_num_sum
            .name("in_queue_write_req_num_sum")
            .desc("Sum of write queue length")
            .precision(0)
            ;
        in_queue_req_num_avg
            .name("in_queue_req_num_avg")
            .desc("Average of read/write queue length per memory cycle")
            .precision(6)
            ;
        in_queue_read_req_num_avg
            .name("in_queue_read_req_num_avg")
            .desc("Average of read queue length per memory cycle")
            .precision(6)
            ;
        in_queue_write_req_num_avg
            .name("in_queue_write_req_num_avg")
            .desc("Average of write queue length per memory cycle")
            .precision(6)
            ;
#ifndef INTEGRATED_WITH_GEM5
        record_read_requests
            .init(configs.get_core_num())
            .name("record_read_requests")
            .desc("record read requests for this core when it reaches request limit or to the end")
            ;

        record_write_requests
            .init(configs.get_core_num())
            .name("record_write_requests")
            .desc("record write requests for this core when it reaches request limit or to the end")
            ;
#endif
        num_onfly_incoming_read_requests
            .name("onfly_incoming_read_requests")
            .desc("Number of onfly read requests to DRAM")
            .precision(0)
            ;
        num_onfly_incoming_write_requests
            .name("onfly_incoming_write_requests")
            .desc("Number of onfly write requests to DRAM")
            .precision(0)
            ;
		cout << "print out core nums: " << configs.get_core_num() << "\n" << std::flush;

    }

    ~Memory()
    {
        for (auto ctrl: ctrls)
            delete ctrl;
        delete spec;
    }

    double clk_ns()
    {
        return spec->speed_entry.tCK;
    }

    void record_core(int coreid) {
#ifndef INTEGRATED_WITH_GEM5
      record_read_requests[coreid] = num_read_requests[coreid];
      record_write_requests[coreid] = num_write_requests[coreid];
#endif
      for (auto ctrl : ctrls) {
        ctrl->record_core(coreid);
      }
    }

    void tick()
    {
        ++num_dram_cycles;
        int cur_que_req_num = 0;
        int cur_que_readreq_num = 0;
        int cur_que_writereq_num = 0;
        for (auto ctrl : ctrls) {
          cur_que_req_num += ctrl->readq.size() + ctrl->writeq.size() + ctrl->pending.size();
          cur_que_readreq_num += ctrl->readq.size() + ctrl->pending.size();
          cur_que_writereq_num += ctrl->writeq.size();
        }
        in_queue_req_num_sum += cur_que_req_num;
        in_queue_read_req_num_sum += cur_que_readreq_num;
        in_queue_write_req_num_sum += cur_que_writereq_num;

        bool is_active = false;
        for (auto ctrl : ctrls) {
          is_active = is_active || ctrl->is_active();
          ctrl->tick();
        }
        if (is_active) {
          ramulator_active_cycles++;
        }
    }

    void tick_onfly_migration(int flag)
    {
        ++num_dram_cycles;
        int cur_que_req_num = 0;
        int cur_que_readreq_num = 0;
        int cur_que_writereq_num = 0;
        for (auto ctrl : ctrls) {
          cur_que_req_num += ctrl->readq.size() + ctrl->writeq.size() + ctrl->pending.size();
          cur_que_readreq_num += ctrl->readq.size() + ctrl->pending.size();
          cur_que_writereq_num += ctrl->writeq.size();
        }
        in_queue_req_num_sum += cur_que_req_num;
        in_queue_read_req_num_sum += cur_que_readreq_num;
        in_queue_write_req_num_sum += cur_que_writereq_num;

        bool is_active = false;
        for (auto ctrl : ctrls) {
          is_active = is_active || ctrl->is_active();
          ctrl->tick_onfly_migration(flag);
        }
        if (is_active) {
          ramulator_active_cycles++;
        }
    }

    bool send(Request req)
    {
        req.addr_vec.resize(addr_bits.size());

        //MZ: modifying it to phys_addr to take care of L2M starting offset
        //long addr = req.addr;
        long addr = req.phys_addr;	// by default req.phys_addr is always req.addr, only in case of hybrid memory it is changed

        int coreid = req.coreid;

        // Each transaction size is 2^tx_bits, so first clear the lowest tx_bits bits
        clear_lower_bits(addr, tx_bits);
        switch(int(type)){
            case int(Type::ChRaBaRoCo):
                for (int i = addr_bits.size() - 1; i >= 0; i--)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                break;
            case int(Type::RoBaRaCoCh):
                req.addr_vec[0] = slice_lower_bits(addr, addr_bits[0]);
                req.addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr, addr_bits[addr_bits.size() - 1]);
                for (int i = 1; i <= int(T::Level::Row); i++)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                break;
            default:
                assert(false);
        }

        if(ctrls[req.addr_vec[0]]->enqueue(req)) {
            if(req.req_category != 6) {
                // tally stats here to avoid double counting for requests that aren't enqueued
                ++num_incoming_requests;
                total_memory_incoming_rw_requests++;	// Mz: will be used to know MPKI
                if (req.type == Request::Type::READ) {
                  ++num_read_requests[coreid];
                  ++incoming_read_reqs_per_channel[req.addr_vec[int(T::Level::Channel)]];
                }
                if (req.type == Request::Type::WRITE) {
                  ++num_write_requests[coreid];
                }
                ++incoming_requests_per_channel[req.addr_vec[int(T::Level::Channel)]];
            }
            else if (req.type == Request::Type::WRITE) 
                ++num_onfly_incoming_write_requests;
            return true;
        }

        return false;
    }

    bool send_mig(Request req)
    {
        req.addr_vec.resize(addr_bits.size());

        //MZ: modifying it to phys_addr to take care of L2M starting offset
        //long addr = req.addr;
        long addr = req.phys_addr;	// by default req.phys_addr is always req.addr, only in case of hybrid memory it is changed

        //int coreid = req.coreid;

        // Each transaction size is 2^tx_bits, so first clear the lowest tx_bits bits
        clear_lower_bits(addr, tx_bits);

        switch(int(type)){
            case int(Type::ChRaBaRoCo):
                for (int i = addr_bits.size() - 1; i >= 0; i--)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                break;
            case int(Type::RoBaRaCoCh):
                req.addr_vec[0] = slice_lower_bits(addr, addr_bits[0]);
                req.addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr, addr_bits[addr_bits.size() - 1]);
                for (int i = 1; i <= int(T::Level::Row); i++)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                break;
            default:
                assert(false);
        }

        if(ctrls[req.addr_vec[0]]->enqueue_mig(req)) {
            // tally stats here to avoid double counting for requests that aren't enqueued
            if (req.type == Request::Type::READ) 
                ++num_onfly_incoming_read_requests;
            /*if (req.type == Request::Type::READ) {
              ++num_read_requests[coreid];
              ++incoming_read_reqs_per_channel[req.addr_vec[int(T::Level::Channel)]];
            }
            if (req.type == Request::Type::WRITE) {
              ++num_write_requests[coreid];
            }
            ++incoming_requests_per_channel[req.addr_vec[int(T::Level::Channel)]];*/
            return true;
        }

        return false;
    }

    int pending_requests()
    {
        int reqs = 0;
        for (auto ctrl: ctrls)
            reqs += ctrl->readq.size() + ctrl->writeq.size() + ctrl->otherq.size() + ctrl->pending.size();
        return reqs;
    }

    void finish(void) {
      dram_capacity = max_address;
      int *sz = spec->org_entry.count;
      maximum_bandwidth = spec->speed_entry.rate * 1e6 * spec->channel_width * sz[int(T::Level::Channel)] / 8;
      long dram_cycles = num_dram_cycles.value();
      for (auto ctrl : ctrls) {
        long read_req = long(incoming_read_reqs_per_channel[ctrl->channel->id].value());
        ctrl->finish(read_req, dram_cycles);
      }
		double total_memory_footprint = ( (double) (l1_pte_map.size() + l2_pte_map.size()) * BASIC_PG_SZ_IN_KB )/ (1024 * 1024);

		cout << "tot_rw_tran extra num cores: " << tot_rw_transactions << ", page size (KB): " << BASIC_PG_SZ_IN_KB << ", L1 pages: " << l1_pte_map.size() << ", L2 pages: " << l2_pte_map.size() << ", total memory footprint in GB: " << total_memory_footprint <<  endl;
		if(has_hma)
			cout << "total pages migrated: " << hma.stats.total_pages_transfered <<  endl;

      // finalize average queueing requests
      in_queue_req_num_avg = in_queue_req_num_sum.value() / dram_cycles;
      in_queue_read_req_num_avg = in_queue_read_req_num_sum.value() / dram_cycles;
      in_queue_write_req_num_avg = in_queue_write_req_num_sum.value() / dram_cycles;
    }
	//Mz creating finish_l2 as we do not want everything to be printed again
    void finish_l2(void) {
      dram_capacity = max_address;
      int *sz = spec->org_entry.count;
      maximum_bandwidth = spec->speed_entry.rate * 1e6 * spec->channel_width * sz[int(T::Level::Channel)] / 8;
      long dram_cycles = num_dram_cycles.value();
      for (auto ctrl : ctrls) {
        long read_req = long(incoming_read_reqs_per_channel[ctrl->channel->id].value());
        ctrl->finish(read_req, dram_cycles);
      }
      // finalize average queueing requests
      in_queue_req_num_avg = in_queue_req_num_sum.value() / dram_cycles;
      in_queue_read_req_num_avg = in_queue_read_req_num_sum.value() / dram_cycles;
      in_queue_write_req_num_avg = in_queue_write_req_num_sum.value() / dram_cycles;
    }

	//Mz function to override default 2KB oage size
    void override_memory_page_size(int size)	//Mz
	{
		
		if(size == 2048)	//2KB
		{
			//do nothing, since 2KB configuration is already set by default
		}
		else if(size == 4096)	//4KB
		{
			//ROW_SHIFT = 12;  // 2^12 = 4096
            //no_of_lines_in_page = 64;
			//BASIC_PG_SZ_IN_KB = 4;
            ROW_SHIFT = 12;  // 2^12 = 4096
            no_of_lines_in_page = 64;
            BASIC_PG_SZ_IN_KB = 4;
            chunk_size_in_pages = 4;    //page size is 4KB so assign 4 such pages at a time from l1m/l2m
            l1_max_pages = L1_MAX_SIZE_MB * 1024 / BASIC_PG_SZ_IN_KB;
            printf("Total L1 pages after over riding %ld\n\n", l1_max_pages);
            fflush(0);

        }
		else
		{
			cout<< "Currenlty we only allow page size either 2KB or 4KB";
			exit(0);
		}
		
		return;
	}

    uint64_t provide_stat()	//Mz
	{
		return 	total_memory_incoming_rw_requests;
	}

    pair<uint64_t, int> provide_page_info()
    {
        return make_pair(ROW_SHIFT, no_of_lines_in_page);
    }
    
	long flat_allocate_va_to_pa(uint64_t req_orig_va, uint64_t req_coreid)
	{
		uint64_t r_addr = req_orig_va;
		#ifdef MZ_DEBUG
			printf("In flat allocate function\n");
			cout << "core id is: " << req_coreid << "; va is: " << r_addr ;
		#endif
		long final_flat_pa;
		// First for each core create separate va space by adding the coreid in the 4 MSBs
		// using a macro to implement it
		// Mz: use a macro to add a very large number to the addr
		// for core 1 we should add 0x 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
		// since there are addresses for which 4 MSBs of 64 bit addr is 1111
		// we want to get rid of such addresses, so first we will 0 out the 4MSBs to 0000 and then add core id to get different va for each core

		r_addr = ADJUST_VA(req_orig_va, req_coreid);

		#ifdef MZ_DEBUG
			cout << "\nAfter adjustemtn va is " << r_addr << "\n";
		#endif

		// Second, we need to assign pa to this va,
		// assign 8 free frames from l1m and then 8 free frames from l2m
		// do it in round robin as long as free frames in l1m.
		// then only assign from l2m
		if( !(l1_pte_map.find(r_addr >> ROW_SHIFT) == l1_pte_map.end()) )   // mapping found in l1m
		{
			r_addr = ( (l1_pte_map.find(r_addr >> ROW_SHIFT)->second) << ROW_SHIFT ) | (r_addr & ((1ULL << ROW_SHIFT) - 1));
		}
		else if ( !(l2_pte_map.find(r_addr >> ROW_SHIFT) == l2_pte_map.end()) ) // mapping exists in L2
		{
			r_addr = ( ((l2_pte_map.find(r_addr >> ROW_SHIFT)->second) << ROW_SHIFT) | (r_addr & ((1ULL << ROW_SHIFT) - 1)) ) + L2_START_OFFSET ;
		}
		else    // no existing mapping, need to map it
		{
			if( (l1_flag) && (!l2_permanent_flag) )
			{
				l1_pte_map[r_addr >> ROW_SHIFT] = l1_pte_map.size(); 	// Mz: be careful clang compiles it as reight to left but gcc does smth different
				l1_count++;

				if( (l1_count % 8) == 0)
				{
					l2_flag = 1;
					l1_flag = 0;
				}

				if (l1_count >= l1_max_pages)
				{
					l2_flag = 1;
					l2_permanent_flag = 1;  // l1 is full only l2 pages can be assigned
					l1_flag = 0;
				}
				r_addr = ((l1_pte_map.find(r_addr >> ROW_SHIFT)->second) << ROW_SHIFT) | (r_addr & ((1ULL << ROW_SHIFT) - 1));
			}
			else
			{
				l2_pte_map[r_addr >> ROW_SHIFT] = l2_pte_map.size();	// Mz: be careful clang compiles it as reight to left but gcc does different
				l2_count++;

				if( (l2_count % 8) == 0)
				{
					l1_flag = 1;
					l2_flag = 0;
				}

				r_addr = ( ((l2_pte_map.find(r_addr >> ROW_SHIFT)->second) << ROW_SHIFT) | (r_addr & ((1ULL << ROW_SHIFT) - 1)) ) + L2_START_OFFSET ;
			}
		}

		final_flat_pa = (long) r_addr;
		tot_rw_transactions++;

		#ifdef MZ_DEBUG
			cout << "core id is: " << req_coreid << " count is: " << tot_rw_transactions << " va is: " << req_orig_va << " pa is: " << final_flat_pa << "\n" ;
		#endif

		return final_flat_pa;
		//printf("Flat allocate VA to PA failed, exiting...\n");
		//exit(0);
	}
	
    long page_allocator(long addr, int coreid, uint64_t addr_temp) {
        long virtual_page_number = addr >> 12;

        switch(int(translation)) {
            case int(Translation::None): {
				addr = (long) addr_temp;	// it might generate some -ve address if we are using raw pin traces
              return addr;
            }
            case int(Translation::Flat): {
				// Mz: addr holds dummy, addr_temp holds the virtual address read from pin trace file
				long req_flat_allocated_pa;	// since we want address to be in long format
				uint64_t req_coreid = (uint64_t) coreid;
				
				req_flat_allocated_pa = flat_allocate_va_to_pa(addr_temp, req_coreid);
				return req_flat_allocated_pa;
            }
            case int(Translation::Random): {
                auto target = make_pair(coreid, virtual_page_number);
                if(page_translation.find(target) == page_translation.end()) {
                    // page doesn't exist, so assign a new page
                    // make sure there are physical pages left to be assigned

                    // if physical page doesn't remain, replace a previous assigned
                    // physical page.
                    if (!free_physical_pages_remaining) {
                      physical_page_replacement++;
                      long phys_page_to_read = lrand() % free_physical_pages.size();
                      assert(free_physical_pages[phys_page_to_read] != -1);
                      page_translation[target] = phys_page_to_read;
                    } else {
                        // assign a new page
                        long phys_page_to_read = lrand() % free_physical_pages.size();
                        // if the randomly-selected page was already assigned
                        if(free_physical_pages[phys_page_to_read] != -1) {
                            long starting_page_of_search = phys_page_to_read;

                            do {
                                // iterate through the list until we find a free page
                                // TODO: does this introduce serious non-randomness?
                                ++phys_page_to_read;
                                phys_page_to_read %= free_physical_pages.size();
                            }
                            while((phys_page_to_read != starting_page_of_search) && free_physical_pages[phys_page_to_read] != -1);
                        }

                        assert(free_physical_pages[phys_page_to_read] == -1);

                        page_translation[target] = phys_page_to_read;
                        free_physical_pages[phys_page_to_read] = coreid;
                        --free_physical_pages_remaining;
                    }
                }

                // SAUGATA TODO: page size should not always be fixed to 4KB
                return (page_translation[target] << 12) | (addr & ((1 << 12) - 1));
            }
            default:
                assert(false);
        }

    }

private:

    int calc_log2(int val){
        int n = 0;
        while ((val >>= 1))
            n ++;
        return n;
    }
    int slice_lower_bits(long& addr, int bits)
    {
        int lbits = addr & ((1<<bits) - 1);
        addr >>= bits;
        return lbits;
    }
    void clear_lower_bits(long& addr, int bits)
    {
        addr >>= bits;
    }
    long lrand(void) {
        if(sizeof(int) < sizeof(long)) {
            return static_cast<long>(rand()) << (sizeof(int) * 8) | rand();
        }

        return rand();
    }
};

} /*namespace ramulator*/

#endif /*__MEMORY_H*/
