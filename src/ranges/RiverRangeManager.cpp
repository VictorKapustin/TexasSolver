//
// Created by Xuefeng Huang on 2020/1/31.
//

#include "include/ranges/RiverRangeManager.h"

#include <utility>
#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include "include/tools/tinyformat.h"
#include "include/compairer/Compairer.h"
#include "Card.h"

using std::shared_ptr;
using std::vector;
using std::unordered_map;
using std::make_shared;
using std::runtime_error;

namespace {
uint64_t steadyNowNs() {
    return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
            ).count()
    );
}
}

RiverRangeManager::RiverRangeManager() {
}

RiverRangeManager::RiverRangeManager(shared_ptr<Compairer> handEvaluator) {
    this->handEvaluator = std::move(handEvaluator);
}

RiverRangeManager::~RiverRangeManager() {
}

void RiverRangeManager::resetStats() {
    this->cache_lookups.store(0, std::memory_order_relaxed);
    this->cache_hits.store(0, std::memory_order_relaxed);
    this->cache_misses.store(0, std::memory_order_relaxed);
    this->cache_builds.store(0, std::memory_order_relaxed);
    this->cache_lookup_ns.store(0, std::memory_order_relaxed);
    this->cache_build_ns.store(0, std::memory_order_relaxed);
    this->cache_lock_wait_ns.store(0, std::memory_order_relaxed);
}

RiverRangeManager::CacheStats RiverRangeManager::getStats() const {
    CacheStats stats;
    stats.lookups = this->cache_lookups.load(std::memory_order_relaxed);
    stats.hits = this->cache_hits.load(std::memory_order_relaxed);
    stats.misses = this->cache_misses.load(std::memory_order_relaxed);
    stats.builds = this->cache_builds.load(std::memory_order_relaxed);
    stats.lookup_ns = this->cache_lookup_ns.load(std::memory_order_relaxed);
    stats.build_ns = this->cache_build_ns.load(std::memory_order_relaxed);
    stats.lock_wait_ns = this->cache_lock_wait_ns.load(std::memory_order_relaxed);
    
    for (int i = 0; i < RiverRangeManager::NUM_SHARDS; ++i) {
        std::lock_guard<std::mutex> lock1(this->p1_shards[i].lock);
        stats.p1_entries += this->p1_shards[i].map.size();
        std::lock_guard<std::mutex> lock2(this->p2_shards[i].lock);
        stats.p2_entries += this->p2_shards[i].map.size();
    }
    return stats;
}


const vector<RiverCombs> &
RiverRangeManager::getRiverCombos(int player, const vector<PrivateCards> &riverCombos, const vector<int> &board) {
    uint64_t board_long = Card::boardInts2long(board);
    return this->getRiverCombos(player,riverCombos,board_long);
}

const vector<RiverCombs> &
RiverRangeManager::getRiverCombos(int player, const vector<PrivateCards> &preflopCombos, uint64_t board_long) {
    RiverRangeManager::Shard* selected_shards;
    if (player == 0) {
        selected_shards = this->p1_shards;
    } else if (player == 1) {
        selected_shards = this->p2_shards;
    } else
        throw runtime_error(tfm::format("player %s not found",player));

    uint64_t key = board_long;
    // Simple mixing for better distribution of bitmasks
    uint32_t h = static_cast<uint32_t>(key ^ (key >> 32));
    h ^= (h >> 16);
    RiverRangeManager::Shard& shard = selected_shards[h % RiverRangeManager::NUM_SHARDS];

    uint64_t lookup_start = steadyNowNs();
    this->cache_lookups.fetch_add(1, std::memory_order_relaxed);

    {
        uint64_t lock_wait_start = steadyNowNs();
        std::lock_guard<std::mutex> lock(shard.lock);
        this->cache_lock_wait_ns.fetch_add(steadyNowNs() - lock_wait_start, std::memory_order_relaxed);
        auto lookup_it = shard.map.find(key);
        if (lookup_it != shard.map.end()) {
            const vector<RiverCombs> &retval = *(lookup_it->second);
            this->cache_hits.fetch_add(1, std::memory_order_relaxed);
            this->cache_lookup_ns.fetch_add(steadyNowNs() - lookup_start, std::memory_order_relaxed);
            return retval;
        }
    }
    this->cache_misses.fetch_add(1, std::memory_order_relaxed);

    uint64_t build_start = steadyNowNs();
    int count = 0;
    for (auto one_hand : preflopCombos) {
        if (!Card::boardsHasIntercept(one_hand.toBoardLong(), board_long))
            count++;
    }

    int index = 0;
    vector<RiverCombs> riverCombos = vector<RiverCombs>(count);
    for (std::size_t hand = 0; hand < preflopCombos.size(); hand++)
    {
        PrivateCards preflopCombo = preflopCombos[hand];
        if (Card::boardsHasIntercept(preflopCombo.toBoardLong(), board_long)){
            continue;
        }

        int rank = this->handEvaluator->get_rank(preflopCombo.toBoardLong(),board_long);
        RiverCombs riverCombo = RiverCombs(preflopCombo, rank, hand);
        riverCombos[index++] = riverCombo;
    }

    std::sort(riverCombos.begin(),riverCombos.end(),[ ]( const RiverCombs& lhs, const RiverCombs& rhs )
    {
        return lhs.rank > rhs.rank;
    });
    this->cache_builds.fetch_add(1, std::memory_order_relaxed);
    this->cache_build_ns.fetch_add(steadyNowNs() - build_start, std::memory_order_relaxed);

    uint64_t lock_wait_start = steadyNowNs();
    std::lock_guard<std::mutex> lock(shard.lock);
    this->cache_lock_wait_ns.fetch_add(steadyNowNs() - lock_wait_start, std::memory_order_relaxed);
    auto existing_it = shard.map.find(key);
    shared_ptr<vector<RiverCombs>> stored_river_combos;
    if(existing_it != shard.map.end()) {
        stored_river_combos = existing_it->second;
    } else {
        stored_river_combos = make_shared<vector<RiverCombs>>(std::move(riverCombos));
        shard.map[key] = stored_river_combos;
    }
    const vector<RiverCombs>& retval = *stored_river_combos;
    this->cache_lookup_ns.fetch_add(steadyNowNs() - lookup_start, std::memory_order_relaxed);

    return retval;
}
