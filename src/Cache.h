#ifndef __CACHE_H
#define __CACHE_H

#include "Config.h"
#include "Request.h"
#include "Statistics.h"
#include "hma.h"
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <list>

using namespace std;

namespace ramulator
{
    class CacheSystem;

    class Cache {
        protected:
            ScalarStat cache_read_miss;
            ScalarStat cache_write_miss;
            ScalarStat cache_total_miss;
            ScalarStat cache_eviction;
            ScalarStat cache_read_access;
            ScalarStat cache_write_access;
            ScalarStat cache_total_access;
            ScalarStat cache_mshr_hit;
            ScalarStat cache_mshr_unavailable;
            ScalarStat cache_set_unavailable;
        public:
            /*//original code:
              enum class Level {
              L1,
              L2,
              L3,
              MAX
              } level;
              std::string level_string;
              */
            // Mz: 2-lelvl cache
            enum class Level {
                L1,
                L3,
                MAX
            } level;
            std::string level_string;

            struct Line {
                long addr;
                long tag;
                bool lock; // When the lock is on, the value is not valid yet.
                bool dirty;
                Line(long addr, long tag):
                    addr(addr), tag(tag), lock(true), dirty(false) {}
                Line(long addr, long tag, bool lock, bool dirty):
                    addr(addr), tag(tag), lock(lock), dirty(dirty) {}
            };

            Cache(int size, int assoc, int block_size, int mshr_entry_num,
                    Level level, std::shared_ptr<CacheSystem> cachesys);

            // Old L1: 32KB, assoc 8, L2: 256KB, assoc 8, L3: 8MB, assoc 8
            // Old L1, L2, L3 accumulated latencies
            //int latency[int(Level::MAX)] = {4, 4 + 12, 4 + 12 + 31};
            //int latency_each[int(Level::MAX)] = {4, 12, 31};

            // And ramualtor assume 1 cycle for non-load inst, so even we do not model I cache it is fine
            // New: changing latency values accordingly, L1: 2, L2: 11
            // For 8MB shared L2 11 cycles, for 16MB shared L2 21 cycles (CACTI)
            int latency[int(Level::MAX)] = {2, 2 + 21};
            int latency_each[int(Level::MAX)] = {2, 21};

            std::shared_ptr<CacheSystem> cachesys;
            // LLC has multiple higher caches
            std::vector<Cache*> higher_cache;
            Cache* lower_cache;

            bool send(Request req);
            bool send_invalidate(long inv_addr);

            void concatlower(Cache* lower);

            void callback(Request& req);
            int callback_migration(long r_addr, int core_id, int count);


            // Align the address to have only page number (which does not have last 11/12 bits)
            long align_to_page_number(long addr);
            int onfly_search(long page_number);
            int onfly_search_set_init(long page_number);

        protected:

            bool is_first_level;
            bool is_last_level;
            size_t size;
            unsigned int assoc;
            unsigned int block_num;
            unsigned int index_mask;
            unsigned int block_size;
            unsigned int index_offset;
            unsigned int tag_offset;
            unsigned int mshr_entry_num;
            //std::vector<std::pair<long, std::list<Line>::iterator>> mshr_entries;
            std::vector<std::pair< std::pair<long, std::list<Line>::iterator> , std::pair<short, short> > > mshr_entries;   //new pair

            std::map<int, std::list<Line> > cache_lines;

            int calc_log2(int val) {
                int n = 0;
                while ((val >>= 1))
                    n ++;
                return n;
            }

            int get_index(long addr) {
                //index_mask = 127;
                //cout<<"addr:"<<addr<<"\n";
                //cout<<"index_offset:"<<index_offset<<"\n";
                //cout<<"index_mask:"<<index_mask<<"\n";
                //cout<<"idx:"<<((addr >> index_offset) & index_mask)<<"\n";
                return (addr >> index_offset) & index_mask;
            };

            long get_tag(long addr) {
                //tag_offset = 13;
                //cout<<"addr:"<<addr<<"\n";
                //cout<<"tag_off:"<<tag_offset<<"\n";
                //cout<<"get tag:"<<(addr >> tag_offset)<<"\n";
                return (addr >> tag_offset);
            }

            // Align the address to cache line size
            long align(long addr) {
                return (addr & ~(block_size-1l));
            }

            // Evict the cache line from higher level to this level.
            // Pass the dirty bit and update LRU queue.
            void evictline(long addr, bool dirty);

            // Invalidate the line from this level to higher levels
            // The return value is a pair. The first element is invalidation
            // latency, and the second is wether the value has new version
            // in higher level and this level.
            std::pair<long, bool> invalidate(long addr);

            // Evict the victim from current set of lines.
            // First do invalidation, then call evictline(L1 or L2) or send
            // a write request to memory(L3) when dirty bit is on.
            void evict(std::list<Line>* lines,
                    std::list<Line>::iterator victim);

            // First test whether need eviction, if so, do eviction by
            // calling evict function. Then allocate a new line and return
            // the iterator points to it.
            std::list<Line>::iterator allocate_line(
                    std::list<Line>& lines, long addr);

            // Check whether the set to hold addr has space or eviction is
            // needed.
            bool need_eviction(const std::list<Line>& lines, long addr);

            // Check whether this addr is hit and fill in the pos_ptr with
            // the iterator to the hit line or lines.end()
            bool is_hit(std::list<Line>& lines, long addr,
                    std::list<Line>::iterator* pos_ptr);

            bool all_sets_locked(const std::list<Line>& lines) {
                if (lines.size() < assoc) {
                    return false;
                }
                for (const auto& line : lines) {
                    if (!line.lock) {
                        return false;
                    }
                }
                return true;
            }

            bool check_unlock(long addr) {
                auto it = cache_lines.find(get_index(addr));
                if (it == cache_lines.end()) {
                    return true;
                } else {
                    auto& lines = it->second;
                    auto line = find_if(lines.begin(), lines.end(),
                            [addr, this](Line l){return (l.tag == get_tag(addr));});
                    if (line == lines.end()) {
                        return true;
                    } else {
                        bool check = !line->lock;
                        if (!is_first_level) {
                            for (auto hc : higher_cache) {
                                if (!check) {
                                    return check;
                                }
                                check = check && hc->check_unlock(line->addr);
                            }
                        }
                        return check;
                    }
                }
            }

            std::vector<std::pair< std::pair<long, std::list<Line>::iterator> , std::pair<short, short> >>::iterator
                hit_mshr(long addr) {
                    auto mshr_it =
                        find_if(mshr_entries.begin(), mshr_entries.end(),
                                [addr, this](std::pair< std::pair<long, std::list<Line>::iterator> , std::pair<short, short> >
                                    mshr_entry) {
                                return (align(mshr_entry.first.first) == align(addr));
                                });
                    return mshr_it;
                }

            std::list<Line>& get_lines(long addr) {
                //cout<<"get lines\n";
                /*std::map<int, std::list<Line> >::iterator it;
                for(it=cache_lines.begin(); it!=cache_lines.end(); it++)
                {
                    for(int i=0; i<(it->second).size(); i++)
                    {
                        cout<<"cl:"<<it->first<<" "<<(it->second)[i].addr<<"\n";
                    }
                }*/
                if (cache_lines.find(get_index(addr))
                        == cache_lines.end()) {
                    //cout<<"entered\n";
                    cache_lines.insert(make_pair(get_index(addr),
                                std::list<Line>()));
                }
                return cache_lines[get_index(addr)];
            }
            /////////// Mz: function to check if the set exists or not, do not create a new set 
            bool check_lines(long addr) {
                if (cache_lines.find(get_index(addr))
                        == cache_lines.end()) {
                    return false; //no such set exists, which means that the cache line is not there
                }
                return true; 
            }
    };

    class CacheSystem {
        public:
            //Mz onfly params
            uint64_t row_shift;
            Buffer* hot_buffer;
            Buffer* cold_buffer;  
            vector<  pair< pair<int64_t, int64_t>, pair<bool, int64_t> >  > remap_tab; 
            CacheSystem(const Config& configs, std::function<bool(Request)> send_memory):
                send_memory(send_memory) {
                    if (configs.has_core_caches()) {
                        first_level = Cache::Level::L1;
                    } else if (configs.has_l3_cache()) {
                        first_level = Cache::Level::L3;
                    } else {
                        last_level = Cache::Level::MAX; // no cache
                    }

                    /* // original code:
                       if (configs.has_l3_cache()) {
                       last_level = Cache::Level::L3;
                       } else if (configs.has_core_caches()) {
                       last_level = Cache::Level::L2;
                       } else {
                       last_level = Cache::Level::MAX; // no cache
                       }*/
                    if (configs.has_l3_cache()) {
                        last_level = Cache::Level::L3;
                    } else if (configs.has_core_caches()) {
                        last_level = Cache::Level::L1;			// this case should not arise since we must always have L1 and L3, but incase
                    } else {
                        last_level = Cache::Level::MAX; // no cache
                    }
                }

            CacheSystem(const Config& configs, const Config& configs_l2, std::function<bool(Request)> send_memory, std::function<bool(Request)> send_memory_l2):
                send_memory(send_memory), send_memory_l2(send_memory_l2) {
                    if (configs.has_core_caches()) {
                        first_level = Cache::Level::L1;
                    } else if (configs.has_l3_cache()) {
                        first_level = Cache::Level::L3;
                    } else {
                        last_level = Cache::Level::MAX; // no cache
                    }
                    if (configs.has_l3_cache()) {
                        last_level = Cache::Level::L3;
                    } else if (configs.has_core_caches()) {
                        last_level = Cache::Level::L1;			// this case should not arise since we must always have L1 and L3, but incase
                    } else {
                        last_level = Cache::Level::MAX; // no cache
                    }

                    if(configs["trace_type"] == "HMA_ONFLY")
                    {
                        if(hma.config.page_size == 2048)
                            row_shift = 11; 
                        else if(hma.config.page_size == 4096)
                            row_shift = 12; 
                    }
                    else row_shift = 11;
                }

            // wait_list contains miss requests with their latencies in
            // cache. When this latency is met, the send_memory function
            // will be called to send the request to the memory system.
            std::list<std::pair<long, Request> > wait_list;

            // hit_list contains hit requests with their latencies in cache.
            // callback function will be called when this latency is met and
            // set the instruction status to ready in processor's window.
            std::list<std::pair<long, Request> > hit_list;
            
            //std::vector<pair<int64_t, int>> threshold_reach;    //this vector keeps track of all pages crossed the threshold, and the migration status at that time.

            std::function<bool(Request)> send_memory;
            std::function<bool(Request)> send_memory_l2;


            long clk = 0;
            void tick();
            void hybrid_tick();
            void hybrid_tick_hma_epoch();
            void hybrid_tick_hma_onfly();
            void set_buffers(Buffer* a, Buffer* b);
            int onfly_mig_demand_check(Request &req, int64_t pagenum, int64_t page_offset, short candidate_pg_flag);
            Cache::Level first_level;
            Cache::Level last_level;

            //iterator
            std::vector<int64_t>::iterator it_unique;
            
            //stats
            int64_t migrated_hbm_access = 0;
            int64_t migrated_hbm_access_temp = 0;       //reset for every interval
            int64_t remap_access = 0;
    };

} // namespace ramulator

#endif /* __CACHE_H */
