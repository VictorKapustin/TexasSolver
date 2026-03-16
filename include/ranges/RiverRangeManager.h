//
// Created by Xuefeng Huang on 2020/1/31.
//

#ifndef TEXASSOLVER_RIVERRANGEMANAGER_H
#define TEXASSOLVER_RIVERRANGEMANAGER_H

#include "RiverCombs.h"
#include <unordered_map>
#include <include/compairer/Compairer.h>
#include <include/compairer/Dic5Compairer.h>
#include <mutex>
#include <memory>
#include <atomic>
#include <cstdint>

class RiverRangeManager {
public:
    struct CacheStats {
        uint64_t lookups = 0;
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t builds = 0;
        uint64_t lookup_ns = 0;
        uint64_t build_ns = 0;
        uint64_t lock_wait_ns = 0;
        uint64_t p1_entries = 0;
        uint64_t p2_entries = 0;
    };

    RiverRangeManager();
    RiverRangeManager(shared_ptr<Compairer> handEvaluator);
    const vector<RiverCombs>& getRiverCombos(int player, const vector<PrivateCards>& riverCombos, const vector<int>& board);
    const vector<RiverCombs>& getRiverCombos(int player, const vector<PrivateCards>& riverCombos, uint64_t board_long);
    void resetStats();
    CacheStats getStats() const;
private:
    unordered_map<uint64_t , shared_ptr<vector<RiverCombs>>> p1RiverRanges;
    unordered_map<uint64_t , shared_ptr<vector<RiverCombs>>> p2RiverRanges;
    shared_ptr<Compairer> handEvaluator;
    mutable mutex p1_maplock;
    mutable mutex p2_maplock;
    std::atomic<uint64_t> cache_lookups{0};
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> cache_builds{0};
    std::atomic<uint64_t> cache_lookup_ns{0};
    std::atomic<uint64_t> cache_build_ns{0};
    std::atomic<uint64_t> cache_lock_wait_ns{0};
};


#endif //TEXASSOLVER_RIVERRANGEMANAGER_H
