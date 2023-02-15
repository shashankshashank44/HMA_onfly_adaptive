#include "Cache.h"
//#include "hma.h"

#ifndef DEBUG_CACHE
#define debug(...)
#else
#define debug(...) do { \
	printf("\033[36m[DEBUG] %s ", __FUNCTION__); \
	printf(__VA_ARGS__); \
	printf("\033[0m\n"); \
} while (0)
#endif

namespace ramulator
{

	Cache::Cache(int size, int assoc, int block_size,
			int mshr_entry_num, Level level,
			std::shared_ptr<CacheSystem> cachesys):
		level(level), cachesys(cachesys), higher_cache(0),
		lower_cache(nullptr), size(size), assoc(assoc),
		block_size(block_size), mshr_entry_num(mshr_entry_num) {

			debug("level %d size %d assoc %d block_size %d\n",
					int(level), size, assoc, block_size);
			/*
			   if (level == Level::L1) {
			   level_string = "L1";
			   } else if (level == Level::L2) {
			   level_string = "L2";
			   } else if (level == Level::L3) {
			   level_string = "L3";
			   }*/

			//Mz: we want two level caches
			if (level == Level::L1) {
				level_string = "L1";
			} else if (level == Level::L3) {
				level_string = "L3";
			}

			is_first_level = (level == cachesys->first_level);
			is_last_level = (level == cachesys->last_level);

			// Check size, block size and assoc are 2^N
			assert((size & (size - 1)) == 0);
			assert((block_size & (block_size - 1)) == 0);
			assert((assoc & (assoc - 1)) == 0);
			assert(size >= block_size);

			// Initialize cache configuration
			block_num = size / (block_size * assoc);
			index_mask = block_num - 1;
			index_offset = calc_log2(block_size);
			tag_offset = calc_log2(block_num) + index_offset;

			debug("index_offset %d", index_offset);
			debug("index_mask 0x%x", index_mask);
			debug("tag_offset %d", tag_offset);

			// regStats
			cache_read_miss.name(level_string + string("_cache_read_miss"))
				.desc("cache read miss count")
				.precision(0)
				;

			cache_write_miss.name(level_string + string("_cache_write_miss"))
				.desc("cache write miss count")
				.precision(0)
				;

			cache_total_miss.name(level_string + string("_cache_total_miss"))
				.desc("cache total miss count")
				.precision(0)
				;

			cache_eviction.name(level_string + string("_cache_eviction"))
				.desc("number of evict from this level to lower level")
				.precision(0)
				;

			cache_read_access.name(level_string + string("_cache_read_access"))
				.desc("cache read access count")
				.precision(0)
				;

			cache_write_access.name(level_string + string("_cache_write_access"))
				.desc("cache write access count")
				.precision(0)
				;

			cache_total_access.name(level_string + string("_cache_total_access"))
				.desc("cache total access count")
				.precision(0)
				;

			cache_mshr_hit.name(level_string + string("_cache_mshr_hit"))
				.desc("cache mshr hit count")
				.precision(0)
				;
			cache_mshr_unavailable.name(level_string + string("_cache_mshr_unavailable"))
				.desc("cache mshr not available count")
				.precision(0)
				;
			cache_set_unavailable.name(level_string + string("_cache_set_unavailable"))
				.desc("cache set not available")
				.precision(0)
				;
		}

	//Mz coherence cache invalidation
	bool Cache::send_invalidate(long inv_addr) {
		// Mz: In case of write-invalidation if there is no set fro req x, then there is no need to create a set for it
		// Mz: We can assume that as a miss and simply ignore it?
		//printf("Cache Clk %ld recvd for inv::  req addr %lx\n",cachesys->clk, inv_addr);
		if(check_lines(inv_addr))
		{
			auto& lines = get_lines(inv_addr);
			std::list<Line>::iterator line;
			if (is_hit(lines, inv_addr, &line)) 
			{
				// Mz: below two lines are for updating the lru list i guess, we do not need to update lru,
				// we just want to delete the line which got hit
				//lines.push_back(Line(req.addr, get_tag(req.addr), false, true));    // push a dirty line
				//lines.erase(line);
				line->lock = false; // since we found hit on this line, it is not locked, ie data is already there
				// Mz: now we will evict this line
				bool check = !line->lock; // check is true now
				if (!is_first_level) 
				{
					for (auto hc : higher_cache) 
					{
						if(!check) 
						{
							return check;
						}
						check = check && hc->check_unlock(line->addr);
					}
				}
				if(check)   //at this point check must be true for all higher level cache for successful evicton    
				{           // other wise we should not proceed
					evict(&lines, line); // deleted & since line is auto &
				}
			}
			else    
			{
				return false;   //no line to invalidate
			}

		}
		else
		{    
			return false;   //no line to invalidate
		}

		//printf("Cache Clk %ld returning for inv::  req addr %lx\n", cachesys->clk, inv_addr);
		hma.stats.total_cline_invalidations++;
		return true;
	}

	bool Cache::send(Request req) {
		debug("level %d req.addr %lx req.type %d, index %d, tag %ld",
				int(level), req.addr, int(req.type), get_index(req.addr),
				get_tag(req.addr));

		// Mz:
		bool is_read_1 = false, is_read_2 = false;

		cache_total_access++;
		if (req.type == Request::Type::WRITE) {
			cache_write_access++;
		} else {
			assert(req.type == Request::Type::READ);
			cache_read_access++;
			is_read_1 = true;
		}
		// If there isn't a set, create it.
		// Mz: In case of write-invalidation if there is no set fro req x, then there is no need to create a set for it
		// Mz: We can assume that as a miss and simply ignore it?
		auto& lines = get_lines(req.addr);
		std::list<Line>::iterator line;
        

		if (is_hit(lines, req.addr, &line)) {
			lines.push_back(Line(req.addr, get_tag(req.addr), false,
						line->dirty || (req.type == Request::Type::WRITE)));
			lines.erase(line);
			cachesys->hit_list.push_back(
					make_pair(cachesys->clk + latency[int(level)], req));

			debug("hit, update timestamp %ld", cachesys->clk);
			debug("hit finish time %ld",
					cachesys->clk + latency[int(level)]);

			return true;

		} else {

			debug("miss @level %d", int(level));
			cache_total_miss++;
			if (req.type == Request::Type::WRITE) {
				cache_write_miss++;
			} else {
				assert(req.type == Request::Type::READ);
				cache_read_miss++;
				is_read_2 = true;
			}

			// The dirty bit will be set if this is a write request and @L1
			bool dirty = (req.type == Request::Type::WRITE);

			// Modify the type of the request to lower level
			if (req.type == Request::Type::WRITE) {
				req.type = Request::Type::READ;
			}

			// Look it up in MSHR entries
			assert(req.type == Request::Type::READ);
			auto mshr = hit_mshr(req.addr);
			if (mshr != mshr_entries.end()) {
				debug("hit mshr");
				cache_mshr_hit++;
				mshr->first.second->dirty = dirty || mshr->first.second->dirty;
				return true;
			}

			// All requests come to this stage will be READ, so they
			// should be recorded in MSHR entries.
			if (mshr_entries.size() == mshr_entry_num) {
				// When no MSHR entries available, the miss request
				// is stalling.
				cache_mshr_unavailable++;

				// Mz: when the miss request is stalling we do not want to count it as avalid cache access, so adjusting the counts
				cache_total_access--;
				cache_total_miss--;

				if(is_read_1)
					cache_read_access--;
				else cache_write_access--;

				if(is_read_2)
					cache_read_miss--;
				else cache_write_miss--;

				debug("no mshr entry available");
				return false;
			}

			// Check whether there is a line available
			if (all_sets_locked(lines)) {
				cache_set_unavailable++;
				return false;
			}
			auto newline = allocate_line(lines, req.addr);
			if (newline == lines.end()) {
				return false;
			}

			newline->dirty = dirty;

			// Add to MSHR entries
			//mshr_entries.push_back(make_pair(req.addr, newline));
			mshr_entries.push_back(make_pair( make_pair(req.addr, newline), make_pair(0, 0) ));   // adding mig_list and another empty list to mshr entries

			// Send the request to next level;
			if (!is_last_level) {
				lower_cache->send(req);
			} else {
				cachesys->wait_list.push_back(
						make_pair(cachesys->clk + latency[int(level)], req));	// Mz: only llc can place requests in wail_list, hence cache system takes request from
				// wait_list and send them to memory
			}
			return true;
		}
	}

	void Cache::evictline(long addr, bool dirty) {

		auto it = cache_lines.find(get_index(addr));
		assert(it != cache_lines.end()); // check inclusive cache
		auto& lines = it->second;
		auto line = find_if(lines.begin(), lines.end(),
				[addr, this](Line l){return (l.tag == get_tag(addr));});

		assert(line != lines.end());
		// Update LRU queue. The dirty bit will be set if the dirty
		// bit inherited from higher level(s) is set.
		lines.push_back(Line(addr, get_tag(addr), false,
					dirty || line->dirty));
		lines.erase(line);
	}

	std::pair<long, bool> Cache::invalidate(long addr) {
		long delay = latency_each[int(level)];
		bool dirty = false;

		auto& lines = get_lines(addr);
		if (lines.size() == 0) {
			// The line of this address doesn't exist.
			return make_pair(0, false);
		}
		auto line = find_if(lines.begin(), lines.end(),
				[addr, this](Line l){return (l.tag == get_tag(addr));});

		// If the line is in this level cache, then erase it from
		// the buffer.
		if (line != lines.end()) {
			assert(!line->lock);
			debug("invalidate %lx @ level %d", addr, int(level));
			lines.erase(line);
		} else {
			// If it's not in current level, then no need to go up.
			return make_pair(delay, false);
		}

		if (higher_cache.size()) {
			long max_delay = delay;
			for (auto hc : higher_cache) {
				auto result = hc->invalidate(addr);
				if (result.second) {
					max_delay = max(max_delay, delay + result.first * 2);
				} else {
					max_delay = max(max_delay, delay + result.first);
				}
				dirty = dirty || line->dirty || result.second;
			}
			delay = max_delay;
		} else {
			dirty = line->dirty;
		}
		return make_pair(delay, dirty);
	}


	void Cache::evict(std::list<Line>* lines,
			std::list<Line>::iterator victim) {
		debug("level %d miss evict victim %lx", int(level), victim->addr);
		cache_eviction++;

		long addr = victim->addr;
		long invalidate_time = 0;
		bool dirty = victim->dirty;

		// First invalidate the victim line in higher level.
		if (higher_cache.size()) {
			for (auto hc : higher_cache) {
				auto result = hc->invalidate(addr);
				invalidate_time = max(invalidate_time,
						result.first + (result.second ? latency_each[int(level)] : 0));
				dirty = dirty || result.second || victim->dirty;
			}
		}

		debug("invalidate delay: %ld, dirty: %s", invalidate_time,
				dirty ? "true" : "false");

		if (!is_last_level) {
			// not LLC eviction
			assert(lower_cache != nullptr);
			lower_cache->evictline(addr, dirty);
		} else {
			// LLC eviction
			if (dirty) {
				Request write_req(addr, Request::Type::WRITE);
				cachesys->wait_list.push_back(make_pair(
							cachesys->clk + invalidate_time + latency[int(level)],
							write_req));
				//printf("inject one write request to memory system addr %lx, invalidate time %ld, issue time %ld\n", write_req.addr, invalidate_time,						cachesys->clk + invalidate_time + latency[int(level)]);

				debug("inject one write request to memory system "
						"addr %lx, invalidate time %ld, issue time %ld",
						write_req.addr, invalidate_time,
						cachesys->clk + invalidate_time + latency[int(level)]);
			}
		}

		lines->erase(victim);
	}

	std::list<Cache::Line>::iterator Cache::allocate_line(
			std::list<Line>& lines, long addr) {
		// See if an eviction is needed
		if (need_eviction(lines, addr)) {
			// Get victim.
			// The first one might still be locked due to reorder in MC
			auto victim = find_if(lines.begin(), lines.end(),
					[this](Line line) {
					bool check = !line.lock;
					if (!is_first_level) {
					for (auto hc : higher_cache) {
					if(!check) {
					return check;
					}
					check = check && hc->check_unlock(line.addr);
					}
					}
					return check;
					});
			if (victim == lines.end()) {
				return victim;  // doesn't exist a line that's already unlocked in each level
			}
			assert(victim != lines.end());
			evict(&lines, victim);
		}

		// Allocate newline, with lock bit on and dirty bit off
		lines.push_back(Line(addr, get_tag(addr)));
		auto last_element = lines.end();
		--last_element;
		return last_element;
	}

	bool Cache::is_hit(std::list<Line>& lines, long addr,
			std::list<Line>::iterator* pos_ptr) {
		auto pos = find_if(lines.begin(), lines.end(),
				[addr, this](Line l){return (l.tag == get_tag(addr));});
		*pos_ptr = pos;
		if (pos == lines.end()) {
			return false;
		}
		return !pos->lock;
	}

	void Cache::concatlower(Cache* lower) {
		lower_cache = lower;
		assert(lower != nullptr);
		lower->higher_cache.push_back(this);
	};

	bool Cache::need_eviction(const std::list<Line>& lines, long addr) {
		if (find_if(lines.begin(), lines.end(),
					[addr, this](Line l){
					return (get_tag(addr) == l.tag);})
				!= lines.end()) {
			// Due to MSHR, the program can't reach here. Just for checking
			assert(false);
		} else {
			if (lines.size() < assoc) {
				return false;
			} else {
				return true;
			}
		}
	}

	void Cache::callback(Request& req) {
		debug("level %d", int(level));

		auto it = find_if(mshr_entries.begin(), mshr_entries.end(),
				[&req, this](std::pair< std::pair<long, std::list<Line>::iterator> , std::pair<short, short> > mshr_entry) {
				return (align(mshr_entry.first.first) == align(req.addr));
				});

		if (it != mshr_entries.end()) {
			it->first.second->lock = false;
			mshr_entries.erase(it);
		}

		if (higher_cache.size()) {
			//for (auto hc : higher_cache) {
                if (level == Level::L3) {
				    higher_cache[req.coreid]->callback(req);
                }
			//}
		}
	}

	int Cache::callback_migration(long r_addr, int core_id, int count) {
		debug("level %d", int(level));

        // We first check if LLC MSHR has the entry. 
        // If it has then clean it and go higher level.
        // If it does not have, than no core is waiting at memory level for this request
        // So do not clean it from higher level cache MSHR, may be there are req in core window
        // and L1 MSHR waiting on hit_list of LLC

		auto it = find_if(mshr_entries.begin(), mshr_entries.end(),
				[&r_addr, this](std::pair< std::pair<long, std::list<Line>::iterator> , std::pair<short, short> > mshr_entry) {
				return (align(mshr_entry.first.first) == r_addr);
				});

		if (it != mshr_entries.end()) {
			it->first.second->lock = false;
			mshr_entries.erase(it);
            count++;
		}
        else {
            return count;
        }

		if (higher_cache.size()) {
            if (level == Level::L3) {
                count = higher_cache[core_id]->callback_migration(r_addr, core_id, count);
            }
		}

        return count;
	}

   // Align the address to have only page number (which does not have last 11/12 bits)
   long Cache::align_to_page_number(long addr) {
     return (addr >> (cachesys->row_shift) );
   }

    int Cache::onfly_search(int64_t page_number)    // called to check if init pending is cleared or not
    {

        int pending_count = 0;

        int i = 0;
        for(i = 0; i < mshr_entries.size(); i++)
        {
            if(mshr_entries[i].second.first == 1) {  // it was already pending
                pending_count++;    // no need tomark wait list, since already marked
            }
        }// for

         return pending_count;
    }

    int Cache::onfly_search_set_init(int64_t page_number)    // called only from mig_status 100 to check and set init pending list
    {

        int pending_count = 0;

        int i = 0;
        for(i = 0; i < mshr_entries.size(); i++)
        {
            if( (mshr_entries[i].first.second->lock) && (mshr_entries[i].second.first < 2) )  //first need to check if this mshr entry is locked (pending) and mig_list is 0/1
            {
                if (align_to_page_number(mshr_entries[i].first.first) == page_number)
                {
                    if(mshr_entries[i].second.first == 0) { // found a pending for the first time. so mark it in mshr after checking eait list
                        bool sent_to_mem = true;
                        // Find it in wait_list, which means till now not sent to mem. 
                        // So change req_category from 0 to 1 and dont send it to mem. Will be served from buffer
                        auto it = cachesys->wait_list.begin();
                        while (it != cachesys->wait_list.end()) {
                            if (align_to_page_number((it->second).addr) == page_number) {
                                (it->second).req_category = 1;
                                sent_to_mem = false;
                                break;
                            }
                            else {
                                ++it;
                            }
                        } //while on wait list
                        if(sent_to_mem) {   // already sent to mem, so must wait
                            mshr_entries[i].second.first = 1;  
                            pending_count++;
                        }
                    }
                    else if(mshr_entries[i].second.first == 1) {  // it was already pending
                        pending_count++;    // no need tomark wait list, since already marked
                    }
                } //if page number matches
            }
        }// for

         return pending_count;
    }

    void CacheSystem::set_buffers(Buffer * a, Buffer *b)
    {
        hot_buffer = a;
        cold_buffer = b;
    }
    
	void CacheSystem::tick() {
		debug("clk %ld", clk);

		++clk;
#ifdef MZ_DEBUG
		cout << "at cache system tick, clk is : " << clk  << "\n";
#endif

		// Sends ready waiting request to memory
		auto it = wait_list.begin();
		while (it != wait_list.end() && clk >= it->first) {
			if (!send_memory(it->second)) {
				++it;
			} else {

				debug("complete req: addr %lx", (it->second).addr);

#ifdef MZ_DEBUG
				//cout << "at cache system tick, clk is : " << clk  << "\tsending to memory req with timestamp: " \\
				//<< (it->first) << " addr: " << (it->second).addr <<"\n";
				if( (it->second).type == Request::Type::READ)
					cout << (it->second).addr << " " << "1"<<"\n";
				else
					cout << (it->second).addr << " " << "2"<<"\n";
#endif

				it = wait_list.erase(it);	// returns an iterator pointing to the element that 
											// followed the last element erased by the function call
											// hence multiple memory requests can be sent in a single cycle
			}
		}

		// hit request callback
		it = hit_list.begin();
		while (it != hit_list.end()) {
			if (clk >= it->first) {
				it->second.callback(it->second);

				debug("finish hit: addr %lx", (it->second).addr);

				it = hit_list.erase(it);
			} else {
				++it;
			}
		}
	}

	//Mz: for simple hybrid system with two types of memories
	void CacheSystem::hybrid_tick() {
		debug("clk %ld", clk);

		++clk;
#ifdef MZ_DEBUG_HYBRID
		cout << "at cache system tick, clk is : " << clk  << "\n";
#endif

		// Sends ready waiting request to memory
		auto it = wait_list.begin();
		while (it != wait_list.end() && clk >= it->first) 
		{
			(it->second).req_target_mem = CHECK_TARGET_MEM((it->second).addr);

			if((it->second).req_target_mem == 0)
			{
				if (!send_memory(it->second)) {
					++it;
				} else {
					debug("complete req: addr %lx", (it->second).addr);
#ifdef MZ_DEBUG_HYBRID
					//cout << "at cache system tick, clk is : " << clk  << "\tsending to memory req with timestamp: "\\
					//<< (it->first) << " addr: " << (it->second).addr <<"\n";
					if( (it->second).type == Request::Type::READ)
						cout << "Sending to L1m: " << (it->second).addr << " " << "R"<<"\n";
					else
						cout << "Sending to L1m: " << (it->second).addr << " " << "W"<<"\n";
#endif
					it = wait_list.erase(it);	
				}
			} //l1m
			else if((it->second).req_target_mem == 1)
			{
				(it->second).phys_addr = (it->second).addr - L2M_START_OFFSET;
				if (!send_memory_l2(it->second)) {
					++it;
				} else {
					debug("complete req: addr %lx", (it->second).addr);
#ifdef MZ_DEBUG_HYBRID
					//cout << "at cache system tick, clk is : " << clk  << "\tsending to memory req with timestamp: " \\
					//<< (it->first) << " addr: " << (it->second).addr <<"\n";
					if( (it->second).type == Request::Type::READ)
						cout << "Sending to L2m: " << (it->second).addr << " " << "R"<<"\n";
					else
						cout << "Sending to L2m: " << (it->second).addr << " " << "W"<<"\n";
#endif
					it = wait_list.erase(it);	
				}
			} //l2m

		}

		// hit request callback
		it = hit_list.begin();
		while (it != hit_list.end()) {
			if (clk >= it->first) {
				it->second.callback(it->second);

				debug("finish hit: addr %lx", (it->second).addr);

				it = hit_list.erase(it);
			} else {
				++it;
			}
		}
	}//tick ends

	//Mz: for simple hybrid system with two types of memories and hma transer, epoch based (page structures are created)
	void CacheSystem::hybrid_tick_hma_epoch() {
		debug("clk %ld", clk);
		++clk;
		
		static int64_t epoch_cycle_counter = 0;
		epoch_cycle_counter++;

		//if (epoch_cycle_counter >= 1000000)   //testing
		if (epoch_cycle_counter >= hma.config.t_epoch) 
		{
			epoch_cycle_counter = 0;
			hma.config.epoch++;
			cout << "Epoch called: " << hma.config.epoch << "\n";
			hma_policy_invoker(&hma, clk);
		}

#ifdef MZ_DEBUG_HYBRID
		cout << "at cache system tick, clk is : " << clk  << "\n";
#endif
		if(clk == 2) {
			cout << "HMA page size: " << hma.config.page_size << " page transfer limit: " << hma.config.page_transfer_limit << "\n";
		}

		// Sends ready waiting request to memory
		auto it = wait_list.begin();
		while (it != wait_list.end() && clk >= it->first) 
		{

			//RAM_HMA: new part
			long req_addr = (it->second).addr;
			struct req_page *r;
			int64_t pagenum = req_addr/hma.config.page_size;
			HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), r);

			if(r == NULL)
			{
				r = hma_handler(&hma, req_addr, (it->second).coreid);
				if(r == NULL)
				{
					// if r is stiil NULL
					cout << "RUNNING OUT OF SYSTEM MEMORY\n" ;
					exit(0);
				}
			}
			// RAM_HMA: re-calculate the request's phys memory address depending on the HMA modified memory location
			int64_t page_offset = (req_addr % hma.config.page_size);    //save page offset bits here
			int64_t target_frame_base_addr = r->frame_addr; // since frame_addr is actual location of the starting of the frame, it contains page offset bit as 0
			int64_t target_phys_addr = target_frame_base_addr + page_offset;
			// Now update the req's physical address and target memory accordingly
			(it->second).phys_addr = target_phys_addr;	// L2M offset is already taken care
			(it->second).req_target_mem = r->dom;    // 0=l1m, 1=l2m, 2=l3m 

			if((it->second).req_target_mem == 0)
			{
				if (!send_memory(it->second)) {
					++it;
				} else {
					hma_epoch_count_keeper(&hma, r, clk);
					debug("complete req: addr %lx", (it->second).addr);
#ifdef MZ_DEBUG_HYBRID
					//cout << "at cache system tick, clk is : " << clk  << "\tsending to memory req with timestamp: " \\
					//<< (it-:>first) << " addr: " << (it->second).addr <<"\n";
					if( (it->second).type == Request::Type::READ)
						cout << "Sending to L1m: " << (it->second).addr << " " << "R"<<"\n";
					else
						cout << "Sending to L1m: " << (it->second).addr << " " << "W"<<"\n";
#endif
					it = wait_list.erase(it);	
				}
			} //l1m
			else if((it->second).req_target_mem == 1)
			{
				if (!send_memory_l2(it->second)) {
					++it;
				} else {
					hma_epoch_count_keeper(&hma, r, clk);
					debug("complete req: addr %lx", (it->second).addr);
#ifdef MZ_DEBUG_HYBRID
					//cout << "at cache system tick, clk is : " << clk  << "\tsending to memory req with timestamp: " \\
					//<< (it->first) << " addr: " << (it->second).addr <<"\n";
					if( (it->second).type == Request::Type::READ)
						cout << "Sending to L2m: " << (it->second).addr << " " << "R"<<"\n";
					else
						cout << "Sending to L2m: " << (it->second).addr << " " << "W"<<"\n";
#endif
					it = wait_list.erase(it);	
				}
			} //l2m

		}

		// hit request callback
		it = hit_list.begin();
		while (it != hit_list.end()) {
			if (clk >= it->first) {
				it->second.callback(it->second);

				debug("finish hit: addr %lx", (it->second).addr);

				it = hit_list.erase(it);
			} else {
				++it;
			}
		}
	}//tick ends

    int CacheSystem::onfly_mig_demand_check(Request &req, int64_t pagenum, int64_t page_offset, short candidate_pg_flag) { 
    ///**** changes for migration ************////////////
    // if it is hot/cold page and request type is READ:
    // case 1: hit in buffer with status 0/1. cache line is in buffer will be written to new destination
    //      do callback with necessary delay, dont send to memory, erase from wait list. continue with next while iteration. dont count keeper
    // case 2: already sent to new destination meory's wrtie q. status 2
    //      change target mem and send req accordingly... erase from wait list.. do count_keeper... 
    // case 3: not hit in buffer, will be brought in future by migc and also be cleaned
    //      this req is already in window and mshr. so we do not have to insert it.
    //      do nothing, do not sent to memory, erase the req from wait list, bcoz migc will do the callback in future.
    //      do not count keeper. continue with next while iteration
    //      ***above not ok: what happens if there is pending request??
    //      it can be the case that for which mshr is 1 for that still request sitting in wait_list and we are not able to clean it??
    //      and we also dont start migration as mshr is not clean, so deadlock??
    //      SOLUTION: changed onfly_search and do not erase request from wait list, process it in future
    ///////////////////////////////////////////////////////      
    // if it is hot/cold page and request type is WRITE:
    // case 1: hit in buffer with status 0/1. cache line is in buffer will be written to new destination
    //      no callback required, dont send to memory. 
    //      increment write count to buffer. erase it from wait list. continue with next while iteration, no count keeper
    // case 2: already sent to new destination meory's write q. status 2
    //      change target mem and send req accordingly... erase from wait list.. do count_keeper... 
    // case 3: not hit in buffer, will be brought in future by migc and also be cleaned
    //      this req is not in window and mshr. so we do not have to callback
    //      option i: do not erase it from wait list + dont send to mem. continue with next while iteration, do not count_keeper
    //      option ii: increment write count to buffer + dont send to memory. erase it from wait list. cont. with next while it., do not count_keeper
    //////////////////////////////////////////////////////

        //Mz: check if current request comes from hot/cold page before going into for loop
        // Will save simulation run time
        short r_type = -1;
        #ifdef MZ_DEBUG_ONFLY
            cout << "Inside demand check, clk: " << clk << "\n";
        #endif
        if(req.type == Request::Type::READ) {
            r_type = 1;     //READ
        }
        else {
            r_type = 2;    //WRITE
        }


        if( (candidate_pg_flag == 1) && (r_type == 1) )  // read and hot page
        {   // hot_buffer->bf_req[i]: 0 means just received mig req from mem, 
            // 1 means set_ready has been called for this mig req,
            // 2 means mig req is no more in buffer, already sent to W q of target memory
            for(int i = 0; i < hot_buffer->bf_req.size(); i++)
            {
                if(hot_buffer->bf_req[i].first == req.addr)
                {
                    if( (hot_buffer->bf_req[i].second == 0) || (hot_buffer->bf_req[i].second == 1) )
                    {
                        //case 1:
                        hma.onfly.total_buffer_hit_count++;
                        req.req_category = 5;   //modifed demand req. served by buffer, not memory. total delay is 3+2
                        req.callback(req);
                        return 1; //dont send to memory, erase from wait list, dont do count_keeper
                    }
                    else if (hot_buffer->bf_req[i].second == 2)
                    {
                        // case 2: 
                        req.phys_addr = hma.onfly.cold_frame_addr + page_offset;
                        req.req_target_mem = 0; // hit in hot buffer, new destination will be l1m
                        return 2; //send req accordingly, erase from wait list, do count_keeper.
                    }
                }   //if hit
            }   // for
            //case 3:
            //req.req_category = 4;   //modifed demand req. served by buffer, not memory. total delay is 3+2
            return 3;   // will come in future, keep for future processing, dont erase the req from wait list, dont do count keeper 
        }   // if candiadate
        else if( (candidate_pg_flag == 2) && (r_type == 1) )  // read and cold page
        {
            for(int i = 0; i < cold_buffer->bf_req.size(); i++)
            {
                if(cold_buffer->bf_req[i].first == req.addr)
                {
                    if( (cold_buffer->bf_req[i].second == 0) || (cold_buffer->bf_req[i].second == 1) )
                    {
                        //case 1:
                        hma.onfly.total_buffer_hit_count++;
                        req.req_category = 5;   //modifed demand req. served by buffer, not memory. total delay is 3+2
                        req.callback(req);
                        return 1; //dont send to memory, erase from wait list, dont do count_keeper
                    }
                    else if (cold_buffer->bf_req[i].second == 2)
                    {
                        // case 2: 
                        req.phys_addr = hma.onfly.hot_frame_addr + page_offset;
                        req.req_target_mem = 1; // hit in cold buffer, new destination will be l2m
                        return 2; //send req accordingly, erase from wait list, do count_keeper.
                    }
                }   //if hit
            }   // for
            //req.req_category = 4;   //modifed demand req. served by buffer, not memory. total delay is 3+2
            return 3;   // will come in future, keep for future processing, dont erase the req from wait list, dont do count keeper 
        }
        else if( (candidate_pg_flag == 1) && (r_type == 2) )  // write and hot page
        {
            for(int i = 0; i < hot_buffer->bf_req.size(); i++)
            {
                if(hot_buffer->bf_req[i].first == req.addr)
                {
                    if( (hot_buffer->bf_req[i].second == 0) || (hot_buffer->bf_req[i].second == 1) )
                    {
                        //case 1: increment write count to buffer. earse it from wait list. continue with next while iteration, no count keeper
                        //This is why we are adding 2 cycles of delay overhead in the whole mig process.
                        hma.onfly.total_buffer_hit_count++;
                        return 1; //dont send to memory, erase from wait list, dont do count_keeper
                    }
                    else if (hot_buffer->bf_req[i].second == 2)
                    {
                        // case 2: 
                        req.phys_addr = hma.onfly.cold_frame_addr + page_offset;
                        req.req_target_mem = 0; // hit in hot buffer, new destination will be l1m
                        return 2; //send req accordingly, erase from wait list, do count_keeper.
                    }
                }   //if hit
            }   // for
            //case 3:
            return 3;   //will be processed in future, do not erase it from wait list, dont send to mem. do not count_keeper
        }
        else if( (candidate_pg_flag == 2) && (r_type == 2) )  // write and cold page
        {
            for(int i = 0; i < cold_buffer->bf_req.size(); i++)
            {
                if(cold_buffer->bf_req[i].first == req.addr)
                {
                    if( (cold_buffer->bf_req[i].second == 0) || (cold_buffer->bf_req[i].second == 1) )
                    {
                        //case 1: increment write count to buffer. earse it from wait list. continue with next while iteration, no count keeper
                        //This is why we are adding 2 cycles of delay overhead in the whole mig process.
                        hma.onfly.total_buffer_hit_count++;
                        return 1; //dont send to memory, erase from wait list, dont do count_keeper
                    }
                    else if (cold_buffer->bf_req[i].second == 2)
                    {
                        // case 2: 
                        req.phys_addr = hma.onfly.hot_frame_addr + page_offset;
                        req.req_target_mem = 1; // hit in cold buffer, new destination will be l2m
                        return 2; //send req accordingly, erase from wait list, do count_keeper.
                    }
                }   //if hit
            }   // for
            //case 3:
            return 3;   //do not erase it from wait list, dont send to mem. do not count_keeper
        }

        return 0;

    }

	//Mz: for simple hybrid system with two types of memories and hma transer, onfly based (page structures are created)
	//MAIN
	void CacheSystem::hybrid_tick_hma_onfly() {
		debug("clk %ld", clk);
		++clk;
		
		static int64_t epoch_cycle_counter = 0;
		epoch_cycle_counter++;


        #ifdef MZ_DEBUG_HYBRID
		    cout << "at cache system tick, clk is : " << clk  << "\n";
        #endif
		if(clk == 2) {
			cout << "HMA page size: " << hma.config.page_size << " page transfer limit: " << hma.config.page_transfer_limit << "\n";
		}

		// Sends ready waiting request to memory
		auto it = wait_list.begin();
		while (it != wait_list.end() && clk >= it->first) 
		{
			//RAM_HMA: new part
			long req_addr = (it->second).addr;
			struct req_page *r;
			int64_t pagenum = req_addr/hma.config.page_size;
			HASH_FIND(r_hh, ptable, &pagenum, sizeof(int64_t), r);


			if(r == NULL)
			{
				// new page need to allocate
				r = hma_handler(&hma, req_addr, (it->second).coreid);
				if(r == NULL)
				{
					// if r is stiil NULL
					cout << "RUNNING OUT OF SYSTEM MEMORY\n" ;
					exit(0);
				}
			}
			// RAM_HMA: re-calculate the request's phys memory address depending on the HMA modified memory location
			int64_t page_offset = (req_addr % hma.config.page_size);    //save page offset bits here
			int64_t target_frame_base_addr = r->frame_addr; // since frame_addr is actual location of the starting of the frame, it contains page offset bit as 0
			int64_t target_phys_addr = target_frame_base_addr + page_offset;
			// Now update the req's physical address and target memory accordingly
			(it->second).phys_addr = target_phys_addr;	// L2M offset is already taken care
			(it->second).req_target_mem = r->dom;    // 0=l1m, 1=l2m, 2=l3m 

            //unique addresses
            it_unique = find(r->hma_stat.unique_addr.begin(), r->hma_stat.unique_addr.end(), req_addr);
            if(it_unique == r->hma_stat.unique_addr.end())
                r->hma_stat.unique_addr.push_back(req_addr);

            //measuring "migrated_hbm_access"
            if(r->dom == 0 && r->hma_stat.migrated_location == 1)
            {            
                migrated_hbm_access++;
                migrated_hbm_access_temp++;
            }
            
            //measuring remap MBQ
            for(int i=0; i<remap_tab.size(); i++){
               if(pagenum == remap_tab[i].first.first && r->dom == 0 && r->hma_stat.migrated_location == 1)
                {
                    remap_access++;
                } 
            }
        
            //pushing into threshold reach vector;

            /*if(r->hma_stat.mig_count == hma.config.thold)
            {
                int64_t temp_page = r->pnum;
                auto tr = find_if(hma.threshold_reach.begin(), hma.threshold_reach.end(), [&temp_page] (const pair<int64_t, int>& element) {return element.first == temp_page;});
                if(tr != hma.threshold_reach.end())
                    ;
                else
                    hma.threshold_reach.push_back(make_pair(r->pnum, hma.onfly.mig_status));
            }*/

            int state = 0;
            if( (hma.onfly.mig_status) && (hma.onfly.mig_status < 200)) {
                short candidate_pg_flag = 0;    // flag 0 means neither hot or cold, 1 means hot, 2 means cold
                if(hma.onfly.mig_type == 1)
                {
                    if(pagenum == hma.onfly.hot_pg_num)
                        candidate_pg_flag = 1;
                }
                else if(hma.onfly.mig_type == 2)
                {
                    if(pagenum == hma.onfly.hot_pg_num)
                        candidate_pg_flag = 1;
                    else if (pagenum == hma.onfly.cold_pg_num)
                        candidate_pg_flag = 2;
                }

                if(candidate_pg_flag) {
                    state = onfly_mig_demand_check(it->second, pagenum, page_offset, candidate_pg_flag); 
                }
            }
            
            if(state == 3) {
                ++it;
                continue;
            }
            else if ((state == 0) || (state == 2)) {    // normal program flow
                if((it->second).req_target_mem == 0) {
                    if (!send_memory(it->second)) {
                        ++it;
                    } else {
                        hma_onfly_count_keeper(&hma, r, clk, (it->second).req_target_mem);
                        debug("complete req: addr %lx", (it->second).addr);
                        #ifdef MZ_DEBUG_HYBRID
                        //cout << "at cache system tick, clk is : " << clk  << "\tsending to memory req with timestamp: " \\
                        //<< (it-:>first) << " addr: " << (it->second).addr <<"\n";
                        if( (it->second).type == Request::Type::READ)
                            cout << "Sending to L1m: " << (it->second).addr << " " << "R"<<"\n";
                        else
                            cout << "Sending to L1m: " << (it->second).addr << " " << "W"<<"\n";
                        #endif
                        it = wait_list.erase(it);	
                    }
                } //l1m
                else if((it->second).req_target_mem == 1)
                {
                    if (!send_memory_l2(it->second)) {
                        ++it;
                    } else {
                        hma_onfly_count_keeper(&hma, r, clk, (it->second).req_target_mem);
                        /*if( hma_onfly_count_keeper(&hma, r, clk, (it->second).req_target_mem) )
                        {
                            if(!hma.onfly.mig_status)
                            {
                            	hma.onfly.mig_status = hma_onfly_hotcold_check_and_init(&hma, r);   //currently there is no ongoing migration, so initiate one
                            }
                        }*/
                        debug("complete req: addr %lx", (it->second).addr);
                        #ifdef MZ_DEBUG_HYBRID
                        //cout << "at cache system tick, clk is : " << clk  << "\tsending to memory req with timestamp: " \\
                        //<< (it->first) << " addr: " << (it->second).addr <<"\n";
                        if( (it->second).type == Request::Type::READ)
                            cout << "Sending to L2m: " << (it->second).addr << " " << "R"<<"\n";
                        else
                            cout << "Sending to L2m: " << (it->second).addr << " " << "W"<<"\n";
                        #endif
                        it = wait_list.erase(it);	
                    }
                } //l2m
            } // state 0 or 2
            else if (state == 1) {
                it = wait_list.erase(it);	
            }
		}   // while

		// hit request callback
		it = hit_list.begin();
		while (it != hit_list.end()) {
			if (clk >= it->first) {
				it->second.callback(it->second);

				debug("finish hit: addr %lx", (it->second).addr);

				it = hit_list.erase(it);
			} else {
				++it;
			}
		}
	}//tick ends
} // namespace ramulator
