#include "Processor.h"
#include "Config.h"
#include "Controller.h"
#include "SpeedyController.h"
#include "Memory.h"
#include "DRAM.h"
#include "Statistics.h"
#include "hma.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdlib.h>
#include <functional>
#include <map>

/* Standards */
#include "Gem5Wrapper.h"
#include "DDR3.h"
#include "DDR4.h"
#include "DSARP.h"
#include "GDDR5.h"
#include "LPDDR3.h"
#include "LPDDR4.h"
#include "WideIO.h"
#include "WideIO2.h"
#include "HBM.h"
#include "SALP.h"
#include "ALDRAM.h"
#include "TLDRAM.h"
#include "PCM.h"

using namespace std;
using namespace ramulator;

hma_struct_t hma;
req_page *ptable = NULL;

template<typename T>
void run_dramtrace(const Config& configs, Memory<T, Controller>& memory, const char* tracename) {

	/* initialize DRAM trace */
	Trace trace(tracename);

	/* run simulation */
	bool stall = false, end = false;
	int reads = 0, writes = 0, clks = 0;
	long addr = 0;
	Request::Type type = Request::Type::READ;
	map<int, int> latencies;
	auto read_complete = [&latencies](Request& r){latencies[r.depart - r.arrive]++;};

	Request req(addr, type, read_complete);

	while (!end || memory.pending_requests()){
		if (!end && !stall){
			end = !trace.get_dramtrace_request(addr, type);
		}

		if (!end){
			req.addr = addr;
			req.type = type;
			stall = !memory.send(req);
			if (!stall){
				if (type == Request::Type::READ) reads++;
				else if (type == Request::Type::WRITE) writes++;
			}
		}
		memory.tick();
		clks ++;
		Stats::curTick++; // memory clock, global, for Statistics
	}
	// This a workaround for statistics set only initially lost in the end
	memory.finish();
	Stats::statlist.printall();

}

	template <typename T>
void run_cputrace(const Config& configs, Memory<T, Controller>& memory, const std::vector<const char *>& files)
{
	int cpu_tick = configs.get_cpu_tick();
	int mem_tick = configs.get_mem_tick();
	auto send = bind(&Memory<T, Controller>::send, &memory, placeholders::_1);
	Processor proc(configs, files, send, memory);
	for (long i = 0; ; i++) {
		proc.tick();
		Stats::curTick++; // processor clock, global, for Statistics
		if (i % cpu_tick == (cpu_tick - 1))
			for (int j = 0; j < mem_tick; j++)
				memory.tick();
		if (configs.is_early_exit()) {
			if (proc.finished())
			{
				break;
			}
		} else {
			if (proc.finished() && (memory.pending_requests() == 0))
			{
				break;
			}
		}
	}
	// This a workaround for statistics set only initially lost in the end
	memory.finish();
	proc.print_stat();
	Stats::statlist.printall();
}

	template <typename T, typename K>
void hybrid_run_cputrace(const Config& configs, const Config& configs_l2, Memory<T, Controller>& memory, Memory<K, Controller>& memory_l2, const vector<const char*>& files)
{
	int cpu_tick = configs.get_cpu_tick();
	int mem_tick = configs.get_mem_tick();
	auto send = bind(&Memory<T, Controller>::send, &memory, placeholders::_1);

	// Mz: add l2 memory tick
	int cpu_tick_l2 = configs_l2.get_cpu_tick();
	int mem_tick_l2 = configs_l2.get_mem_tick();
	auto send_l2 = bind(&Memory<K, Controller>::send, &memory_l2, placeholders::_1);

	Processor proc(configs, configs_l2, files, send, send_l2, memory, memory_l2);
	for (long i = 0; ; i++) {
		proc.hybrid_tick();
		Stats::curTick++; // processor clock, global, for Statistics
		if (i % cpu_tick == (cpu_tick - 1))
			for (int j = 0; j < mem_tick; j++)
				memory.tick();
		if (i % cpu_tick_l2 == (cpu_tick_l2 - 1))   // Mz: for l2 memory
			for (int j = 0; j < mem_tick_l2; j++)
				memory_l2.tick();
		if (configs.is_early_exit() && configs_l2.is_early_exit()) {
			if (proc.finished())
				break;
		} else {
			if (proc.finished() && ( (memory.pending_requests() == 0) &&  (memory_l2.pending_requests() == 0) ))
				break;
		}
	}

	cout << "Hybrid_run_cputrace\n";
	// This a workaround for statistics set only initially lost in the end
	memory.finish();
	memory_l2.finish_l2();
	proc.print_hybrid_stat();
	Stats::statlist.printall();
}

	template <typename T, typename K>
void hybrid_run_cputrace_hma_epoch(const Config& configs, const Config& configs_l2, Memory<T, Controller>& memory, Memory<K, Controller>& memory_l2, const vector<const char*>& files)
{
	//*** STILL NOT READY FOR ONFLY ***//
	//just testing basic page structs and epoch based hma
    //string hma_type = "EPOCH"; //"ONFLY";
	
	int cpu_tick = configs.get_cpu_tick();
	int mem_tick = configs.get_mem_tick();
	auto send = bind(&Memory<T, Controller>::send, &memory, placeholders::_1);

	// Mz: add l2 memory tick
	int cpu_tick_l2 = configs_l2.get_cpu_tick();
	int mem_tick_l2 = configs_l2.get_mem_tick();
	auto send_l2 = bind(&Memory<K, Controller>::send, &memory_l2, placeholders::_1);
	
	Processor proc(configs, configs_l2, files, send, send_l2, memory, memory_l2, configs["trace_type"]);

	for (long i = 0; ; i++) {
		proc.hybrid_tick_hma_epoch();
		Stats::curTick++; // processor clock, global, for Statistics
		if (i % cpu_tick == (cpu_tick - 1))
			for (int j = 0; j < mem_tick; j++)
				memory.tick();
		if (i % cpu_tick_l2 == (cpu_tick_l2 - 1))   // Mz: for l2 memory
			for (int j = 0; j < mem_tick_l2; j++)
				memory_l2.tick();
		if (configs.is_early_exit() && configs_l2.is_early_exit()) {
			if (proc.finished())
				break;
		} else {
			if (proc.finished() && ( (memory.pending_requests() == 0) &&  (memory_l2.pending_requests() == 0) ))
				break;
		}
	}

	cout << "Hybrid_run_cputrace_hma_epoch\n";
	// This a workaround for statistics set only initially lost in the end
	memory.finish();
	memory_l2.finish_l2();
	proc.print_hybrid_stat();
	Stats::statlist.printall();
}


	template <typename T, typename K>
void hybrid_run_cputrace_hma_onfly(const Config& configs, const Config& configs_l2, Memory<T, Controller>& memory, Memory<K, Controller>& memory_l2, const vector<const char*>& files)
{
    string hma_type = "ONFLY";
	
	int cpu_tick = configs.get_cpu_tick();
	int mem_tick = configs.get_mem_tick();
	auto send = bind(&Memory<T, Controller>::send, &memory, placeholders::_1);
	auto send_mig = bind(&Memory<T, Controller>::send_mig, &memory, placeholders::_1);

	// Mz: add l2 memory tick
	int cpu_tick_l2 = configs_l2.get_cpu_tick();
	int mem_tick_l2 = configs_l2.get_mem_tick();
	auto send_l2 = bind(&Memory<K, Controller>::send, &memory_l2, placeholders::_1);
	auto send_mig_l2 = bind(&Memory<K, Controller>::send_mig, &memory_l2, placeholders::_1);
	
	Processor proc(configs, configs_l2, files, send, send_l2, send_mig, send_mig_l2, memory, memory_l2, configs["trace_type"]);

	for (long i = 0; ; i++) {
		proc.hybrid_tick_hma_onfly();
		Stats::curTick++; // processor clock, global, for Statistics
		if (i % cpu_tick == (cpu_tick - 1))
			for (int j = 0; j < mem_tick; j++)
				memory.tick_onfly_migration(hma.onfly.mig_status);
		if (i % cpu_tick_l2 == (cpu_tick_l2 - 1))   // Mz: for l2 memory
			for (int j = 0; j < mem_tick_l2; j++)
				memory_l2.tick_onfly_migration(hma.onfly.mig_status);
		if (configs.is_early_exit() && configs_l2.is_early_exit()) {
			if (proc.finished())
				break;
		} else {
			if (proc.finished() && ( (memory.pending_requests() == 0) &&  (memory_l2.pending_requests() == 0) ))
				break;
		}
	}

	cout << "Hybrid_run_cputrace_hma_onfly\n";
	// This a workaround for statistics set only initially lost in the end
	memory.finish();
	memory_l2.finish_l2();
	proc.print_hybrid_stat();
    proc.onfly_mig_print();
	Stats::statlist.printall();
}


template<typename T>
void start_run(const Config& configs, T* spec, const vector<const char*>& files) {
	// initiate controller and memory
	int C = configs.get_channels(), R = configs.get_ranks();
	// Check and Set channel, rank number
	spec->set_channel_number(C);
	spec->set_rank_number(R);
	std::vector<Controller<T>*> ctrls;
	for (int c = 0 ; c < C ; c++) {
		DRAM<T>* channel = new DRAM<T>(spec, T::Level::Channel);
		channel->id = c;
		channel->regStats("");
		Controller<T>* ctrl = new Controller<T>(configs, channel);
		ctrls.push_back(ctrl);
	}
	Memory<T, Controller> memory(configs, ctrls);

	assert(files.size() != 0);
	if (configs["trace_type"] == "CPU") {
		run_cputrace(configs, memory, files);
	} else if (configs["trace_type"] == "DRAM") {
		run_dramtrace(configs, memory, files[0]);
	} 
}

template<typename T, typename K>
void hybrid_start_run(const Config& configs, const Config& configs_l2, const Config& hmaconfig, T* spec, K* spec_l2,  const vector<const char*>& files) {
//void hybrid_start_run(const Config& configs, const Config& configs_l2, T* spec, K* spec_l2, const vector<const char*>& files) {
	// initiate controller and memory
	int C = configs.get_channels(), R = configs.get_ranks();
	//Mz: initiate 2nd level controller and memory
	int C_l2 = configs_l2.get_channels(), R_l2 = configs_l2.get_ranks();

	// Check and Set channel, rank number
	spec->set_channel_number(C);
	spec->set_rank_number(R);
	std::vector<Controller<T>*> ctrls;
	for (int c = 0 ; c < C ; c++) {
		DRAM<T>* channel = new DRAM<T>(spec, T::Level::Channel);
		channel->id = c;
		channel->regStats("");
		Controller<T>* ctrl = new Controller<T>(configs, channel);
		ctrls.push_back(ctrl);
	}
	Memory<T, Controller> memory(configs, ctrls);

	//Mz: Check and Set l2 channel, rank number
	spec_l2->set_channel_number(C_l2);
	spec_l2->set_rank_number(R_l2);
	std::vector<Controller<K>*> ctrls_l2;
	for (int c = 0 ; c < C_l2 ; c++) {
		DRAM<K>* channel_l2 = new DRAM<K>(spec_l2, K::Level::Channel);
		channel_l2->id = c;
		channel_l2->regStats("");
		Controller<K>* ctrl_l2 = new Controller<K>(configs_l2, channel_l2);
		ctrls_l2.push_back(ctrl_l2);
	}
	Memory<K, Controller> memory_l2(configs_l2, ctrls_l2);

	if (configs["trace_type"] == "CPU") {
		hybrid_run_cputrace(configs, configs_l2, memory, memory_l2, files);
	} 
	else if (configs["trace_type"] == "HMA_EPOCH") {
		cout << "Running: " << configs["trace_type"] <<" l1m size: " << memory.hma_mem_size << " l2msize: " << memory_l2.hma_mem_size << " l1m mem bw: " << memory.hma_mem_bw  << " l2m mem bw: " << memory_l2.hma_mem_bw <<"\n" <<std::flush; 
		hma_config_and_init(&hma, hmaconfig, memory.hma_mem_size, memory.hma_mem_bw, memory_l2.hma_mem_size, memory_l2.hma_mem_bw); 
		memory.override_memory_page_size(hma.config.page_size);
		hybrid_run_cputrace_hma_epoch(configs, configs_l2, memory, memory_l2, files);
	}
	else if (configs["trace_type"] == "HMA_ONFLY") {
		cout << "Running: " << configs["trace_type"] <<" l1m size: " << memory.hma_mem_size << " l2msize: " << memory_l2.hma_mem_size << " l1m mem bw: " << memory.hma_mem_bw  << " l2m mem bw: " << memory_l2.hma_mem_bw <<"\n" <<std::flush; 
		hma_config_and_init(&hma, hmaconfig, memory.hma_mem_size, memory.hma_mem_bw, memory_l2.hma_mem_size, memory_l2.hma_mem_bw); 
		memory.override_memory_page_size(hma.config.page_size);
		hybrid_run_cputrace_hma_onfly(configs, configs_l2, memory, memory_l2, files);
	}
	// else if (configs["trace_type"] == "PREF") {
	//   hybrid_run_cputrace_pref(configs, configs_l2, memory, memory_l2, files);
	//}

}

int main(int argc, const char *argv[])
{
	if (argc < 2) {
		printf("Usage: %s <configs-file> --mode=cpu,dram [--stats <filename>] <trace-filename1> <trace-filename2>\n"
				"Example: %s ramulator-configs.cfg --mode=cpu cpu.trace cpu.trace\n", argv[0], argv[0]);
		return 0;
	}


	string run_mode = argv[1];

	if(run_mode == "normal")
	{   //Mz: non-hybrid 
		Config configs(argv[2]);

		// Config configs(argv[1]);

		const std::string& standard = configs["standard"];
		assert(standard != "" || "DRAM standard should be specified.");

		const char *trace_type = strstr(argv[3], "=");
		trace_type++;
		if (strcmp(trace_type, "cpu") == 0) {
			configs.add("trace_type", "CPU");
		} else if (strcmp(trace_type, "dram") == 0) {
			configs.add("trace_type", "DRAM");
		} else {
			printf("invalid trace type: %s\n", trace_type);
			assert(false);
		}

		int trace_start = 4;
		string stats_out;
		if (strcmp(argv[4], "--stats") == 0) {
			Stats::statlist.output(argv[5]);
			stats_out = argv[5];
			trace_start = 6;
		} else {
			Stats::statlist.output(standard+".stats");
			stats_out = standard + string(".stats");
		}
		std::vector<const char*> files(&argv[trace_start], &argv[argc]);
		configs.set_core_num(argc - trace_start);

		if (standard == "DDR3") {
			DDR3* ddr3 = new DDR3(configs["org"], configs["speed"]);
			start_run(configs, ddr3, files);
		} else if (standard == "DDR4") {
			DDR4* ddr4 = new DDR4(configs["org"], configs["speed"]);
			start_run(configs, ddr4, files);
		} else if (standard == "SALP-MASA") {
			SALP* salp8 = new SALP(configs["org"], configs["speed"], "SALP-MASA", configs.get_subarrays());
			start_run(configs, salp8, files);
		} else if (standard == "LPDDR3") {
			LPDDR3* lpddr3 = new LPDDR3(configs["org"], configs["speed"]);
			start_run(configs, lpddr3, files);
		} else if (standard == "LPDDR4") {
			// total cap: 2GB, 1/2 of others
			LPDDR4* lpddr4 = new LPDDR4(configs["org"], configs["speed"]);
			start_run(configs, lpddr4, files);
		} else if (standard == "GDDR5") {
			GDDR5* gddr5 = new GDDR5(configs["org"], configs["speed"]);
			start_run(configs, gddr5, files);
		} else if (standard == "HBM") {
			HBM* hbm = new HBM(configs["org"], configs["speed"]);
			start_run(configs, hbm, files);
		} else if (standard == "WideIO") {
			// total cap: 1GB, 1/4 of others
			WideIO* wio = new WideIO(configs["org"], configs["speed"]);
			start_run(configs, wio, files);
		} else if (standard == "WideIO2") {
			// total cap: 2GB, 1/2 of others
			WideIO2* wio2 = new WideIO2(configs["org"], configs["speed"], configs.get_channels());
			wio2->channel_width *= 2;
			start_run(configs, wio2, files);
		} // Mz Adding PCM configuration
		else if (standard == "PCM") {
			PCM* pcm = new PCM(configs["org"], configs["speed"]);
			start_run(configs, pcm, files);
		} // Various refresh mechanisms
		else if (standard == "DSARP") {
			DSARP* dsddr3_dsarp = new DSARP(configs["org"], configs["speed"], DSARP::Type::DSARP, configs.get_subarrays());
			start_run(configs, dsddr3_dsarp, files);
		} else if (standard == "ALDRAM") {
			ALDRAM* aldram = new ALDRAM(configs["org"], configs["speed"]);
			start_run(configs, aldram, files);
		} else if (standard == "TLDRAM") {
			TLDRAM* tldram = new TLDRAM(configs["org"], configs["speed"], configs.get_subarrays());
			start_run(configs, tldram, files);
		}

		printf("Simulation done. Statistics written to %s\n", stats_out.c_str());
	}
	else
	{
		if(run_mode == "hybrid")
		{

			Config configs(argv[2]);
			Config configs_l2(argv[3]);
			Config hmaconfig(argv[4]);

			/* Mz: will make it flexible later
			const std::string& standard = configs["standard"];
			assert(standard != "" || "DRAM standard should be specified.");
			const std::string& standard_l2 = configs_l2["standard"];
			assert(standard != "" || "DRAM standard should be specified.");
			*/
		
			const char *trace_type = strstr(argv[5], "=");
			trace_type++;
			if (strcmp(trace_type, "cpu") == 0) {
				configs.add("trace_type", "CPU");
				configs_l2.add("trace_type", "CPU");
			} else if (strcmp(trace_type, "pref") == 0) {
				configs.add("trace_type", "PREF");
				configs_l2.add("trace_type", "PREF");
			} else if (strcmp(trace_type, "hma_epoch") == 0) {
				configs.add("trace_type", "HMA_EPOCH");
				configs_l2.add("trace_type", "HMA_EPOCH");
			} else if (strcmp(trace_type, "hma_onfly") == 0) {
				configs.add("trace_type", "HMA_ONFLY");
				configs_l2.add("trace_type", "HMA_ONFLY");
			} else {
				printf("invalid trace type: %s\n", trace_type);
				assert(false);
			}

			int trace_start = 6;
			string stats_out;
			if (strcmp(argv[6], "--stats") == 0) {
				Stats::statlist.output(argv[7]);
				stats_out = argv[7];
				trace_start = 8;
			} 
			//const char* file = argv[trace_start];
			std::vector<const char*> files(&argv[trace_start], &argv[argc]);
			configs.set_core_num(argc - trace_start);
			configs_l2.set_core_num(argc - trace_start);

			HBM* hbm = new HBM(configs["org"], configs["speed"]);
			PCM* pcm = new PCM(configs_l2["org"], configs_l2["speed"]);
			//DDR3* ddr3 = new DDR3(configs_l2["org"], configs_l2["speed"]);

			hybrid_start_run(configs, configs_l2, hmaconfig, hbm, pcm, files);	// later depending on mode hmaconfig might be used or not used
																				// but we will always provide a HMA-config file	

			printf("Simulation done. Statistics written to %s\n", stats_out.c_str());
		}

	}
	return 0;
}
