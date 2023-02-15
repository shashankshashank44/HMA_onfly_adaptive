#include "Processor.h"
#include <iostream>
#include <cassert>

using namespace std;
using namespace ramulator;

Processor::Processor(const Config& configs,
		vector<const char*> trace_list,
		function<bool(Request)> send_memory,
		MemoryBase& memory)
	: ipcs(trace_list.size(), -1),
	early_exit(configs.is_early_exit()),
	no_core_caches(!configs.has_core_caches()),
	no_shared_cache(!configs.has_l3_cache()),
	cachesys(new CacheSystem(configs, send_memory)),
	llc(l3_size, l3_assoc, l3_blocksz,
			mshr_per_bank * trace_list.size(),
			Cache::Level::L3, cachesys) {

		assert(cachesys != nullptr);
		int tracenum = trace_list.size();
		assert(tracenum > 0);
		printf("tracenum: %d\n", tracenum);
		for (int i = 0 ; i < tracenum ; ++i) {
			printf("trace_list[%d]: %s\n", i, trace_list[i]);
		}
		if (no_shared_cache) {
			for (int i = 0 ; i < tracenum ; ++i) {
				cores.emplace_back(new Core(
							configs, i, trace_list[i], send_memory, nullptr,
							cachesys, memory));
			}
		} else {
			for (int i = 0 ; i < tracenum ; ++i) {
				cores.emplace_back(new Core(configs, i, trace_list[i],
							std::bind(&Cache::send, &llc, std::placeholders::_1),
							&llc, cachesys, memory));
			}
		}
		for (int i = 0 ; i < tracenum ; ++i) {
			cores[i]->callback = std::bind(&Processor::receive, this,
					placeholders::_1);
		}

		// regStats
		cpu_cycles.name("cpu_cycles")
			.desc("cpu cycle number")
			.precision(0)
			;
		cpu_cycles = 0;
	}

// For simple hybrid memory, with two types of memories
Processor::Processor(const Config& configs, const Config& configs_l2,
		vector<const char*> trace_list,
		function<bool(Request)> send_memory, function<bool(Request)> send_memory_l2,
		MemoryBase& memory, MemoryBase& memory_l2 )
	: ipcs(trace_list.size(), -1),
	early_exit(configs.is_early_exit()),
	no_core_caches(!configs.has_core_caches()),
	no_shared_cache(!configs.has_l3_cache()),
	cachesys(new CacheSystem(configs, configs_l2, send_memory, send_memory_l2)),
	llc(l3_size, l3_assoc, l3_blocksz,
			mshr_per_bank * trace_list.size(),
			Cache::Level::L3, cachesys) {

		assert(cachesys != nullptr);
		int tracenum = trace_list.size();
		assert(tracenum > 0);
		printf("tracenum: %d\n", tracenum);
		for (int i = 0 ; i < tracenum ; ++i) {
			printf("trace_list[%d]: %s\n", i, trace_list[i]);
		}
		if (no_shared_cache) {
			for (int i = 0 ; i < tracenum ; ++i) {
				cores.emplace_back(new Core(
							configs, i, trace_list[i], send_memory, nullptr,
							cachesys, memory ));	// *Mz* here we are not sending memory_l2 to core, hence when we do not have llc then we can 
													// not have two levels of memory. At this time it simplifies the design. Can be changed later if req.
			}
		} else {
			for (int i = 0 ; i < tracenum ; ++i) {
				cores.emplace_back(new Core(configs, configs_l2, i, trace_list[i],
							std::bind(&Cache::send, &llc, std::placeholders::_1),
							&llc, cachesys, memory, memory_l2));
			}
		}
		for (int i = 0 ; i < tracenum ; ++i) {
			cores[i]->callback = std::bind(&Processor::receive, this,
					placeholders::_1);
		}

		// regStats
		cpu_cycles.name("cpu_cycles")
			.desc("cpu cycle number")
			.precision(0)
			;
		cpu_cycles = 0;
	}

// For simple hybrid memory, with two types of memories and hma transfer, epoch based
Processor::Processor(const Config& configs, const Config& configs_l2,
		vector<const char*> trace_list,
		function<bool(Request)> send_memory, function<bool(Request)> send_memory_l2,
		MemoryBase& memory, MemoryBase& memory_l2, string hma_type )
	: ipcs(trace_list.size(), -1),
	early_exit(configs.is_early_exit()),
	no_core_caches(!configs.has_core_caches()),
	no_shared_cache(!configs.has_l3_cache()),
	hma_type(hma_type),
	cachesys(new CacheSystem(configs, configs_l2, send_memory, send_memory_l2)), /*need to pass hma here?*/
	llc(l3_size, l3_assoc, l3_blocksz,
			mshr_per_bank * trace_list.size(), Cache::Level::L3, cachesys)  {

		cout <<"HMA type: " << hma_type <<  " page size: " << hma.config.page_size << "\n";

		assert(cachesys != nullptr);
		int tracenum = trace_list.size();
		assert(tracenum > 0);
		printf("tracenum: %d\n", tracenum);
		for (int i = 0 ; i < tracenum ; ++i) {
			printf("trace_list[%d]: %s\n", i, trace_list[i]);
		}
		if (no_shared_cache) {
			for (int i = 0 ; i < tracenum ; ++i) {
				cores.emplace_back(new Core(
							configs, i, trace_list[i], send_memory, nullptr,
							cachesys, memory ));	// *Mz* here we are not sending memory_l2 to core, hence when we do not have llc then we can 
													// not have two levels of memory. At this time it simplifies the design. Can be changed later if req.
			}
		} else {
			for (int i = 0 ; i < tracenum ; ++i) {
				cores.emplace_back(new Core(configs, configs_l2, i, trace_list[i],
							std::bind(&Cache::send, &llc, std::placeholders::_1),
							&llc, cachesys, memory, memory_l2));
			}
		}
		for (int i = 0 ; i < tracenum ; ++i) {
			cores[i]->callback = std::bind(&Processor::receive, this,
					placeholders::_1);
		}

		// regStats
		cpu_cycles.name("cpu_cycles")
			.desc("cpu cycle number")
			.precision(0)
			;
		cpu_cycles = 0;
	}

//Mz this is Processor constructor for onfly
// For simple hybrid memory, with two types of memories and hma transfer, onfly based
Processor::Processor(const Config& configs, const Config& configs_l2,
		vector<const char*> trace_list,
		function<bool(Request)> send_memory, function<bool(Request)> send_memory_l2,
		function<bool(Request)> send_mig, function<bool(Request)> send_mig_l2,
		MemoryBase& memory, MemoryBase& memory_l2, string hma_type )
	: ipcs(trace_list.size(), -1),
	early_exit(configs.is_early_exit()),
	no_core_caches(!configs.has_core_caches()),
	no_shared_cache(!configs.has_l3_cache()),
	hma_type(hma_type),
	cachesys(new CacheSystem(configs, configs_l2, send_memory, send_memory_l2)), 
	llc(l3_size, l3_assoc, l3_blocksz,
			mshr_per_bank * trace_list.size(), Cache::Level::L3, cachesys)  {

		cout <<"HMA type: " << hma_type <<  " page size: " << hma.config.page_size << "\n";

        //hot_buffer = new Buffer;
        //cold_buffer = new Buffer;
		assert(cachesys != nullptr);
		int tracenum = trace_list.size();
		assert(tracenum > 0);
		printf("tracenum: %d\n", tracenum);
		for (int i = 0 ; i < tracenum ; ++i) {
			printf("trace_list[%d]: %s\n", i, trace_list[i]);
		}
		if (no_shared_cache) {
			for (int i = 0 ; i < tracenum ; ++i) {
				cores.emplace_back(new Core(
							configs, i, trace_list[i], send_memory, nullptr,
							cachesys, memory ));	// *Mz* here we are not sending memory_l2 to core, hence when we do not have llc then we can 
													// not have two levels of memory. At this time it simplifies the design. Can be changed later if req.
			}
		} else {
			for (int i = 0 ; i < tracenum ; ++i) {
				cores.emplace_back(new Core(configs, configs_l2, i, trace_list[i],
							std::bind(&Cache::send, &llc, std::placeholders::_1),
							&llc, cachesys, memory, memory_l2));
			}
		}
		for (int i = 0 ; i < tracenum ; ++i) {
			cores[i]->callback = std::bind(&Processor::receive, this,
					placeholders::_1);
		}

        migc = new Migration_controller(send_memory, send_memory_l2, send_mig, send_mig_l2, cores, hot_buffer, cold_buffer, &llc, memory, memory_l2);
        cachesys->set_buffers(hot_buffer, cold_buffer);

		// regStats
		cpu_cycles.name("cpu_cycles")
			.desc("cpu cycle number")
			.precision(0)
			;
		cpu_cycles = 0;
	}

void Processor::tick() {
	cpu_cycles++;
	if (!(no_core_caches && no_shared_cache)) {
		cachesys->tick();

	}
	for (unsigned int i = 0 ; i < cores.size() ; ++i) {
		Core* core = cores[i].get();
		core->tick();
	}
}

//Mz: for simple hybrid system with two memories
void Processor::hybrid_tick() {
	cpu_cycles++;
	if (!(no_core_caches && no_shared_cache)) {
		cachesys->hybrid_tick();

	}
	for (unsigned int i = 0 ; i < cores.size() ; ++i) {
		Core* core = cores[i].get();
		core->tick();
	}
}

//Mz: for simple hybrid system with two memories and simple epoch based hma migration
void Processor::hybrid_tick_hma_epoch() {
	cpu_cycles++;
	if (!(no_core_caches && no_shared_cache)) {
		cachesys->hybrid_tick_hma_epoch();

	}
	for (unsigned int i = 0 ; i < cores.size() ; ++i) {
		Core* core = cores[i].get();
		core->tick();	// in core tick function we do not create the page
	}
}

//Mz: for simple hybrid system with two memories and simple onfly based hma migration
void Processor::hybrid_tick_hma_onfly() {
	cpu_cycles++;
    
    //to calculate remap mbq, copy remap table to cache remap table since cache class don't have access to processor (inefficient)	
    cachesys->remap_tab = migc->remap_table;

    //used for adaptive threshold, copying access counts from cachesys to migc variable.
    migc->migrated_hbm_access = cachesys->migrated_hbm_access;    
    migc->migrated_hbm_access_temp = cachesys->migrated_hbm_access_temp;    

    migc->remap_access = cachesys->remap_access;
    
    migc->hybrid_tick_hma_onfly();

    if(migc->adaptive_mig_cycle == 0)       //reset interval
    {
       cachesys->migrated_hbm_access_temp = 0; 
    }

	if (!(no_core_caches && no_shared_cache)) {
		cachesys->hybrid_tick_hma_onfly();

	}
	for (unsigned int i = 0 ; i < cores.size() ; ++i) {
		Core* core = cores[i].get();
		core->tick();	// in core tick function we do not create the page
	}
}

void Processor::receive(Request& req) {
	if (!no_shared_cache) {
		llc.callback(req);
	} else if (!cores[0]->no_core_caches) {
		// Assume all cores have caches or don't have caches
		// at the same time.
		for (unsigned int i = 0 ; i < cores.size() ; ++i) {
			Core* core = cores[i].get();
			core->caches[0]->callback(req);
		}
	}
	/*for (unsigned int i = 0 ; i < cores.size() ; ++i) {
		Core* core = cores[i].get();
		core->receive(req);
	}*/
    // Mz: optimizing, in our case one request always come from a certain core so call receive for that core only
    int core_id = req.coreid;
	Core* core = cores[core_id].get();
	core->receive(req);

}

bool Processor::finished() {
	total_cpu_instructions = 0;
	if (early_exit) {
		for (unsigned int i = 0 ; i < cores.size(); ++i) {
			if (cores[i]->finished()) {
#ifdef MZ_DEBUG
				cout << "at processor finshed, core finished is true" << "\n";
#endif			
				for (unsigned int j = 0 ; j < cores.size() ; ++j) {
					ipc += cores[j]->calc_ipc();
					total_cpu_instructions = total_cpu_instructions + cores[i]->cpu_inst.value();
				}
				return true;
			}
		}
		return false;
	} else {
		for (unsigned int i = 0 ; i < cores.size(); ++i) {
			if (!cores[i]->finished()) {
				return false;
			}
			if (ipcs[i] < 0) {
				ipcs[i] = cores[i]->calc_ipc();
				ipc += ipcs[i];
			}
			//total_cpu_instructions += cores[i].cpu_inst;
			total_cpu_instructions = total_cpu_instructions + cores[i]->cpu_inst.value();

		}

		return true;
	}
}

bool Processor::has_reached_limit() {
	for (unsigned int i = 0 ; i < cores.size() ; ++i) {
		if (!cores[i]->has_reached_limit()) {
			return false;
		}
	}
	return true;
}

void Processor::print_stat() {

	double mpki = 0.0;
	if(cores.size())
		mpki = ( (double)cores[0]->get_memory_count()/total_cpu_instructions) * 1000.0;

	cout << "MPKI " << mpki << " Total memory " << cores[0]->get_memory_count() << " Total cpu " << total_cpu_instructions <<  endl;
	return;
}

void Processor::print_hybrid_stat() {

	double mpki = 0.0;
	if(cores.size())
		mpki = ( (double)cores[0]->get_hybrid_memory_count()/total_cpu_instructions) * 1000.0;

	cout << "MPKI " << mpki << " Total memory " << cores[0]->get_hybrid_memory_count() << " Total cpu " << total_cpu_instructions <<  endl;
	return;
}

void Processor::onfly_mig_print() {
    migc->migrated_hbm_access = cachesys->migrated_hbm_access;
    migc->remap_access = cachesys->remap_access;
	migc->onfly_mig_print();
	return;
}

// Basic single memory system
Core::Core(const Config& configs, int coreid,
		const char* trace_fname, function<bool(Request)> send_next,
		Cache* llc, std::shared_ptr<CacheSystem> cachesys, MemoryBase& memory)
: id(coreid), no_core_caches(!configs.has_core_caches()),
	no_shared_cache(!configs.has_l3_cache()),
	llc(llc), trace(trace_fname), memory(memory), memory_l2(memory) // memory_l2 just initialized to clear the error, not used actually
{
	// Build cache hierarchy
	if (no_core_caches) {
		send = send_next;
	} else {
		/* //original
		// L2 caches[0]
		caches.emplace_back(new Cache(
		l2_size, l2_assoc, l2_blocksz, l2_mshr_num,
		Cache::Level::L2, cachesys));
		// L1 caches[1]
		caches.emplace_back(new Cache(
		l1_size, l1_assoc, l1_blocksz, l1_mshr_num,
		Cache::Level::L1, cachesys));
		send = bind(&Cache::send, caches[1].get(), placeholders::_1);
		if (llc != nullptr) {
		caches[0]->concatlower(llc);
		}
		caches[1]->concatlower(caches[0].get());
		*/
		// Mz: we want two levels of caches private L1 and shared L2, so changing configuration accordingly
		// we will not change the name for L3 since it is hard coded, so what we will have:
		// prvaite L1 -> shared L3
		// L1 caches[0]
		caches.emplace_back(new Cache(
					l1_size, l1_assoc, l1_blocksz, l1_mshr_num,
					Cache::Level::L1, cachesys));
		send = bind(&Cache::send, caches[0].get(), placeholders::_1);
		if (llc != nullptr) {			// we must have an LLC in this setup
			caches[0]->concatlower(llc);
		}
	}

    ar_flag = 0;
    core_halt = 0;
	os_halt_cycle = 0;
	window.mask = (~(l1_blocksz - 1l));
    if(configs["trace_type"] == "HMA_ONFLY") {
        is_hma_onfly = true;
    }

    //Mz: with or w/o cache it will use the same trace format
	more_reqs = trace.get_request_pin_format(bubble_cnt, req_addr, req_type, req_addr_temp, id);
	if (req_addr != -1) 
	{
		//Mz: Though in case of hybrid we will have memory and memory_l2 but we will use only
		//memory.allocator() as we want to keep mapping in one place 
		//(the mapping function we designed it will take care of our HMA style flat memory, 1GB l1m and 16 GB l2m)
		req_addr = memory.page_allocator(req_addr, id, req_addr_temp);	// after returning from page_allocator, req_addr is valid now

	}

	/*if (no_core_caches) {
	  more_reqs = trace.get_filtered_request(
	  bubble_cnt, req_addr, req_type);
	  req_addr = memory.page_allocator(req_addr, id);
	  } else {
	  more_reqs = trace.get_unfiltered_request(
	  bubble_cnt, req_addr, req_type);
	  req_addr = memory.page_allocator(req_addr, id);
	  }*/


	// set expected limit instruction for calculating weighted speedup
	expected_limit_insts = configs.get_expected_limit_insts();

	// regStats
	record_cycs.name("record_cycs_core_" + to_string(id))
		.desc("Record cycle number for calculating weighted speedup. (Only valid when expected limit instruction number is non zero in config file.)")
		.precision(0)
		;

	record_insts.name("record_insts_core_" + to_string(id))
		.desc("Retired instruction number when record cycle number. (Only valid when expected limit instruction number is non zero in config file.)")
		.precision(0)
		;

	memory_access_cycles.name("memory_access_cycles_core_" + to_string(id))
		.desc("memory access cycles in memory time domain")
		.precision(0)
		;
	memory_access_cycles = 0;
	cpu_inst.name("cpu_instructions_core_" + to_string(id))
		.desc("cpu instruction number")
		.precision(0)
		;
	cpu_inst = 0;

	ar_halt_cycles.name("halt_cycle_core_" + to_string(id))
		.desc("halt cycles due to ar")
		.precision(0)
		;
    ar_halt_cycles = 0;
}

//Mz: Core with simple hybrid memory, with two types of memories
Core::Core(const Config& configs, const Config& configs_l2, int coreid,
		const char* trace_fname, function<bool(Request)> send_next,
		Cache* llc, std::shared_ptr<CacheSystem> cachesys, MemoryBase& memory, MemoryBase& memory_l2)
: id(coreid), no_core_caches(!configs.has_core_caches()),
	no_shared_cache(!configs.has_l3_cache()),
	llc(llc), trace(trace_fname), memory(memory), memory_l2(memory_l2) 
{
	// Build cache hierarchy
	if (no_core_caches) {	//Mz: currently fro hybrid memory it will never happen
		send = send_next;
	} else {
		caches.emplace_back(new Cache(
					l1_size, l1_assoc, l1_blocksz, l1_mshr_num,
					Cache::Level::L1, cachesys));
		send = bind(&Cache::send, caches[0].get(), placeholders::_1);
		if (llc != nullptr) {			// we must have an LLC in this setup
			caches[0]->concatlower(llc);
		}
	}

    ar_flag = 0;
    core_halt = 0;
	os_halt_cycle = 0;

	window.mask = (~(l1_blocksz - 1l));
    if(configs["trace_type"] == "HMA_ONFLY") {
        is_hma_onfly = true;
    }
	//Mz: with or w/o cache it will use the same trace format
	more_reqs = trace.get_request_pin_format(bubble_cnt, req_addr, req_type, req_addr_temp, id);
	if (req_addr != -1) 
	{
		//Mz: Though in case of hybrid we will have memory and memory_l2 but we will use only
		//memory.allocator() as we want to keep mapping in one place 
		//(the mapping function we designed it will take care of our HMA style flat memory, 1GB l1m and 16 GB l2m)
		req_addr = memory.page_allocator(req_addr, id, req_addr_temp);	// after returning from page_allocator, req_addr is valid now

	}

	// set expected limit instruction for calculating weighted speedup
	expected_limit_insts = configs.get_expected_limit_insts();

     //Mz 500M trace
     tot_bbl_cnt = 0;
     b_ind = 1;

	// regStats
	record_cycs.name("record_cycs_core_" + to_string(id))
		.desc("Record cycle number for calculating weighted speedup. (Only valid when expected limit instruction number is non zero in config file.)")
		.precision(0)
		;

	record_insts.name("record_insts_core_" + to_string(id))
		.desc("Retired instruction number when record cycle number. (Only valid when expected limit instruction number is non zero in config file.)")
		.precision(0)
		;

	memory_access_cycles.name("memory_access_cycles_core_" + to_string(id))
		.desc("memory access cycles in memory time domain")
		.precision(0)
		;
	memory_access_cycles = 0;
	memory_l2_access_cycles.name("memory_l2_access_cycles_core_" + to_string(id))
		.desc("memory l2 access cycles in memory time domain")
		.precision(0)
		;
	memory_l2_access_cycles = 0;
	cpu_inst.name("cpu_instructions_core_" + to_string(id))
		.desc("cpu instruction number")
		.precision(0)
		;
	cpu_inst = 0;
	ar_halt_cycles.name("halt_cycle_core_" + to_string(id))
		.desc("halt cycles due to ar")
		.precision(0)
		;
    ar_halt_cycles = 0;
}


double Core::calc_ipc()
{
	printf("[%d]retired: %ld, clk, %ld\n", id, retired, clk);
	return (double) retired / clk;
}

void Core::tick()
{
	clk++;

	if(os_halt_cycle > 0) {
			os_halt_cycle--;
            ar_halt_cycles++;
            //exit(0);
            return;
	}

    if(ar_flag) {
        if (core_halt) {
            ar_halt_cycles++;
            //exit(0);
            return;
        }
    }

	retired += window.retire(clk);

	// Mz: expected limit does not work properly, so commenting out below line
	// if (expected_limit_insts == 0 && !more_reqs) return;
	// Mz: instead only relying on more_reqs as previous ramulator
	if (!more_reqs) return;

	// bubbles (non-memory operations)
	int inserted = 0;
	while (bubble_cnt > 0) {
		if (inserted == window.ipc) return;
		if (window.is_full()) return;

		window.insert(true, -1);
		inserted++;
		bubble_cnt--;
		cpu_inst++;
	}

	if (req_type == Request::Type::READ) {
		// read request
		if (inserted == window.ipc) return;
		if (window.is_full()) return;

        /*if(req_addr == 1078961664)
        {
        }*/
		Request req(req_addr, req_type, callback, id);
		if (!send(req)) return;

		window.insert(false, req_addr);
		cpu_inst++;
	}
	else {
		// write request
        /*if(req_addr == 1078961664)
        {
        }*/
		assert(req_type == Request::Type::WRITE);
		Request req(req_addr, req_type, callback, id);
		if (!send(req)) return;
		cpu_inst++;
	}

	if (no_core_caches) {
		more_reqs = trace.get_request_pin_format(bubble_cnt, req_addr, req_type, req_addr_temp, id);
		if (req_addr != -1) {
			req_addr = memory.page_allocator(req_addr, id, req_addr_temp);	// after returning from page_allocator, req_addr is valid now
		}
	} else {
         more_reqs = trace.get_request_pin_format(bubble_cnt, req_addr, req_type, req_addr_temp, id);
         //Mz 500M trace
         tot_bbl_cnt += bubble_cnt;  // this is per core now
 
         if (req_addr != -1) {
             req_addr = memory.page_allocator(req_addr, id, req_addr_temp);  // after returning from page_allocator, req_addr is valid now
         }
        // here check the ar_flag
        if (ar_flag) {
            int64_t pagenum = req_addr/hma.config.page_size;
            if (pagenum == hma.onfly.ar_page_1) {
                core_halt = 1; 
            }
            else if (pagenum == hma.onfly.ar_page_2) {
                core_halt = 1; 
            }
        }

         //Mz 500M trace
         if(tot_bbl_cnt > expected_limit_insts)  {
             more_reqs = false;
         }
 
     } //else

#ifdef MZ_DEBUG
	if (!more_reqs) 
	{
		cout << "File reading complete,  core tick: " << clk << " no. of sent req: " <<  long(cpu_inst.value())<< "\n";
	}
#endif			
}

bool Core::finished()
{

#ifdef MZ_DEBUG
	cout << "at core finished " << "\n";
#endif			
	return !more_reqs && window.is_empty();
}

bool Core::has_reached_limit() {
	return reached_limit;
}

void Core::receive(Request& req)
{
	//window.set_ready(req.addr, ~(l1_blocksz - 1l));
    // aligning req.addr at cache block 
    long req_addr = (req.addr & window.mask);
    long delayed_time = 0;

    if(is_hma_onfly) {
        if(req.req_category == 5) {
            delayed_time = clk + REMAP_TABLE_DELAY + HC_BUFFER_DELAY; // 3 is for accessing remap table and 2 cycle to read from hot/cold buffer
        }
        else delayed_time = clk + REMAP_TABLE_DELAY;     // With CACTI, for 16KB (16B *1024 entries), it takes 3 cycles to access remap_table.
    }
    else {
        delayed_time = clk;     // No added delay in other modes of runnig 
    }

	window.set_ready_optimized(req_addr, delayed_time);

	if (req.arrive != -1 && req.depart > last) {
		memory_access_cycles += (req.depart - max(last, req.arrive));
		last = req.depart;
	}
}

void Core::receive_for_mig_buffer(long r_addr, long temp_clk)
{
	window.set_ready_for_mig_buffer(r_addr, temp_clk);

}

uint64_t Core::get_memory_count()
{
	return memory.provide_stat();

}

uint64_t Core::get_hybrid_memory_count()
{
	uint64_t total_mem_count =  memory.provide_stat() + memory_l2.provide_stat();
	return total_mem_count;

}

//Mz Migc functions
Migration_controller::Migration_controller(function<bool(Request)> send_memory, function<bool(Request)> send_memory_l2, function<bool(Request)> send_mig, function<bool(Request)> send_mig_l2, std::vector<std::shared_ptr<Core>> mig_cores, Buffer* hb, Buffer *cb, Cache* llc, MemoryBase& memory, MemoryBase& memory_l2)
: send_memory(send_memory), send_memory_l2(send_memory_l2), send_mig(send_mig), send_mig_l2(send_mig_l2), mig_cores(mig_cores),
	hot_buffer(hb), cold_buffer(cb), llc(llc), memory(memory), memory_l2(memory_l2), mig_callback(bind(&Migration_controller::mig_receive, this, placeholders::_1)) 
{
    mig_window = new Window(hot_buffer, cold_buffer);
    
    memory_page_info = memory.provide_page_info();
    // over writing the q and buffer sizes from default 32 depending on page size
    lines_in_page = memory_page_info.second;
 
    hot_mig_stage_q.max = lines_in_page;
    cold_mig_stage_q.max = lines_in_page;
    hot_buffer->max = lines_in_page;
    cold_buffer->max = lines_in_page;
}

void Migration_controller::mig_receive(Request& req)    // called on mig_window
{
	mig_window->mig_set_ready(req.addr, req.req_target_mem);   // for migration window addresses are already aligned at cache block

	/*if (req.arrive != -1 && req.depart > last) {
		memory_access_cycles += (req.depart - max(last, req.arrive));
		last = req.depart;
	}*/
}

bool Migration_controller::onfly_fill_mig_stage_q(hma_struct_t *hma)
{
    int64_t hot_page_addr, hot_frame_addr, cold_page_addr, cold_frame_addr, hot_page_no, cold_page_no;
    int j = 0;
    int pending_count = 0;

    if (hma->onfly.mig_type == 1 && hma->onfly.reverse_migration == 1)
    {
    	cold_page_no = hma->onfly.cold_pg_num;
    	pending_count += llc->onfly_search_set_init(cold_page_no);
        
		if( cold_mig_stage_q.pg_q_size() == 0 )  
		{
		    cold_page_addr = hma->onfly.cold_pg_num;  //this is page no., does not include last 11 bits 
		    cold_page_addr = cold_page_addr << memory_page_info.first;  //now this is byte level page addr

		    cold_frame_addr = hma->onfly.cold_frame_addr;  //this is actual byte-level starting addr of the frame, so no need ot add any bits
            
            //rev_mig
            hot_frame_addr = hma->onfly.hot_frame_addr;

		    for(j = 0; j < lines_in_page; j++)
		    {
                cold_mig_stage_q.ms_pg_addr_q.push_back(cold_page_addr);
                cold_mig_stage_q.pg_backup.push_back(cold_page_addr);
                cold_page_addr += 64;

                cold_mig_stage_q.ms_fr_addr_q.push_back(cold_frame_addr);
                cold_mig_stage_q.fr_backup.push_back(cold_frame_addr);// no use
                cold_frame_addr += 64;

                hot_mig_stage_q.ms_fr_addr_q.push_back(hot_frame_addr);//no use
                hot_mig_stage_q.fr_backup.push_back(hot_frame_addr);
                hot_frame_addr += 64;
		    }
		}
		else
		{
		    //check why the hot_q is not cleared from last itme
		    #ifdef MZ_DEBUG_ONFLY
			printf("Cold Mig stage q is not empty!!\n");
		    #endif
		}

     
	    if(pending_count == 0)
            hma->onfly.mig_status = 1;
	    else
		    hma->onfly.mig_status = 2;
        
        /*bool check_flag = false;
        for(int i = 0; i < remap_table.size(); i++)
        {
            if(remap_table[i].first.first == hot_page_no && hma->onfly.reverse_migration == 0)
            {
                remap_table[i].second.first = 1;
                remap_table[i].second.second = mig_pair_count;
                check_flag = true;
				exit(0);
                break;
            }
        }
        if(!check_flag && !hma->onfly.reverse_migration)
	        remap_table.push_back(make_pair(make_pair(hot_page_no,hma->onfly.cold_frame_addr), make_pair(1, mig_pair_count)));		// new is frame address
        
        check_flag = false;
        for(int i = 0; i < remap_table.size(); i++)
        {
            if(remap_table[i].first.first == cold_page_no && hma->onfly.reverse_migration == 0)
            {
                remap_table[i].second.first = 1;
                remap_table[i].second.second = mig_pair_count;
                check_flag = true;
				exit(0);
                break;
            }
        }
        if(!check_flag && !hma->onfly.reverse_migration)
		    remap_table.push_back(make_pair(make_pair(cold_page_no,hma->onfly.hot_frame_addr), make_pair(1, mig_pair_count)));		//new one is frame address

		// update the remap table size in hma
		hma->onfly.remap_table_size = remap_table.size();*/
        //cout << "REMAP TABLE SIZE after inserting: " <<remap_table.size() << "\n";
        return true;

    }

 
	if ( hma->onfly.mig_type == 1 ) 
	{
        hot_page_no = hma->onfly.hot_pg_num;
    	pending_count = llc->onfly_search_set_init(hot_page_no);    

        #ifdef MZ_DEBUG_ONFLY
        #endif
        if( hot_mig_stage_q.pg_q_size() == 0 )  
        {
            hot_page_addr = hma->onfly.hot_pg_num;  //this is page no., does not include last 11 bits 
            hot_page_addr = hot_page_addr << memory_page_info.first;  //now this is byte level page addr

            hot_frame_addr = hma->onfly.hot_frame_addr;  //this is actual byte-level starting addr of the frame, so no need ot add any bits
            cold_frame_addr = hma->onfly.cold_frame_addr;
            for(j = 0; j < lines_in_page; j++)
            {
                hot_mig_stage_q.ms_pg_addr_q.push_back(hot_page_addr);	    
                hot_mig_stage_q.pg_backup.push_back(hot_page_addr);
                hot_page_addr += 64;

                hot_mig_stage_q.ms_fr_addr_q.push_back(hot_frame_addr);
                hot_mig_stage_q.fr_backup.push_back(hot_frame_addr);
                hot_frame_addr += 64;

                cold_mig_stage_q.ms_fr_addr_q.push_back(cold_frame_addr);
                cold_mig_stage_q.fr_backup.push_back(cold_frame_addr);
                cold_frame_addr += 64;
            }
        }
        else
        {
            //check why the hot_q is not cleared from last itme
            #ifdef MZ_DEBUG_ONFLY
                printf("Hot Mig stage q is not empty!!\n");
            #endif
        }

	    if(pending_count == 0)
            hma->onfly.mig_status = 1;
	    else
		    hma->onfly.mig_status = 2;

        bool check_flag = false;
        for(int i=0;i<remap_table.size();i++)
        {
            if(remap_table[i].first.first == hot_page_no && hma->onfly.reverse_migration == 0)
            {
                remap_table[i].second.first = 1;
                remap_table[i].second.second = mig_pair_count;
                check_flag = true;
				cout <<"Found new migration in rmap table ...exiting\n";
				exit(0);
                break;
            }
        }
        if(!check_flag && !hma->onfly.reverse_migration)
	        remap_table.push_back(make_pair(make_pair(hot_page_no,hma->onfly.cold_frame_addr), make_pair(1, mig_pair_count) ));			// new is frame address
		// update the remap table size in hma
		hma->onfly.remap_table_size = remap_table.size();
        //cout << "REMAP TABLE SIZE after inserting: " <<remap_table.size() << "\n";
	    return true;
    }
    else if (hma->onfly.mig_type == 2)
    {	
        #ifdef MZ_DEBUG_ONFLY
            cout<<"type 2 pending_count "<<pending_count<<"\n";
        #endif
        hot_page_no = hma->onfly.hot_pg_num;
    	pending_count = llc->onfly_search_set_init(hot_page_no);    
    	cold_page_no = hma->onfly.cold_pg_num;
    	pending_count += llc->onfly_search_set_init(cold_page_no);
    
        if( hot_mig_stage_q.pg_q_size() == 0 )  
        {
            hot_page_addr = hma->onfly.hot_pg_num;  //this is page no., does not include last 11 bits 
            hot_page_addr = hot_page_addr << memory_page_info.first;  //now this is byte level page addr

            hot_frame_addr = hma->onfly.hot_frame_addr;  //this is actual byte-level starting addr of the frame, so no need ot add any bits

            for(j = 0; j < lines_in_page; j++)
            {
                hot_mig_stage_q.ms_pg_addr_q.push_back(hot_page_addr);	    
                hot_mig_stage_q.pg_backup.push_back(hot_page_addr);
                hot_page_addr += 64;

                hot_mig_stage_q.ms_fr_addr_q.push_back(hot_frame_addr);
                hot_mig_stage_q.fr_backup.push_back(hot_frame_addr);
                hot_frame_addr += 64;
            }
        }
        else
        {
            //check why the hot_q is not cleared from last itme
            #ifdef MZ_DEBUG_ONFLY
                printf("Hot Mig stage q is not empty!!\n");
            #endif
        }
		if( cold_mig_stage_q.pg_q_size() == 0 )  
		{
		    cold_page_addr = hma->onfly.cold_pg_num;  //this is page no., does not include last 11 bits 
		    cold_page_addr = cold_page_addr << memory_page_info.first;  //now this is byte level page addr

		    cold_frame_addr = hma->onfly.cold_frame_addr;  //this is actual byte-level starting addr of the frame, so no need ot add any bits

		    for(j = 0; j < lines_in_page; j++)
		    {
                cold_mig_stage_q.ms_pg_addr_q.push_back(cold_page_addr);
                cold_mig_stage_q.pg_backup.push_back(cold_page_addr);
                cold_page_addr += 64;

                cold_mig_stage_q.ms_fr_addr_q.push_back(cold_frame_addr);
                cold_mig_stage_q.fr_backup.push_back(cold_frame_addr);
                cold_frame_addr += 64;

		    }
		}
		else
		{
		    //check why the hot_q is not cleared from last itme
		    #ifdef MZ_DEBUG_ONFLY
			printf("Cold Mig stage q is not empty!!\n");
		    #endif
		}

     
	    if(pending_count == 0)
            hma->onfly.mig_status = 1;
	    else
		    hma->onfly.mig_status = 2;
        
        bool check_flag = false;
        for(int i = 0; i < remap_table.size(); i++)
        {
            if(remap_table[i].first.first == hot_page_no && hma->onfly.reverse_migration == 0)
            {
                remap_table[i].second.first = 1;
                remap_table[i].second.second = mig_pair_count;
                check_flag = true;
				cout << "Found new migration in rmap table ...exiting\n";
				exit(0);
                break;
            }
        }
        if(!check_flag && !hma->onfly.reverse_migration)
	        remap_table.push_back(make_pair(make_pair(hot_page_no,hma->onfly.cold_frame_addr), make_pair(1, mig_pair_count)));		// new is frame address
        
        check_flag = false;
        for(int i = 0; i < remap_table.size(); i++)
        {
            if(remap_table[i].first.first == cold_page_no && hma->onfly.reverse_migration == 0)
            {
                remap_table[i].second.first = 1;
                remap_table[i].second.second = mig_pair_count;
                check_flag = true;
				cout << "Found new migration in rmap table ...exiting\n";
				exit(0);
                break;
            }
        }
        if(!check_flag && !hma->onfly.reverse_migration)
		    remap_table.push_back(make_pair(make_pair(cold_page_no,hma->onfly.hot_frame_addr), make_pair(1, mig_pair_count)));		//new one is frame address

		// update the remap table size in hma
		hma->onfly.remap_table_size = remap_table.size();
        //cout << "REMAP TABLE SIZE after inserting: " <<remap_table.size() << "\n";
        return true;
    }

    return false;    
}

bool Migration_controller::onfly_mig_demand_pending()
{
    int64_t hot_page_no = hma.onfly.hot_pg_num;
    int pending_count = llc->onfly_search(hot_page_no);    

    if(hma.onfly.mig_type == 2)
    { 
        int64_t cold_page_no = hma.onfly.cold_pg_num;
        pending_count += llc->onfly_search(cold_page_no);    
    }

    if(pending_count == 0)  // nothing is pending
        return false;
    else
    { 
        #ifdef MZ_DEBUG_ONFLY
            printf("waiting in mig_status 2\n");
        #endif
    }

    return true;
}

int Migration_controller::onfly_mig_send_read()
{
    //send the hot and cold page migration requests to memories
    int64_t mig_req_page_addr, mig_req_phys_addr; 
    static int hot_req_count = 0, cold_req_count = 0;
     
    #ifdef MZ_DEBUG_ONFLY
        printf("onfly mig req to mem()\n");
    #endif
    if( hot_mig_stage_q.pg_q_size() > 0 )  
    {
        //process hot q, send 1 request per cpu cycle
        mig_req_page_addr = hot_mig_stage_q.ms_pg_addr_q.front();
        mig_req_phys_addr = hot_mig_stage_q.ms_fr_addr_q.front();
	
        if(!mig_window->is_full()) //onfly- change logic based on mig_type, if mif_window is full. if mig_type=1, window size need to be 32, if 2, size should be 64
        {
            // make the req and send the req
            int req_category = 6;       //mig req going to memory
            //int delay = mig_delay;      // if any ??

            Request mig_req((long)mig_req_page_addr, Request::Type::READ, req_category, mig_callback, hma.onfly.hot_pg_core_id); 
            mig_req.phys_addr = mig_req_phys_addr;  //  this is very important since send() now relies on req.phys_addr, not req.addr
            mig_req.req_target_mem = 1;
            // hot_mig_stage_q always should hold addr of hot pages which must go to PCM
            // so, sending the mig_req to l2 without further check
            if(send_mig_l2(mig_req)) // if the mig_req inserted successfully in the mig_q
            {
                hot_mig_stage_q.ms_pg_addr_q.pop_front();
                hot_mig_stage_q.ms_fr_addr_q.pop_front();

                mig_window->insert(false, (long)mig_req_page_addr);
                hot_req_count++;    //should be 32 when all requests are sent out
                hma.onfly.no_mig_reqs++;
                return 0;   // optimization: when successful return 0, so that in this cpu cycle processor do on-demand requests
            
	        }
            #ifdef MZ_DEBUG_ONFLY
                printf("mig_window is not full but send_mig_l2 failed\n");
            #endif

        }
        #ifdef MZ_DEBUG_ONFLY
            printf("mig_window is full\n");
        #endif
        return 0;  // let the processor process some on-demand req in this cycle
    }
    else if(cold_mig_stage_q.pg_q_size() > 0)    			// else if is fine
    {
        //process hot q, send 1 request per cpu cycle
        mig_req_page_addr = cold_mig_stage_q.ms_pg_addr_q.front();
        mig_req_phys_addr = cold_mig_stage_q.ms_fr_addr_q.front();

        if(!mig_window->is_full()) 
        {
            // make the req and send the req
            int req_category = 6;       //mig req going to memory
            //int delay = mig_delay;      // if any ??

            Request mig_req((long)mig_req_page_addr, Request::Type::READ, req_category, mig_callback, hma.onfly.cold_pg_core_id); 
            mig_req.phys_addr = mig_req_phys_addr;  //  this is very important since send() now relies on req.phys_addr, not req.addr
            mig_req.req_target_mem = 0;
            // cold_mig_stage_q always should hold addr of cold pages which must go to HBM
            // so, sending the mig_req to l1 without further check
            if(send_mig(mig_req)) // if the mig_req inserted successfully in the mig_q
            {
                cold_mig_stage_q.ms_pg_addr_q.pop_front();
                cold_mig_stage_q.ms_fr_addr_q.pop_front();
                mig_window->insert(false, (long)mig_req_page_addr);
                cold_req_count++;    //should be 32 when all requests are sent out
                hma.onfly.no_mig_reqs++;
                return 0;   // optimization: when successful return 0, so that in this cpu cycle processor do on-demand requests
            }
            #ifdef MZ_DEBUG_ONFLY
                printf("mig_window is not full but send_mig_l2 failed\n");
            #endif

        }
        #ifdef MZ_DEBUG_ONFLY
            printf("mig_window is full\n");
        #endif
        return 0;  // let the processor process some on-demand req in this cycle
    }
    else
    {
        if( (hma.onfly.mig_type == 1) && (hma.onfly.reverse_migration == 1) && (cold_req_count == lines_in_page))
        {
            hma.onfly.mig_status = 3;
            cold_req_count = 0;
            
        }
        else if( (hma.onfly.mig_type == 1) && (hot_req_count == lines_in_page) )
        {   
            #ifdef MZ_DEBUG_ONFLY
                printf("current hot pg num %ld\n",  hma.onfly.hot_pg_num);
            #endif
            hma.onfly.mig_status = 3;
            #ifdef MZ_DEBUG_ONFLY
                printf("mig type %d, mig status %d\n", hma.onfly.mig_type, hma.onfly.mig_status);
            #endif
            hot_req_count = 0;
        }
        else if( ( (hma.onfly.mig_type == 2) && (hot_req_count == lines_in_page) ) && (cold_req_count == lines_in_page) )
        {   
            #ifdef MZ_DEBUG_ONFLY
                printf("current hot pg num %ld\n",  hma.onfly.hot_pg_num);
                printf("current cold pg num %ld\n",  hma.onfly.cold_pg_num);
            #endif
            hma.onfly.mig_status = 3;
            #ifdef MZ_DEBUG_ONFLY
                printf("mig type %d, mig status %d\n", hma.onfly.mig_type, hma.onfly.mig_status);
            #endif

            hot_req_count = 0;
            cold_req_count = 0;
        }
        else
        {
            //some problem, chek h/c q s etc
            printf("onfly_send request count do not match, hot %d, cold %d\n", hot_req_count, cold_req_count);
            hma.onfly.mig_status = 0; // no migration: either abort or clean all the q s for these pg requests
            exit(0);
 
            hot_req_count = 0;
            cold_req_count = 0;
        }
    }

    return 0;
}
void Migration_controller::onfly_mig_receive_clearing_demand(long req_addr, long temp_clk, int cid)
{
    int returned_count = 0;
    returned_count = llc->callback_migration(req_addr, cid, 0); 
    
    // if it was found in LLC MSHR, only then we will clean window
    if(returned_count) {
        //cout << "MIG_CLK: "<< mig_clk << " req_addr from onfly_mig_receive_clearing_demand: " << req_addr << "\n";
    	Core* m_core = mig_cores[cid].get();
        m_core->receive_for_mig_buffer(req_addr, temp_clk);
    }

}

int Migration_controller::onfly_mig_start()
{
	
        //depending on proc.mig_status is being passed as a flag to the memory and controller tick functions
        //so, they will change mode to start processing mig qs with highest priority
        mig_retired_current += mig_window->mig_retire();    // dont see any reason to pass clk
       // mig_retired += mig_retired_current; 
       //long buffer_access_time = 2; // accessing a cache line size data from 2KB buffer will only take 2 core cycels, (same as L1 cache)

        for(int i = 0; i < hot_buffer->bf_req.size(); i++)
        {
            if(hot_buffer->bf_req[i].second == 0)
            {
                #ifdef MZ_DEBUG_ONFLY
                    cout<<" hot received, set_ready "<<hot_buffer->bf_req[i].first<< ", mig retired: " << mig_retired_current << "\n";
                #endif

                //long temp_clk = mig_clk + buffer_access_time; // we should also add remap table access time that is 3 cycles
                //onfly_mig_receive_clearing_demand(hot_buffer->bf_req[i].first, temp_clk, hma.onfly.hot_pg_core_id);
                hot_buffer->bf_req[i].second = 1;
            } 
        }

        
        for(int i = 0;i < cold_buffer->bf_req.size();i++)
        {
            if(cold_buffer->bf_req[i].second == 0)
            {
                #ifdef MZ_DEBUG_ONFLY
                    cout<<" cold received, set_ready "<< cold_buffer->bf_req[i].first<< ", mig retired: " << mig_retired_current << "\n";
                #endif
                //long temp_clk = mig_clk + buffer_access_time;
                //onfly_mig_receive_clearing_demand(cold_buffer->bf_req[i].first, temp_clk, hma.onfly.cold_pg_core_id);
                cold_buffer->bf_req[i].second = 1;
            } 
        }

        if (mig_retired_current == hma.onfly.no_mig_reqs)
        {
            #ifdef MZ_DEBUG_ONFLY
            for(int i = 0; i < hot_buffer->bf_req.size(); i++)
            {
                cout<<" HB contents "<<hot_buffer->bf_req[i].first<<"\n"; 
            }
            #endif

            hma.onfly.mig_status = 4;
            mig_retired_current = 0;
            hma.onfly.no_mig_reqs = 0;
            /*struct req_page *m;
            int64_t m_pagenum = hma.onfly.hot_pg_num;
	    
            HASH_FIND(r_hh, ptable, &m_pagenum, sizeof(int64_t), m);
         
            if(m == NULL)
            {
                exit(0);
            }*/
            
            #ifdef MZ_DEBUG_ONFLY
                cout << "Mig reading done at CLK: " << mig_clk << ", Total cpu clk cycle taken: "<< (mig_clk - mig_start_clk) << "\n\n";
            #endif
        }

	return 0;

}
void Migration_controller::onfly_mig_print() {
    
    /*req_page*  pg1 = NULL;

    for(pg1 = ptable; pg1 != NULL; pg1 = (struct req_page*)(pg1->r_hh.next))
    {
        if(pg1 != NULL)
        {
        }
    }*/

    cout<< "Total migration count " << hma.stats.pages_migrated<<"\n";
    cout<< "Total Reverse migration count: "<<hma.stats.reverse_migration<<"\n";
    cout << "Total size of remap table " << remap_table.size() << "\n";
    cout << "Total number of address reconciliations " << hma.stats.pages_addr_recon << "\n";
    cout << "Total number of cache lines invalidations due to ar " <<  hma.stats.total_cline_invalidations << "\n";
    cout << "Total number of same page selection before ar " <<  hma.stats.same_page_selected << "\n";

    if(hma.stats.pages_migrated)
        cout << "Average cpu clk cycle per migration " << (int)(total_mig_cycle/hma.stats.pages_migrated) << "\n";
	else
        cout << "Average cpu clk cycle per migration " << 0 << "\n";
    if(hma.stats.pages_addr_recon)
        cout << "Average cpu clk cycle per address reconciliation " << (int)(total_ar_cycle/hma.stats.pages_addr_recon) << "\n";
	else
        cout << "Average cpu clk cycle per address reconciliation " << 0 << "\n";

    if(total_no_of_ar1)
        cout << "Average cpu clk cycle per one page ar " << (int)(total_ar_cycle_ar1/total_no_of_ar1) << " no of ar1 " << total_no_of_ar1 <<"\n";
	else
        cout << "Average cpu clk cycle per one page ar " << 0 << "\n";
    if(total_no_of_ar2)
        cout << "Average cpu clk cycle per two page ar " << (int)(total_ar_cycle_ar2/total_no_of_ar2) << " no of ar2 " << total_no_of_ar2 <<"\n";
	else
        cout << "Average cpu clk cycle per two page ar " << 0 << "\n";

    for (unsigned int i = 0 ; i < mig_cores.size() ; ++i) { // rest all cores' ar_flag
        Core* m_core = mig_cores[i].get();
        cout << "Core " << i << " total ar halt cycle " << m_core->ar_halt_cycles.value() << "\n";
    }
    //cout << "Total migration buffer hit " << window.current_mig_buffer_hits << "\n";
    //cout << "Total no. of read hit we missed due to invalidate in L1M prefetch buffer: " << tot_pref_rd_miss << "\n";
    //cout << "Total no. of clines write back to L2M from L1M prefetch buffer: " << tot_wr_bk_cline_L2_mem << "\n";
    //cout << "Total no. of page containing the above clines are: " << tot_wr_bk_pg_L2_mem << "\n";
    //cout << "Total no. of atend clines write back to L2M from L1M prefetch buffer: " << tot_wr_bk_cline_L2_mem_atend << "\n";
    //cout << "Total no. of atend page containing the above clines are: " << tot_wr_bk_pg_L2_mem_atend << "\n";

    int64_t hot_count = 0, cold_count = 0, total_buffer_hits = 0;
    uint64_t tot_l1m_access = 0, tot_l2m_access = 0;

    /*cout<<"Threshold reach\n";
    for(long i=0; i<hma.threshold_reach.size(); i++)
    {
        cout<<hma.threshold_reach[i].first<<":>"<<hma.threshold_reach[i].second<<"\n";
    }*/
    

    int next_domain = -1;
    req_page*  pg = NULL;

    
    /*for(pg = ptable; pg != NULL; pg = (struct req_page*)(pg->r_hh.next))
    {
        //pagenum = pg->pnum;
        //HASH_FIND(r_hh, req_pg_table, &pagenum, sizeof(int64_t), r);
        if(pg != NULL)
        {
            if(pg->hma_stat.onfly_detail_count.size() > 0)
            {
                next_domain = -1;
                cout<<"page detail vector: "<<pg->pnum<<": ";
                for(int i = 0; i < pg->hma_stat.onfly_detail_count.size(); i++)
                {  
                    if(pg->hma_stat.onfly_detail_count[i].second == 1)
                    {
                        hot_count++;
                        tot_l2m_access += pg->hma_stat.onfly_detail_count[i].first;
                        next_domain = 0;    // has been mig to l1m
                    }
                    else if(pg->hma_stat.onfly_detail_count[i].second == 0)
                    {
                        cold_count++;
                        tot_l1m_access += pg->hma_stat.onfly_detail_count[i].first;
                        next_domain = 1;    // has been mig to l2m
                    }
                    else
                    {
                        total_buffer_hits+=pg->hma_stat.onfly_detail_count[i].first;             
                        next_domain = -1;
                    }
                    cout<<pg->hma_stat.onfly_detail_count[i].first<<","<<pg->hma_stat.onfly_detail_count[i].second<<": ";
                }
                cout << pg->hma_stat.mig_count <<"\n";
                if(next_domain == 0)
                    tot_l1m_access += pg->hma_stat.mig_count;
                else if(next_domain == 1)
                    tot_l2m_access += pg->hma_stat.mig_count;
                
            }
        }

    }*/


    int64_t page_count = 0;
    int64_t total_unique = 0;
    cout<<"page_details_start:\n";
    for(pg = ptable; pg != NULL; pg = (struct req_page*)(pg->r_hh.next))
    {
        if(pg != NULL)
        {
            cout<<pg->hma_stat.total_count<<":";
            total_unique += pg->hma_stat.unique_addr.size();                                                                                         
            cout<<pg->hma_stat.unique_addr.size()<<"\n";
        }
        page_count++;
    }
    cout<<"Average unique addr access for each page:"<<total_unique/page_count<<"\n";
    cout<<"page_details_end:\n";
    cout << "Total no. of hot pages migrated " << hot_count << "\n";
    cout << "Total no. of cold pages migrated " << cold_count << "\n";
    cout << "Total Buffer hits "<< hma.onfly.total_buffer_hit_count<< "\n";
    cout << "Total l1m access "<< tot_l1m_access << " total l2m access " << tot_l2m_access << "\n";
    cout << "Migrated HBM accesses:"<<migrated_hbm_access<<"\n";
    cout<<"Access to migration/migrations (MBQ):"<<migrated_hbm_access/hma.stats.pages_migrated<<"\n";    
    cout<<"Ratio:\n";
    int64_t mbq_total = 0;
    for(int i=0; i<access_migration_ratio.size(); i++)
    {
        cout<<access_migration_ratio[i]<<" ";
        mbq_total += access_migration_ratio[i];
    }
    cout<<"\n";
    for(int i=0; i<window_mig_count.size(); i++)
        cout<<window_mig_count[i]<<" ";
    cout<<"\n";
    cout<<"average mbq/window: "<<mbq_total/access_migration_ratio.size()<<" average mig/window: "<<hma.stats.pages_migrated/access_migration_ratio.size()<<"\n";
    int64_t tremap_access = 0;
    for(int i=0; i<remap_access_ratio.size(); i++)
    {
        cout<<remap_access_ratio[i]<<" ";
        tremap_access += remap_access_ratio[i];
    }
    cout<<"average remap_mbq/window:"<<tremap_access/remap_access_ratio.size()<<"\n";
    cout<<"\n";
    return;
}

int Migration_controller:: onfly_mig_send_write()
{ 
    int64_t mig_req_page_addr, mig_req_phys_addr;
    static int hot_write_count = 0, cold_write_count = 0;
	pair<int64_t, int64_t> hot_cold;
	int req_category = 6;
	
    #ifdef MZ_DEBUG_ONFLY
        printf(" this is mig write part\n");
        cout<<"hot size "<<hot_buffer->bf_req.size()<<" cold size "<<cold_buffer->bf_req.size()<<" mig type "<<hma.onfly.mig_type<<" hot write count "<<hot_write_count<<"\n";
    #endif
    if( (hot_write_count < lines_in_page) && (hot_buffer->bf_req.size()>0) )  
    {
        //process hot q, send 1 request per cpu cycle
        mig_req_page_addr = hot_mig_stage_q.pg_backup.front();
        mig_req_phys_addr = cold_mig_stage_q.fr_backup.front();
	
        // make the req and send the req
        Request mig_req((long)mig_req_page_addr, Request::Type::WRITE, req_category, mig_callback, hma.onfly.hot_pg_core_id); 
        mig_req.phys_addr = mig_req_phys_addr;  //  this is very important since send() now relies on req.phys_addr, not req.addr
        mig_req.req_target_mem = 0;
        // hot_mig_stage_q always should hold addr of hot pages which must go to PCM
        // so, sending the mig_req to l2 without further check
        if(send_memory(mig_req)) // if the mig_req inserted successfully in the mig_q
        {
            hot_mig_stage_q.pg_backup.pop_front();
            /*#ifdef MZ_DEBUG_ONFLY
                printf(" position\n");
                fflush(0);
            #endif*/
            cold_mig_stage_q.fr_backup.pop_front();
            /*#ifdef MZ_DEBUG_ONFLY
                printf(" position1\n");
                fflush(0);
            #endif*/
			
            for(int i = 0; i < hot_buffer->bf_req.size(); i++)
            {
				if(hot_buffer->bf_req[i].first == mig_req_page_addr)
                {
                    hot_buffer->bf_req[i].second = 2;                   //written to memory     
                    break;
                }
            }
            hot_write_count++;    //should be 32 when all requests are sent out
            return 0;   // optimization: when successful return 0, so that in this cpu cycle processor do on-demand requests
        }
        #ifdef MZ_DEBUG_ONFLY
            printf("send write mig req failed to memory\n");
        #endif

        return 0;  // let the processor process some on-demand req in this cycle
    }
    else if( (cold_buffer->bf_req.size()>0) && (cold_write_count < lines_in_page) )  
    {
        //process hot q, send 1 request per cpu cycle
        mig_req_page_addr = cold_mig_stage_q.pg_backup.front();
        mig_req_phys_addr = hot_mig_stage_q.fr_backup.front();
	
        // make the req and send the req
        Request mig_req((long)mig_req_page_addr, Request::Type::WRITE, req_category,  mig_callback, hma.onfly.cold_pg_core_id); 
        mig_req.phys_addr = mig_req_phys_addr;  //  this is very important since send() now relies on req.phys_addr, not req.addr
        mig_req.req_target_mem = 1;
        // hot_mig_stage_q always should hold addr of hot pages which must go to PCM
        // so, sending the mig_req to l2 without further check
        if(send_memory_l2(mig_req)) // if the mig_req inserted successfully in the mig_q
        {
            cold_mig_stage_q.pg_backup.pop_front();
            hot_mig_stage_q.fr_backup.pop_front();
            for(int i = 0;i < cold_buffer->bf_req.size();i++)
            {
                if(cold_buffer->bf_req[i].first == mig_req_page_addr)
                {
                    cold_buffer->bf_req[i].second = 2;                   //written to memory     
                    break;
                }
            }
            cold_write_count++;    //should be 32 when all requests are sent out
            return 0;   // optimization: when successful return 0, so that in this cpu cycle processor do on-demand requests
        }
        #ifdef MZ_DEBUG_ONFLY
            printf("send write mig req failed to memory\n");
        #endif

        return 0;  // let the processor process some on-demand req in this cycle
    }
	//above we are creating new right requests but how do you acknowledge that these are successfully sent, if needed add logic to check i.mig_receive	
	//also modifying existing details of page's frame address
    else if( (( (hma.onfly.mig_type == 2) && (hot_write_count == lines_in_page) ) && (cold_write_count == lines_in_page))  || ( ( hma.onfly.mig_type == 1 )&&( hot_write_count == lines_in_page )) || ( (hma.onfly.mig_type == 1) && (hma.onfly.reverse_migration == 1) && (cold_write_count == lines_in_page) ) )
    {
        #ifdef MZ_DEBUG_ONFLY
            cout<<"mig buffer hits "<<total_mig_buffer_hits<<"\n";
        #endif
        hma.onfly.mig_status = 5;
        hot_write_count = 0;        //resetting hot write counts
        cold_write_count = 0;       //resetting cold write counts
        //exit(0);
    }

  return 0;
}

void Migration_controller::onfly_mig_completion()
{

    int64_t hot_pg_num = hma.onfly.hot_pg_num;
    int64_t cold_pg_num = hma.onfly.cold_pg_num;
    pair<int64_t, int64_t> hot_cold;
    struct req_page *p,*q;
    int64_t  hot_page, cold_page;

    hot_page = hma.onfly.hot_pg_num;   
    HASH_FIND(r_hh, ptable, &hot_page, sizeof(int64_t), p);
    cold_page = hma.onfly.cold_pg_num; 
    HASH_FIND(r_hh, ptable, &cold_page, sizeof(int64_t), q);

    if(hma.onfly.reverse_migration == 1)
    {
        if(hma.onfly.mig_type == 1)
        {
            q->hma_stat.migration_ping++;
            q->hma_stat.already_migrated = 0;
            q->prev_frame_no = q->frame_addr;
            q->hma_stat.onfly_detail_count.push_back(make_pair(q->hma_stat.mig_count,q->dom)); 
            if(q->hma_stat.migrated_location == 1)
                q->hma_stat.migrated_location = 0;
            else if(q->hma_stat.migrated_location == 0)
                q->hma_stat.migrated_location = 1;             
            
            q->dom = 1;
            q->frame_addr = hma.onfly.hot_frame_addr;
            //hma.stats.pages_migrated += 1;
            hma.stats.reverse_migration += 1;
        }
        else if(hma.onfly.mig_type == 2)
        {
            p->hma_stat.migration_ping++;
            q->hma_stat.migration_ping++;
            p->hma_stat.already_migrated = 0;
            q->hma_stat.already_migrated = 0;
            p->prev_frame_no = p->frame_addr;
            q->prev_frame_no = q->frame_addr;
            p->hma_stat.onfly_detail_count.push_back(make_pair(p->hma_stat.mig_count,p->dom)); 
            q->hma_stat.onfly_detail_count.push_back(make_pair(q->hma_stat.mig_count,q->dom)); 
            if(p->hma_stat.migrated_location == 1)
                p->hma_stat.migrated_location = 0;
            else if(p->hma_stat.migrated_location == 0)
                p->hma_stat.migrated_location = 1;             
            
            if(q->hma_stat.migrated_location == 1)
                q->hma_stat.migrated_location = 0;
            else if(q->hma_stat.migrated_location == 0)
                q->hma_stat.migrated_location = 1;             
            
            p->dom = 0;
            p->frame_addr = hma.onfly.cold_frame_addr;
            q->dom = 1;
            q->frame_addr = hma.onfly.hot_frame_addr;
            hma_add_page_lru(&hma, p , mig_clk);
            //hma.stats.pages_migrated += 2;
            hma.stats.reverse_migration += 2;
            
        }
    	hma.onfly.hot_partial_count = 0;
        hot_buffer_hits_current_mig = 0;
	    hma.onfly.cold_partial_count = 0;
        cold_buffer_hits_current_mig = 0;

    }
    else if(q->dom == 1 && hma.onfly.reverse_migration == 1)
    {
        cout<<" Reverse migration, q is 0\n";
        exit(0);
    }
    

    else if(p->dom != 0 && hma.onfly.reverse_migration == 0) // which means l2m
    {
        // put flushing here 

        // move it to mem0, replace with lru if neccessary 
        if(hma.onfly.mig_type == 1)     // there are free frames in l1m
        {
            if(hot_buffer_hits_current_mig > 0)
                p->hma_stat.onfly_detail_count.push_back(make_pair(hot_buffer_hits_current_mig,2));

    	    if(p->hma_stat.mig_count > hma.onfly.hot_partial_count)
	        	p->hma_stat.mig_count = p->hma_stat.mig_count - hma.onfly.hot_partial_count; 

            p->hma_stat.onfly_detail_count.push_back(make_pair(p->hma_stat.mig_count,p->dom)); 
            p->prev_frame_no = p->frame_addr;       //copying the prev addr. to use in rev mig   
            p->dom = 0;
            p->hma_stat.mig_count = hma.onfly.hot_partial_count;
            p->hma_stat.mig_times_count++;
            p->frame_addr = hma.onfly.cold_frame_addr;
            if(p->hma_stat.migrated_location == 1)
                p->hma_stat.migrated_location = 0;
            else if(p->hma_stat.migrated_location == 0)
                p->hma_stat.migrated_location = 1;             

            hma.stats.pages_migrated += 1;    
            adaptive_mig_count += 1; 
            hma.stats.pages_migrated_temp += 1;    
            hma_inc_npt(p->dom, 0);
        }
        else if(hma.onfly.mig_type == 2)   // we need to swap between l1m and l2m
        {
            if(q->dom != 0)                         //checks the cold page is from HBM or not
            {
                printf(" q->domain == 1 , it is incorrect");
                exit(0);
            }
            if(hot_buffer_hits_current_mig > 0)
                p->hma_stat.onfly_detail_count.push_back(make_pair(hot_buffer_hits_current_mig,2)); 

            if(cold_buffer_hits_current_mig > 0)
                q->hma_stat.onfly_detail_count.push_back(make_pair(cold_buffer_hits_current_mig,3)); 

            if(p->hma_stat.mig_count > hma.onfly.hot_partial_count)
                p->hma_stat.mig_count = p->hma_stat.mig_count - hma.onfly.hot_partial_count; 

            if(q->hma_stat.mig_count > hma.onfly.cold_partial_count)
                q->hma_stat.mig_count = q->hma_stat.mig_count - hma.onfly.cold_partial_count; 

            p->hma_stat.onfly_detail_count.push_back(make_pair(p->hma_stat.mig_count,p->dom)); 
            q->hma_stat.onfly_detail_count.push_back(make_pair(q->hma_stat.mig_count,q->dom)); 

            //cout<<"p, this is new page in hbm."<<p->pnum<<"\n";

            p->prev_frame_no = p->frame_addr;       //since these will be replaced in next lines
            q->prev_frame_no = q->frame_addr;
            p->dom = 0;
            p->frame_addr = hma.onfly.cold_frame_addr;
            q->dom = 1;
            q->frame_addr = hma.onfly.hot_frame_addr;
            p->hma_stat.mig_count=hma.onfly.hot_partial_count;
            q->hma_stat.mig_count=hma.onfly.cold_partial_count;
            p->hma_stat.mig_times_count++;
            q->hma_stat.mig_times_count++;
            
            if(p->hma_stat.migrated_location == 1)
                p->hma_stat.migrated_location = 0;
            else if(p->hma_stat.migrated_location == 0)
                p->hma_stat.migrated_location = 1;             
            
            if(q->hma_stat.migrated_location == 1)
                q->hma_stat.migrated_location = 0;
            else if(q->hma_stat.migrated_location == 0)
                q->hma_stat.migrated_location = 1;             

            hma.stats.pages_migrated += 2;
            adaptive_mig_count += 2; 
            hma.stats.pages_migrated_temp += 2;    
            hma_inc_npt(0, p->dom);
            hma_inc_npt(p->dom, 0);
            
        }
        hma_add_page_lru(&hma, p , mig_clk);
    	hma.onfly.hot_partial_count = 0;
        hot_buffer_hits_current_mig = 0;
	    hma.onfly.cold_partial_count = 0;
        cold_buffer_hits_current_mig = 0;
    }
    else if(p->dom == 0 && hma.onfly.reverse_migration == 0) // which means l2m
    {
        printf(" regular migartion, p->domain == 0 , it is incorrect\n");
        exit(0);
    }
    
    //also remap table
    if(hma.onfly.reverse_migration == 0)
    {
        hma.threshold_reach.push_back(make_pair(hot_pg_num, 0));
        for(int i=0; i<remap_table.size(); i++)    
        {
            if(remap_table[i].first.first == hot_pg_num  ||  remap_table[i].first.first == cold_pg_num)
            {
                remap_table[i].second.first = 0;			//once the requests are sent, make the onfly status in remap table as 0;
                if(remap_table[i].first.first == hot_pg_num )
                {
                    remap_table[i].first.second = p->frame_addr;		//modifying the new frame addresses
                    //p->hma_stat.already_migrated++;   
                }
                else if(remap_table[i].first.first == cold_pg_num )
                {
                    remap_table[i].first.second = q->frame_addr;
                    //q->hma_stat.already_migrated++;
                }
            }
        }
    }

    //erasing the entries from remap table once the reverse migration is done
    if(hma.onfly.reverse_migration == 1)
    {
        hma.threshold_reach.push_back(make_pair(hot_pg_num, 1));
        hma.threshold_reach.push_back(make_pair(cold_pg_num, 1));
        //if(hma.onfly.mig_type == 2)
        for(int i=0; i<remap_table.size(); i++)
        {
            if(remap_table[i].first.first == hot_pg_num)
                remap_table.erase(remap_table.begin()+i);
        }
        for(int i=0; i<remap_table.size(); i++)
        {
            if(remap_table[i].first.first == cold_pg_num)
                remap_table.erase(remap_table.begin()+i);
        }
    }

    hma.onfly.reverse_migration = 0;
    hot_buffer->bf_req.clear();                   //clearing buffer     
    cold_buffer->bf_req.clear();                   //clearing buffer   
    total_mig_cycle += (mig_clk - mig_start_clk);
    //adaptive_mig_cycle += (mig_clk - mig_start_clk);
    //adaptive_mig_count++; 

	//printf("Clk %ld:: Mig completed for hot pg %ld, hot fr %ld, cold pg %ld, cold fr %ld\n", mig_clk, hma.onfly.hot_pg_num, hma.onfly.hot_frame_addr, hma.onfly.cold_pg_num, hma.onfly.cold_frame_addr);

	/*cout <<"Migration completed: " << (mig_clk - mig_start_clk) << "\n";
	cout << "Total migrations " << hma.stats.pages_migrated << "\n";
	cout << "Mig write done at CLK: " << mig_clk << ", Total cpu clk cycle taken: "<< (mig_clk - mig_start_clk) << "\n\n";*/
    // clear the hma.onfly hot/cold info
    hma.onfly.hot_pg_num = -1;
    hma.onfly.hot_frame_addr = -1;
    hma.onfly.hot_pg_core_id = -1;
    hma.onfly.cold_pg_num = -1;
    hma.onfly.cold_frame_addr = -1;
    hma.onfly.cold_pg_core_id = -1;

    hma.onfly.mig_status = 0;


    #ifdef MZ_DEBUG_ONFLY
        cout <<"Migration completed: " << (mig_clk - mig_start_clk) << "\n";
        cout << "Mig write done at CLK: " << mig_clk << ", Total cpu clk cycle taken: "<< (mig_clk - mig_start_clk) << "\n\n";
        //exit(0);
    #endif
}

int Migration_controller::onfly_ar_init()
{
    //check rempa table size
    if(remap_table.size() > (int)(REMAP_TABLE_MAX_SIZE * 1/2) ) {	// when 50% full
        //cout <<"Mig clk" << mig_clk << ", AR phase starting "<< "\n";
        for(int i = 0; i< remap_table.size(); i++) {
            if( (remap_table[i].first.first == hma.onfly.hot_pg_num) ||  (remap_table[i].first.first == hma.onfly.cold_pg_num))
                continue;
            else {  // found a candidate page
                int64_t pagenum = remap_table[i].first.first;
                struct req_page *p;
                HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), p);

                if(p == NULL) {
                    cout <<"Remap table page not found!\n";
                    exit(0);
                }
                // we have to check the pair for current candidate
                if(p->onfly_pair_pnum != -1) {
                    if( (p->onfly_pair_pnum == hma.onfly.hot_pg_num) ||  (p->onfly_pair_pnum == hma.onfly.cold_pg_num))
                        continue;
                    else {  // we have found two pages for ar
                        struct req_page *q;
                        int64_t q_pagenum = p->onfly_pair_pnum;
                        HASH_FIND(r_hh, ptable, &q_pagenum, sizeof(int64_t), q);

                        if(q == NULL) {
                            cout <<"Remap table pair page not found!\n";
                            exit(0);
                        }

                        //init two ar page info in hma 
                        hma.onfly.ar_page_1 = p->pnum;
                        hma.onfly.ar_page_2 = p->onfly_pair_pnum;
                        ar_page_1_core_id = p->th_id;
                        ar_page_2_core_id = q->th_id;
                        hma.onfly.ar_status = 21;
                        ar_start_clk = mig_clk;
                        ar_next_free_clk = mig_clk + ar_stage_21_bz_cycle;
                        ar_bz = 1;
                        ar_wait = 0;
                        //cout <<"Mig clk" << mig_clk << ", two ar pagses found: " << hma.onfly.ar_page_1 << " "<< hma.onfly.ar_page_2 << "\n";
                        return 1;
                    }
                }
                else {  // we found a one way page for ar, it does not have any pair
                    //init one ar page info in hma 
                    hma.onfly.ar_page_1 = p->pnum;
                    ar_page_1_core_id = p->th_id;
                    ar_page_2_core_id = -1;
                    hma.onfly.ar_status = 11;
                    ar_start_clk = mig_clk;
                    ar_next_free_clk = mig_clk + ar_stage_11_bz_cycle;
                    ar_bz = 1;
                    ar_wait = 0;
                    //cout <<"Mig clk" << mig_clk << ", one ar pagse found: " << hma.onfly.ar_page_1 << "\n";
                    return 1;
                }
            } //else 
        } // for
    }   //if remap table size
    return 0;
}

int Migration_controller::reverse_migration()
{
//check for remap table size
    if(remap_table.size() > (int)(REMAP_TABLE_MAX_SIZE * 1/2) ) 	// when 50% full
    {
        //if(!hma.onfly.mig_status)
        //{
            int64_t selected_page = -1;
            long min_clock;
            vector<pair<long, int64_t>> temp_clock_page;
            for(int i=0; i<remap_table.size(); i++)
            {
                long temp_clk = hma_search_lru_page(&hma, remap_table[i].first.first); 
                if(temp_clk != -1)
                {
                    temp_clock_page.push_back(make_pair(temp_clk, remap_table[i].first.first));
                }
            }

            //initialize min clock
            if(temp_clock_page.size()>0)
            {
                min_clock = temp_clock_page[0].first;
                selected_page = temp_clock_page[0].second;
            }
            for(int i=0; i<temp_clock_page.size(); i++)
            {
                if(temp_clock_page[i].first < min_clock)
                {
                    selected_page = temp_clock_page[i].second;
                    min_clock = temp_clock_page[i].first;
                }
            }

            for(int i=0; i<remap_table.size(); i++)
            {
                if(remap_table[i].first.first == hma.onfly.hot_pg_num && hma.onfly.reverse_migration == 1)
                {
                    hma.onfly.hot_frame_addr = remap_table[i].first.second;
                    remap_table[i].second.first = 1;
                    return 203;
                }
                
                if(remap_table[i].second.first == 0 && remap_table[i].first.first == selected_page)    //onfly status should be 0.
                {
                    //this part should be replaced with LRU hot and cold pages
                    hma.onfly.cold_pg_num = remap_table[i].first.first;
                    hma.onfly.cold_frame_addr = remap_table[i].first.second;
                    int64_t hpage = remap_table[i].first.first;
                    struct req_page *p;
                    HASH_FIND(r_hh, ptable, &hpage, sizeof(int64_t), p);
                     
                    if(p == NULL)
                    {
                        cout<<"Page not found\n";
                        exit(0);
                    }            
                    
                    
                    if(p->onfly_pair_pnum == -1)
                    {
                        //continue;
                        hma.onfly.hot_frame_addr = p->prev_frame_no;
                        hma.onfly.mig_type = 1;
                        remap_table[i].second.first = 1;
                        hma.onfly.reverse_migration = 1;
                        bool rm_res = remove_node(&hma, selected_page);
                        if(!rm_res)
                        {
                            cout<<"remove node failed\n";
                            exit(0);
                        }
                        return 203;
                    }
                    else if(p->dom == 0 && p->onfly_pair_pnum != -1)
                    {
                        hma.onfly.hot_pg_num = p->onfly_pair_pnum;
                        //check window
                        hma.onfly.mig_type = 2;
                        remap_table[i].second.first = 1;
                        hma.onfly.reverse_migration = 1;
                        bool rm_res = remove_node(&hma, selected_page);
                        if(!rm_res)
                        {
                            cout<<"remove node failed\n";
                            exit(0);
                        }
                        continue;
                    }
                    else
                        continue;            
                }
                

            }
       // }
    }   //if(remap_table.size() > 512)

    return 0;

//first check if any migration going on

//if no, start reverse by assigning repective hot and cold pages(select LRU hot and cold pages from remap table)
//{
//  then check if pending requests exists in window to these pages
//  {
//      not exists
//      {
//         initiate migration, put least recently used page of HBM as cold and its pair as hot and set status to 203 and return 
//      }
//      exists
//      {
//          wait until it clears and put reverse migration as highest priority
//      }
//      
//  }
//}
//else, wait for the migration to complete and put reverse migration highest priority


}

void Migration_controller::onfly_page_mig_initial_overhead(int status)
{
	switch(status)
	{
		case 201:
			migp_next_free_clk = mig_clk + mig_stage_201_bz_cycle;
			migp_bz = 1;
			migp_wait = 0;
			break;
		case 202:
			migp_next_free_clk = mig_clk + mig_stage_202_bz_cycle;
			migp_bz = 1;
			migp_wait = 0;
			break;
		case 203:
			migp_next_free_clk = mig_clk + mig_stage_203_bz_cycle;
			//printf("Clk; %ld, inside switch, next free clk is %ld\n", mig_clk, migp_next_free_clk);
			migp_bz = 1;
			migp_wait = 0;
			break;
	}
	
}	

//MAIN
void Migration_controller::hybrid_tick_hma_onfly()
{
    ++mig_clk;
    static long start_complete_time = 0;
    static short core_info_sent = 0;
    static int bz_process_flag = 0;
    static int64_t current_cache_line_ar1 = 0;
    static int64_t current_cache_line_ar2 = 0;
	static int set_initial_overhead = 0;

    adaptive_mig_cycle++;
    adaptive_mig_cycle_recheck++;

    //cout<< "Mig clk: "<< mig_clk << " remap_table size: "<< remap_table.size()<<" ms:"<<hma.onfly.mig_status<<" rev_mig?:"<<hma.onfly.reverse_migration<<" hot:"<<hma.onfly.hot_pg_num<<" cold:"<<hma.onfly.cold_pg_num<<" mig_type:"<<hma.onfly.mig_type<<"\n";    
    /*for(int i=0;i<remap_table.size();i++)
    {   
        cout<<"remap table "<<remap_table[i].first.first<<" "<<remap_table[i].first.second<<" "<<remap_table[i].second.second<<"\n";
    }*/
    //if (mig_clk % 50000 == 0 )
    /*if (mig_clk % 50000 == 0 ) {
            cout<< "Mig clk: "<< mig_clk << " remap_table size: "<< remap_table.size()<<"\n";*/    
            /*for(int i=0;i<remap_table.size();i++)
            {   
                cout<<"remap table "<<remap_table[i].first.first<<" "<<remap_table[i].first.second<<" "<<remap_table[i].second<<"\n";
            }*/
    //}

    #ifdef MZ_DEBUG_ONFLY
        cout << "Mig CLK: " << mig_clk << " mig_Status "<< hma.onfly.mig_status <<" mig_type "<< hma.onfly.mig_type<<"\n";
    #endif

    if(ar_bz) {
        if( (hma.onfly.ar_status == 11) || (hma.onfly.ar_status == 21) ) {  // send/recv with OS for VA list
            if(mig_clk >= ar_next_free_clk) {
                ar_bz = 0;
                ar_wait = 1;
                ar_next_free_clk = mig_clk + ( (hma.onfly.ar_status == 11) ? ar_stage_11_wait_cycle : ar_stage_21_wait_cycle) ;
                //cout << "Status: " << hma.onfly.ar_status << ", completed bz cycles at: " << mig_clk << "\n";
				if(hma.onfly.ar_status == 11) {
					Core* m_core = mig_cores[ar_page_1_core_id].get();
					m_core->os_halt_cycle = ar_stage_11_wait_cycle;	//For eachpage x cycles 
				}
				if(hma.onfly.ar_status == 21) {
					if (ar_page_1_core_id == ar_page_2_core_id) {
						Core* m_core = mig_cores[ar_page_1_core_id].get();
						m_core->os_halt_cycle = ar_stage_21_wait_cycle;	//For eachpage x cycle, for two pages 2x cycles 
					}
					else {
						Core* m_core_1 = mig_cores[ar_page_1_core_id].get();
						m_core_1->os_halt_cycle = ar_stage_11_wait_cycle;	//For eachpage x cycles  
						Core* m_core_2 = mig_cores[ar_page_2_core_id].get();
						m_core_2->os_halt_cycle = ar_stage_11_wait_cycle;	//For eachpage x cycles 
					}
				}
            }
            else return;
        }   //if 11, 21
        else if( (hma.onfly.ar_status == 12) || (hma.onfly.ar_status == 22) ) { // send/recv with DiDi. DiDi stops cores.
            if(mig_clk >= ar_next_free_clk) {
                ar_bz = 0;
                ar_wait = 1;
                bz_process_flag = 0; //resetting flag
                ar_next_free_clk = mig_clk + ( (hma.onfly.ar_status == 12) ? ar_stage_12_wait_cycle : ar_stage_22_wait_cycle) ;
                //cout << "Status: " << hma.onfly.ar_status << ", completed bz cycles at: " << mig_clk << "\n";
            }
            else {  // bz cycles. send/recv with DiDi. DiDi stops cores.
                if(!bz_process_flag) {
                    //send core interrupt
                    if (hma.onfly.ar_page_1 != -1) {
                        Core* m_core = mig_cores[ar_page_1_core_id].get();
                        m_core->ar_flag = 1;
                        bz_process_flag = 1;
                        //printf("halt sent to coreid %d\n", ar_page_1_core_id);
                    }
                    if (hma.onfly.ar_page_2 != -1) {
                        Core* m_core = mig_cores[ar_page_2_core_id].get();
                        m_core->ar_flag = 1;
                        bz_process_flag = 1;
                        //printf("halt sent to coreid %d\n", ar_page_2_core_id);
                    }
                    return; // as it is bz cycle
                }
                else return; // as it is bz cycle
            }
        }   // else if 12, 22
        else if( (hma.onfly.ar_status == 13) || (hma.onfly.ar_status == 23) ) { // send cache flushing to LLC and recv ack.
            if(mig_clk >= ar_next_free_clk) {
                ar_bz = 0;
                ar_wait = 1;
                bz_process_flag = 0; //resetting flag
                current_cache_line_ar1 = 0; //resetting cache lines
                current_cache_line_ar2 = 0;
                ar_next_free_clk = mig_clk + ( (hma.onfly.ar_status == 13) ? ar_stage_13_wait_cycle : ar_stage_23_wait_cycle) ;
                //cout << "Status: " << hma.onfly.ar_status << ", completed bz cycles at: " << mig_clk << "\n";
            }
            else {  // bz cycle. send cache flushing to LLC
                if (hma.onfly.ar_status == 13) {    // send INV for one page
                    if(bz_process_flag < lines_in_page) {   // when x lines are sent it will be false
                        //printf("Mig clk: %ld sending INV for Pg1, line no. %d, addr: %lx\n", mig_clk, bz_process_flag, current_cache_line_ar1);
                        llc->send_invalidate(current_cache_line_ar1);
                        current_cache_line_ar1 += 64;
                        bz_process_flag++;
                        return;
                    }
                    else return; // by this time mig_clk should be more than next free clk
                }
                else if (hma.onfly.ar_status == 23) {    // Need to INV for both pages
                    if(bz_process_flag < lines_in_page) {   // First send 32 INV for ar_page_1
                        //printf("Mig clk: %ld sending INV for Pg1, line no. %d, addr: %lx\n", mig_clk, bz_process_flag, current_cache_line_ar1);
                        llc->send_invalidate(current_cache_line_ar1);
                        current_cache_line_ar1 += 64;
                        bz_process_flag++;
                        return;
                    }
                    else if(bz_process_flag < (2*lines_in_page) ) {   // then send 32 INV for ar_page_2
                        //printf("Mig clk: %ld sending INV for Pg2, line no. %d, addr: %lx\n", mig_clk, bz_process_flag, current_cache_line_ar1);
                        llc->send_invalidate(current_cache_line_ar2);
                        current_cache_line_ar2 += 64;
                        bz_process_flag++;
                        return;
                    }
                    else return; // by this time mig_clk should be more than next free clk
                }
            }   // else bz
        }   // else if 12, 22
        else if( (hma.onfly.ar_status == 14) || (hma.onfly.ar_status == 24) ) {  // send/recv with OS for completion of ar
            if(mig_clk >= ar_next_free_clk) {
                ar_bz = 0;
                ar_wait = 1;   
                ar_next_free_clk = mig_clk + ( (hma.onfly.ar_status == 14) ? ar_stage_14_wait_cycle : ar_stage_24_wait_cycle) ;
                //cout << "Status: " << hma.onfly.ar_status << ", completed bz cycles at: " << mig_clk << "\n";

				// Now we also make impacted cores to halt 150 cycles assuming their TLBs are being updated
				// irrespective of whether or not they have a TLB miss on AR pages.
				// Since we do not explicitely model TLB, it will take care of additional TLB invalidation cost due to AR.
				ar_next_half_way_wait_clk = 0;
				if (hma.onfly.ar_status == 14) {
					Core* m_core = mig_cores[ar_page_1_core_id].get();
					m_core->core_halt = 1;	// ar_flag was already set now we are explicitely setting core_halt
					//cout << mig_clk << " halt_150 core with id " <<ar_page_1_core_id<< "\n";
					
				}
				else if( (hma.onfly.ar_status == 24) && (ar_page_1_core_id ==  ar_page_2_core_id) ) {	// same core accesses ar page pair, should wait 300 cycles
					Core* m_core = mig_cores[ar_page_1_core_id].get();
					m_core->core_halt = 1;	// ar_flag was already set now we are explicitely setting core_halt
					//cout << mig_clk << " halt_300 core with id " <<ar_page_1_core_id<<"\n";
				}
				else {
					ar_next_half_way_wait_clk = mig_clk + ar_stage_14_wait_cycle; //incase of two way ar, each of the different cores will wait 150 cycles
					//cout << "halfway: " << ar_next_half_way_wait_clk << " fullway: " << ar_next_free_clk << "\n";
				}
            }
            else return;
        }   //if 11, 21
    } // if ar_bz
       

    
    if(ar_wait) {
        if( (hma.onfly.ar_status == 11) || (hma.onfly.ar_status == 21) ) {
            if(mig_clk >= ar_next_free_clk) {
                ar_bz = 1;
                ar_wait = 0;
                //cout << "Status: " << hma.onfly.ar_status << ", completed wait cycles at: " << mig_clk << "\n";
                if(hma.onfly.ar_status == 11) {
                    hma.onfly.ar_status = 12;
                    ar_next_free_clk = mig_clk + ar_stage_12_bz_cycle ;
                }
                else {
                    hma.onfly.ar_status = 22;
                    ar_next_free_clk = mig_clk + ar_stage_22_bz_cycle ;
                }
                // Clear core halt flag
	            //for (unsigned int i = 0 ; i < mig_cores.size() ; ++i) { // rest all cores' ar_flag
                  //  Core* m_core = mig_cores[i].get();
				  //  m_core->os_halt_cycle = 0;
                //}
				if(ar_page_1_core_id != -1) {
                    Core* m_core = mig_cores[ar_page_1_core_id].get();
					m_core->os_halt_cycle = 0;
				}
				if(ar_page_2_core_id != -1) {
                    Core* m_core = mig_cores[ar_page_2_core_id].get();
					m_core->os_halt_cycle = 0;
				}
            }
            //else return;
        }   //if 11, 21
        else if( (hma.onfly.ar_status == 12) || (hma.onfly.ar_status == 22) ) {
            if(mig_clk >= ar_next_free_clk) {
                ar_bz = 1;
                ar_wait = 0;
                //cout << "Status: " << hma.onfly.ar_status << ", completed wait cycles at: " << mig_clk << "\n";
                if(hma.onfly.ar_status == 12) { // init next stage's bz params
                    hma.onfly.ar_status = 13;
                    current_cache_line_ar1 = hma.onfly.ar_page_1; // this is page no., does not include last 11 bits 
                    current_cache_line_ar1 = current_cache_line_ar1 << memory_page_info.first; //now this is byte level addr
                    ar_next_free_clk = mig_clk + ar_stage_13_bz_cycle ;
                }
                else {  // init next stage's bz params
                    hma.onfly.ar_status = 23;
                    current_cache_line_ar1 = hma.onfly.ar_page_1; // this is page no., does not include last 11 bits 
                    current_cache_line_ar1 = current_cache_line_ar1 << memory_page_info.first; //now this is byte level addr
                    current_cache_line_ar2 = hma.onfly.ar_page_2; // this is page no., does not include last 11 bits 
                    current_cache_line_ar2 = current_cache_line_ar2 << memory_page_info.first; //now this is byte level addr
                    ar_next_free_clk = mig_clk + ar_stage_23_bz_cycle ;
                }
            }
            //else return;
        }   // else if 12, 22
        else if( (hma.onfly.ar_status == 13) || (hma.onfly.ar_status == 23) ) {
            if(mig_clk >= ar_next_free_clk) {
                ar_bz = 1;
                ar_wait = 0;
                //cout << "Status: " << hma.onfly.ar_status << ", completed wait cycles at: " << mig_clk << "\n";
                if(hma.onfly.ar_status == 13) {
                    hma.onfly.ar_status = 14;
                    ar_next_free_clk = mig_clk + ar_stage_14_bz_cycle ;
                }
                else {
                    hma.onfly.ar_status = 24;
                    ar_next_free_clk = mig_clk + ar_stage_24_bz_cycle ;
                }
            }
            //else return;
        }   // else if 13, 23
        else if( (hma.onfly.ar_status == 14) || (hma.onfly.ar_status == 24) ) {
            if(mig_clk >= ar_next_free_clk) {
                //cout << "AR completed:: Status: " << hma.onfly.ar_status << ", completed wait cycles at: " << mig_clk << "\n"; 
                //update ar stats:
                total_ar_cycle += (mig_clk - ar_start_clk);
                total_no_of_ar_process++;

                if (hma.onfly.ar_status == 14)  {
                    hma.stats.pages_addr_recon = hma.stats.pages_addr_recon + 1;  
                	total_ar_cycle_ar1 += (mig_clk - ar_start_clk);
					total_no_of_ar1++;
                    // Delete from Remap table
                    for(int i = 0; i< remap_table.size(); i++) {
                        if( remap_table[i].first.first == hma.onfly.ar_page_1 ) {
                            int64_t pagenum = remap_table[i].first.first;
                            struct req_page *p;
                            HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), p);
                            if(p->onfly_pair_pnum != -1) {
                                cout<< "Problem found on one way completion!\n";
                                exit(0);
                            }
							p->hma_stat.already_migrated = 0;
                            remap_table.erase(remap_table.begin() + i);
							break;
                        }   
                    }   //for
                }   //if 14
                else    { 
                    hma.stats.pages_addr_recon = hma.stats.pages_addr_recon + 2;
                	total_ar_cycle_ar2 += (mig_clk - ar_start_clk);
					total_no_of_ar2 += 2;
                    // Delete from Remap table
                    for(int i = 0; i< remap_table.size(); i++) {
                        if( remap_table[i].first.first == hma.onfly.ar_page_1 ) {
                            int64_t pagenum = remap_table[i].first.first;
                            struct req_page *p;
                            HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), p);
                            if(p== NULL) {
                                cout<< "Problem found on two way completion!\n";
                                exit(0);
                            }
                            if(p->onfly_pair_pnum != hma.onfly.ar_page_2) {
                                cout<< "Problem found on two way completion!\n";
                                exit(0);
                            }
                            p->onfly_pair_pnum = -1;
							p->hma_stat.already_migrated = 0;
                            remap_table.erase(remap_table.begin() + i);
							break;
                        }   
                    }   //1st page for
                    for(int i = 0; i< remap_table.size(); i++) {
                        if( remap_table[i].first.first == hma.onfly.ar_page_2 ) {
                            int64_t pagenum = remap_table[i].first.first;
                            struct req_page *p;
                            HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), p);
                            if(p== NULL) {
                                cout<< "Problem found on two way completion!\n";
                                exit(0);
                            }
                            if(p->onfly_pair_pnum != hma.onfly.ar_page_1) {
                                cout<< "Problem found on two way completion!\n";
                                exit(0);
                            }
                            p->onfly_pair_pnum = -1;
							p->hma_stat.already_migrated = 0;
                            remap_table.erase(remap_table.begin() + i);
							break;
                        }   
                    }   //2nd page for
                } //else 24

				hma.onfly.remap_table_size = remap_table.size();
                //cout << "AR done REMAP TABLE SIZE after deleting: " <<remap_table.size() << "hma remap table size " << hma.onfly.remap_table_size << "\n";
                // Clear core halt flag
				if(ar_page_1_core_id != -1) {
                    Core* m_core = mig_cores[ar_page_1_core_id].get();
                    m_core->ar_flag = 0;
                    m_core->core_halt = 0;
					m_core->os_halt_cycle = 0;
				}
				if(ar_page_2_core_id != -1) {
                    Core* m_core = mig_cores[ar_page_2_core_id].get();
                    m_core->ar_flag = 0;
                    m_core->core_halt = 0;
					m_core->os_halt_cycle = 0;
				}
                
                // Completed current ar process, reset all params
                hma.onfly.ar_page_1 = -1;
                hma.onfly.ar_page_2 = -1;
                hma.onfly.ar_status = 0;    
				//cout << "AR status " << hma.onfly.ar_status << " Mig status " << hma.onfly.mig_status << "\n";

                ar_page_1_core_id = -1;
                ar_page_2_core_id = -1;
                ar_start_clk = 0;
                ar_next_free_clk = 0;
				ar_next_half_way_wait_clk = 0;

                ar_bz = 0;
                ar_wait = 0;
                bz_process_flag = 0; 
                current_cache_line_ar1 = 0; 
                current_cache_line_ar2 = 0;
    
            }// wait time over
            else if( (ar_next_half_way_wait_clk) && (mig_clk >= ar_next_half_way_wait_clk)) {	// wee need to halt two cores each for 150 cycles
				if(ar_page_1_core_id != -1) {
					Core* m_core = mig_cores[ar_page_1_core_id].get();
					m_core->core_halt = 1;	// ar_flag was already set now we are explicitely setting core_halt
					//cout << mig_clk << " halt_150 core with id " <<ar_page_1_core_id<< "\n";
				}
				if(ar_page_2_core_id != -1) {
					Core* m_core = mig_cores[ar_page_2_core_id].get();
					m_core->core_halt = 1;	// ar_flag was already set now we are explicitely setting core_halt
					//cout << mig_clk << " halt_150 core with id " <<ar_page_2_core_id<< "\n";
				}
				ar_next_half_way_wait_clk = 0;	//this else if should fire only once
			}
            //else return;
        }   // else if 14, 24
        
    }   // if ar_wait
    


    // first check if ar is going or not, if not then check remap table size, should we init ar?
    if(!hma.onfly.ar_status) {
        if(onfly_ar_init())
            return;
    }// if no ar status
    

    //adaptive threshold
    if(!hma.onfly.mig_status)
    {
        //assuming avg mig takes around 1000 cycles for xalan
        if(adaptive_mig_cycle >= 4000000  && onfly_started == true)    //check for every 4 million cycles
        {
            if(hma.stats.pages_migrated > 0){
                access_migration_ratio.push_back(migrated_hbm_access/hma.stats.pages_migrated);
                int pair_count_val = remap_table[0].second.second;
                int hot_pages_in_remap = 1;
                for(int i=1; i<remap_table.size(); i++){
                    if(pair_count_val != remap_table[i].second.second)
                        hot_pages_in_remap++;
                    pair_count_val = remap_table[i].second.second;
                }
                //cout<<remap_access<<" "<<hot_pages_in_remap<<" "<<remap_table.size()<<"\n";
                remap_access_ratio.push_back(remap_access/hot_pages_in_remap);
            }    

            /*if(adaptive_mig_cycle_recheck >= 100000000 && !pause_migration && migrated_hbm_access/hma.stats.pages_migrated < 60)  //changed window //increase limit 
            {
                cout<<"mig_pause\n";
                pause_migration = true; //still paused
                adaptive_mig_cycle_recheck = 0; 
            }
            else if(adaptive_mig_cycle_recheck >= 100000000 && pause_migration && migrated_hbm_access/hma.stats.pages_migrated > 80) //changed window //increase limit
            {
                cout<<"mig_resume\n";
                pause_migration = false;
                adaptive_mig_cycle_recheck = 0; 
            }

            if(adaptive_mig_cycle_recheck >= 100000000 && !pause_migration)
            {
                float count_32=0, count_64=0, count_128=0, count_256=0;
                for(int i=0; i<threshold_history.size(); i++)
                {
                    switch(threshold_history[i])
                    {
                        case 32:
                            count_32++;
                            break;
                        case 64:
                            count_64++;
                            break;
                        case 128:
                            count_128++;
                            break;
                        case 256:
                            count_256++;
                            break;
                    }
                }
                if(count_64/count_128 >= 3){              //64 min
                    cout<<"mig_pause\n";
                    pause_migration = true;
                }
                adaptive_mig_cycle_recheck = 0;
            }

            
            //access_migration_ratio.push_back(migrated_hbm_access_temp/hma.stats.pages_migrated_temp);

            //checking migrations and changing threshold
            if(adaptive_mig_count > 240 && (migrated_hbm_access/hma.stats.pages_migrated < 100))       //60 mig per million;
            {
                if(hma.config.thold < 256)      //limiting until 256 (4 - 256)
                hma.config.thold = hma.config.thold * 2;
            }   //increase threshold
            else if(adaptive_mig_count < 160 && (migrated_hbm_access/hma.stats.pages_migrated < 70))       //40 per million; if less MBQ, migrate more to increase MBQ (biased towards friendly workloads)
            {    
                if(hma.config.thold > 32)      //min 64 //change to 16 //limiting to 4 (4 - 256)
                hma.config.thold = hma.config.thold / 2;
                //decrease threshold
            }*/
            cout<<"resetting, new thld:"<<hma.config.thold<<" MBQ:"<<migrated_hbm_access/hma.stats.pages_migrated<<" mig_count:"<<adaptive_mig_count<<"\n";
            adaptive_mig_cycle = 0;
            hma.stats.pages_migrated_temp = 0; 
            window_mig_count.push_back(adaptive_mig_count);   
            adaptive_mig_count = 0;
            threshold_history.push_back(hma.config.thold);
        }
    }

    //commenting reverse migration
    /*if(!hma.onfly.mig_status)
    {
        hma.onfly.mig_status = reverse_migration();
        //if(!hma.onfly.mig_status)
          //  return;
    }*/
 
    if(!hma.onfly.mig_status) {
        
        //before selecting hot and cold pages, checking if migration is paused (adaptive threshold)
       if(pause_migration)
            return;         //return if migration is paused for a while  

        hma.onfly.mig_status = hma_onfly_opt_hotcold_check_and_init(&hma, mig_clk);
        //if(hma.onfly.mig_status == 203)
        if(!hma.onfly.mig_status)
        {
            return;
        }
    }

	if(migp_bz)
	{
        if( (hma.onfly.mig_status == 201) || (hma.onfly.mig_status == 202) ) {  // send/recv with OS for VA list
            if(mig_clk >= migp_next_free_clk) {
                migp_bz = 0;
                migp_wait = 0;
				set_initial_overhead = 0;
				hma.onfly.mig_status = 0;
				return;
            }
            else return;
        }   // 201, 202
        else if(hma.onfly.mig_status == 203) {  // send/recv with OS for VA list
            if(mig_clk >= migp_next_free_clk) {
				//printf("Clk; %ld, Busy over for status 203\n", mig_clk);
                if(!onfly_started)
                    onfly_started = true;
                mig_start_clk = mig_clk;
                migp_bz = 0;
                migp_wait = 1;
                migp_next_free_clk = mig_clk + mig_stage_203_wait_cycle;

				int no_of_cores_halted = 0;
				for(int i = 0 ; i < mig_cores.size() ; i++ )
				{
					if ( (next_core_id != ar_page_1_core_id) && (next_core_id != ar_page_2_core_id) ) {
						Core* m_core = mig_cores[next_core_id].get();
						m_core->os_halt_cycle = mig_stage_203_wait_cycle;	//For eachpage x cycles 
						no_of_cores_halted++;
						if(no_of_cores_halted == 2) {
							next_core_id++;
							next_core_id = next_core_id % mig_cores.size();
							break;
						}
					}
					next_core_id++;
					next_core_id = next_core_id % mig_cores.size();
				}//for
            }
            else return;
        }   //203
	}
	
	if(migp_wait)
	{
		if(mig_clk >= migp_next_free_clk) {
			migp_bz = 0;
			migp_wait = 0;
			set_initial_overhead = 0;
			hma.onfly.mig_status = 100;
		}
		else return;
	}

    if(hma.onfly.mig_status > 200)
    {
		//printf("Clk; %ld, status is more than 200 \n", mig_clk);
		//onfly_page_mig_initial_overhead(hma.onfly.mig_status);	
		if(!set_initial_overhead) {
			//printf("Clk; %ld, set_init called\n", mig_clk);
			onfly_page_mig_initial_overhead(hma.onfly.mig_status);	
			set_initial_overhead = 1;	// CHANGE_ME
			return;
		}
    }

    // mig_status = 100 at init. If pending found then mig_status = 2, else mig_status = 1    
    if(hma.onfly.mig_status == 100)
    {
        start_complete_time = 0;
        mig_pair_count++;
        onfly_fill_mig_stage_q(&hma);
		//printf("Clk; %ld, in mig_status 100, new staus is %d\n", mig_clk, hma.onfly.mig_status);
    }
    else if(hma.onfly.mig_status == 2)    //all mig reqs already sent to mig qs, match found with demand qs, need to wait for those to be cleared
    {
        
        //wait //do nothing but also check if the pending count is 0 for demand request queue, if yes, the change change status of migration to 1**
        if(!onfly_mig_demand_pending())     // if pending then returns true, if no pending then returns false
        {
            //printf("changing mig_status to 1 since no pending count \n");	
		    hma.onfly.mig_status = 1;
        }

    }
    else if(hma.onfly.mig_status == 1) //mig initiated, reqs are being sent to mig qs, 1per cpu cycle
    {
        onfly_mig_send_read();   // process one req per migc clk and return
        return;
    }
    else if(hma.onfly.mig_status == 3)    //all mig reqs already sent to mig qs, no match with demand qs, start migrating
    {
	    onfly_mig_start();
	    return;
    }
    else if(hma.onfly.mig_status == 4)
    {
        onfly_mig_send_write();
        start_complete_time = mig_clk + 2;
        return;
    }
    else if(hma.onfly.mig_status == 5)
    {
        if(mig_clk > start_complete_time)
            onfly_mig_completion();
    }

    return;

}

//// Migc function ends
bool Window::is_full()
{
	return load == depth;
}

bool Window::is_empty()
{
	return load == 0;
}


void Window::insert(bool ready, long addr)
{
	assert(load <= depth);

	ready_list.at(head) = ready;
	addr_list.at(head) = addr;

	head = (head + 1) % depth;
	load++;
}

void Window::mig_insert(bool ready, long addr, int delays, int mig)     //mig_insert inserts in actual window
{
    assert(load <= depth);

    ready_list.at(head) = ready;
    addr_list.at(head) = addr;
    delay_list.at(head) = delays;
    mig_list.at(head) = mig;

    head = (head + 1) % depth;
    load++;

    //if(mig_list.at(head) == 3)
}


long Window::retire(long core_clk)
{
	assert(load <= depth);

	if (load == 0) return 0;

	int retired = 0;
	while (load > 0 && retired < ipc) {
		if (!ready_list.at(tail))
			break;
        if (delay_list.at(tail) > core_clk)
            break;
        delay_list.at(tail) = 0;            //resetting index
        mig_list.at(tail) = 0;              //resetting index

		tail = (tail + 1) % depth;
		load--;
		retired++;
	}

	return retired;
}

long Window::mig_retire()   // no need for passing clk
{
    assert(load <= depth);

    if (load == 0) return 0;

    int retired = 0;
    //we should consume one mig req per cpu cycle
    //also there is no need to consume in-order like demand reqs
    //though currently processing in-order
    //while (load > 0 && retired < ipc) {
    while (load > 0 && retired < 1) {
        if (!ready_list.at(tail)) 
        {
            break;
        }
    
        if(mig_list.at(tail) == 1 )   //copy to hot buffer here, check if the request belongs to PCM or HBM, to push to either hot or cold buffer
        {
            if(hot_buffer->bf_req.size() < hot_buffer->max)
            { 
                #ifdef MZ_DEBUG_ONFLY
                cout << " pushing to HB "<<"\n\n";
                #endif
                hot_buffer->bf_req.push_back(make_pair(addr_list.at(tail),0));
            }
            else if(hot_buffer->bf_req.size() == hot_buffer->max)
            {
                cout<<"hot buffer not empty\n";
            }
        }
        else if (mig_list.at(tail) == 0 )
        {
            if(cold_buffer->bf_req.size() < cold_buffer->max)
            {
                #ifdef MZ_DEBUG_ONFLY
                cout << "pushing to CB "<< "\n\n";
                #endif
                cold_buffer->bf_req.push_back(make_pair(addr_list.at(tail),0));
            }

            else if(cold_buffer->bf_req.size()==cold_buffer->max)
            {
                cout<<"cold buffer not empty\n";
            }
        }

        delay_list.at(tail) = 0;            //resetting index
        mig_list.at(tail) = 0;              //resetting index
        tail = (tail + 1) % depth;
        load--;
        retired++;
        #ifdef MZ_DEBUG_ONFLY
            cout << "mig retired: " << retired << "\n\n";
        #endif
    }

    return retired;
}

void Window::set_ready(long addr) // for window
{
	if (load == 0) return;

	for (int i = 0; i < load; i++) {
		int index = (tail + i) % depth;
		if ((addr_list.at(index) & mask) != (addr & mask))
			continue;
		ready_list.at(index) = true;
	}
}

void Window::set_ready_optimized(long addr, long delayed_time)    // for window, here the addr is already cache line aligned
{
	if (load == 0) return;

	for (int i = 0; i < load; i++) {
		int index = (tail + i) % depth;
        //printf("core set_ready_optimize mask is bin %16x, dec %d \n", mask, mask);
		if ((addr_list.at(index) & mask) != addr)
			continue;
        if(ready_list.at(index))
            continue;
        ready_list.at(index) = true;
        delay_list.at(index) = delayed_time;
	}
}

void Window::set_ready_for_mig_buffer(long addr, long cdt)      //for window, called when there is a hit in hot or cold buffer or after mig_retire
{                                                                          //provided addr is already cache block aligned 
    if (load == 0) return;

    for (int i = 0; i < load; i++) {
        int index = (tail + i) % depth;
        //cout << "mig buffer addr: "<< addr << "\t";
        //cout << "window addr: "<< addr_list.at(index) << "\t";
        //printf("mask is hex %16lx, dec %lu \n", mask, mask);
        if ( (addr_list.at(index) & mask) != addr)
            continue;
        if(ready_list.at(index))
            continue;
        ready_list.at(index) = true;
        delay_list.at(index) = cdt;
        //cout << "window addr: "<< addr_list.at(index) << " matched with mig buffer addr:  " << addr << "\n";
    }
}

void Window::mig_set_ready(long addr, int target_memory_type)   // this is called by mig_receive for mig_window
{                                                               // provided addr and addr_list are cache block aligned
    if (load == 0) return;

    for (int i = 0; i < load; i++) {
        int index = (tail + i) % depth;
        if (addr_list.at(index) != addr)
            continue;
        ready_list.at(index) = true;
        mig_list.at(index) = target_memory_type;    // now we know if the request is serviced by HBM(0) or PCM(1)

    }    
}

Trace::Trace(const char* trace_fname) : file(trace_fname), trace_name(trace_fname)
{
	if (!file.good()) {
		std::cerr << "Bad trace file: " << trace_fname << std::endl;
		exit(1);
	}
}

bool Trace::get_unfiltered_request(long& bubble_cnt, long& req_addr, Request::Type& req_type)
{
	string line;
	getline(file, line);
	if (file.eof()) {
		file.clear();
		file.seekg(0, file.beg);
		return false;
	}
	size_t pos, end;
	bubble_cnt = std::stoul(line, &pos, 10);
	pos = line.find_first_not_of(' ', pos+1);
	req_addr = std::stoul(line.substr(pos), &end, 0);

	pos = line.find_first_not_of(' ', pos+end);

	if (pos == string::npos || line.substr(pos)[0] == 'R')
		req_type = Request::Type::READ;
	else if (line.substr(pos)[0] == 'W')
		req_type = Request::Type::WRITE;
	else assert(false);
	return true;
}


bool Trace::get_request_pin_format(long& bubble_cnt, long& req_addr, Request::Type& req_type, uint64_t &req_addr_temp, int coreid)
{
	static int line_num = 0;
	uint64_t req_orig_va;
	uint64_t bub_cnt, req_pc, req_coreid, acc_type;
	string line;
	getline(file, line);
	line_num ++;
	if (file.eof() || line.size() == 0) {
		file.clear();
		file.seekg(0);
		// getline(file, line);
		line_num = 0;
		return false;
	}
	//
	size_t pos0, pos1, pos2;
	pos0  = line.find(" ");
	pos1  = line.find(" ", pos0+1);
	pos2  = line.find(" ", pos1+1);
	//pos3  = line.find(" ", pos2+1);
	//pos4  = line.find(" ", pos3+1);

	bub_cnt = stoul(line.substr(0, pos0), NULL, 10);
	req_orig_va = stoul(line.substr(pos0+1, pos1-pos0), NULL, 10);      //10 for SPEC, 16 for BFS
	//req_orig_va = stoul(line.substr(pos0+1, pos1-pos0), NULL, 16);
	req_pc = stoul(line.substr(pos1+1, pos2-pos1), NULL, 10);
	acc_type = stoul(line.substr(pos2+1, line.length()-pos2+1), NULL, 10);
	//acc_type = stoul(line.substr(pos3+1, pos4-pos3), NULL, 10);
	//pid_count = stoul(line.substr(pos4+1, line.length()-pos4+1), NULL, 10);


	req_addr = 100;	// it is just a dummy address, we do not assign uint64_t to long since it might corrupt some addr	
	req_addr_temp = req_orig_va;	//	at this moment it is just the virtual address, we will get the meaningful address
	//	only after "pin" translation
	bubble_cnt = (long) bub_cnt;	// no change needed for bubble count
	req_coreid = (uint64_t) coreid;

	if(acc_type == 2)
		req_type = Request::Type::WRITE;
	else
		req_type = Request::Type::READ;
	/*
	   std::cout << "Bubble_Count: " << bubble_cnt;
	   cout << " req add: " << req_addr ;
	   std::cout << " PC: " << req_pc ;
	   std::cout << " PID: " << req_pid ;
	   std::cout << " Type: " << acc_type;
	   std::cout << " P_Count: " << pid_count << std::endl;
	   */
	return true;
}


bool Trace::get_filtered_request(long& bubble_cnt, long& req_addr, Request::Type& req_type)
{
	static bool has_write = false;
	static long write_addr;
	static int line_num = 0;
	if (has_write){
		bubble_cnt = 0;
		req_addr = write_addr;
		req_type = Request::Type::WRITE;
		has_write = false;
		return true;
	}
	string line;
	getline(file, line);
	line_num ++;
	if (file.eof() || line.size() == 0) {
		file.clear();
		file.seekg(0, file.beg);
		has_write = false;
		line_num = 0;
		return false;
	}

	size_t pos, end;
	bubble_cnt = std::stoul(line, &pos, 10);

	pos = line.find_first_not_of(' ', pos+1);
	req_addr = stoul(line.substr(pos), &end, 0);
	req_type = Request::Type::READ;

	pos = line.find_first_not_of(' ', pos+end);
	if (pos != string::npos){
		has_write = true;
		write_addr = stoul(line.substr(pos), NULL, 0);
	}
	return true;
}

bool Trace::get_dramtrace_request(long& req_addr, Request::Type& req_type)
{
	string line;
	getline(file, line);
	if (file.eof()) {
		return false;
	}
	size_t pos;
	req_addr = std::stoul(line, &pos, 16);

	pos = line.find_first_not_of(' ', pos+1);

	if (pos == string::npos || line.substr(pos)[0] == 'R')
		req_type = Request::Type::READ;
	else if (line.substr(pos)[0] == 'W')
		req_type = Request::Type::WRITE;
	else assert(false);
	return true;
}
