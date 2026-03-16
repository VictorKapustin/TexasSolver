#ifndef TEXASSOLVER_PCFRSOLVER_H
#define TEXASSOLVER_PCFRSOLVER_H

#include <ranges/PrivateCards.h>
#include <compairer/Compairer.h>
#include <Deck.h>
#include <ranges/RiverRangeManager.h>
#include <ranges/PrivateCardsManager.h>
#include <trainable/CfrPlusTrainable.h>
#include <trainable/DiscountedCfrTrainable.h>
#include <solver/Solver.h>
#include <tools/lookup8.h>
#include <tools/utils.h>
#include <json.hpp>
#include <omp.h>
#include <queue>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <type_traits>

using nlohmann::json;

class GameTree;
class GameTreeNode;
class ActionNode;
class TerminalNode;
class ShowdownNode;
class Card;

class PCfrSolver : public Solver {
public:
    struct BenchmarkThreadStats {
        uint64_t action_nodes = 0;
        uint64_t chance_nodes = 0;
        uint64_t showdown_nodes = 0;
        uint64_t terminal_nodes = 0;
        uint64_t strategy_fetch_ns = 0;
        uint64_t regret_update_ns = 0;
        uint64_t ev_update_ns = 0;
        uint64_t chance_setup_ns = 0;
        uint64_t chance_merge_ns = 0;
        uint64_t showdown_ns = 0;
        uint64_t terminal_ns = 0;
        uint64_t allocator_ns = 0;
        uint64_t allocator_calls = 0;
        uint64_t allocator_bytes = 0;
    };

    struct ThreadScratchBuffer {
        enum : size_t {
            kInitialFloatCapacity = 512 * 1024,
            kInitialIntCapacity = 64 * 1024
        };

        struct FloatBlock {
            std::unique_ptr<float[]> data;
            size_t capacity = 0;
            size_t used = 0;

            explicit FloatBlock(size_t block_capacity)
                    : data(std::make_unique<float[]>(block_capacity)),
                      capacity(block_capacity) {
            }
        };

        struct IntBlock {
            std::unique_ptr<int[]> data;
            size_t capacity = 0;
            size_t used = 0;

            explicit IntBlock(size_t block_capacity)
                    : data(std::make_unique<int[]>(block_capacity)),
                      capacity(block_capacity) {
            }
        };

        struct FloatAllocation {
            size_t block_index = 0;
            size_t previous_used = 0;
            size_t size = 0;
        };

        struct IntAllocation {
            size_t block_index = 0;
            size_t previous_used = 0;
            size_t size = 0;
        };

        std::vector<FloatBlock> float_blocks;
        std::vector<FloatAllocation> float_allocations;
        std::vector<IntBlock> int_blocks;
        std::vector<IntAllocation> int_allocations;

        uint64_t allocator_ns = 0;
        uint64_t allocator_calls = 0;
        uint64_t allocator_bytes = 0;

        template<typename BlockVector, typename BlockType>
        void ensure_block_capacity(BlockVector& blocks, size_t size) {
            if(blocks.empty() || (blocks.back().capacity - blocks.back().used) < size) {
                const uint64_t allocation_start = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                const size_t default_capacity = std::is_same<BlockType, FloatBlock>::value
                        ? static_cast<size_t>(kInitialFloatCapacity)
                        : static_cast<size_t>(kInitialIntCapacity);
                const size_t new_capacity = std::max(size, default_capacity);
                blocks.emplace_back(new_capacity);
                const uint64_t allocation_end = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                allocator_calls += 1;
                allocator_bytes += new_capacity * sizeof(typename std::conditional<std::is_same<BlockType, FloatBlock>::value, float, int>::type);
                allocator_ns += (allocation_end - allocation_start);
            }
        }

        ThreadScratchBuffer() {
            float_blocks.emplace_back(kInitialFloatCapacity);
            int_blocks.emplace_back(kInitialIntCapacity);
            float_allocations.reserve(1024);
            int_allocations.reserve(1024);
        }

        float* push_floats(size_t size) {
            ensure_block_capacity<std::vector<FloatBlock>, FloatBlock>(float_blocks, size);
            FloatBlock& block = float_blocks.back();
            const size_t previous_used = block.used;
            block.used += size;
            float_allocations.push_back(FloatAllocation{float_blocks.size() - 1, previous_used, size});
            return block.data.get() + previous_used;
        }
        void pop_floats(size_t size) {
#ifdef DEBUG
            if(float_allocations.empty()) throw runtime_error("float scratch underflow");
            if(float_allocations.back().size != size) throw runtime_error("float scratch pop size mismatch");
#endif
            const FloatAllocation allocation = float_allocations.back();
            float_allocations.pop_back();
#ifdef DEBUG
            if(allocation.block_index >= float_blocks.size()) throw runtime_error("float scratch block mismatch");
#endif
            float_blocks[allocation.block_index].used = allocation.previous_used;
        }

        int* push_ints(size_t size) {
            ensure_block_capacity<std::vector<IntBlock>, IntBlock>(int_blocks, size);
            IntBlock& block = int_blocks.back();
            const size_t previous_used = block.used;
            block.used += size;
            int_allocations.push_back(IntAllocation{int_blocks.size() - 1, previous_used, size});
            return block.data.get() + previous_used;
        }
        void pop_ints(size_t size) {
#ifdef DEBUG
            if(int_allocations.empty()) throw runtime_error("int scratch underflow");
            if(int_allocations.back().size != size) throw runtime_error("int scratch pop size mismatch");
#endif
            const IntAllocation allocation = int_allocations.back();
            int_allocations.pop_back();
#ifdef DEBUG
            if(allocation.block_index >= int_blocks.size()) throw runtime_error("int scratch block mismatch");
#endif
            int_blocks[allocation.block_index].used = allocation.previous_used;
        }

        void resetAllocatorStats() {
            allocator_ns = 0;
            allocator_calls = 0;
            allocator_bytes = 0;
        }

        bool empty() const {
            return float_allocations.empty() && int_allocations.empty();
        }
    };

    PCfrSolver(std::shared_ptr<GameTree> tree,
            std::vector<PrivateCards> range1 ,
            std::vector<PrivateCards> range2,
            std::vector<int> initial_board,
            std::shared_ptr<Compairer> compairer,
            Deck deck,
            int iteration_number,
            bool debug,
            int print_interval,
            std::string logfile,
            std::string trainer,
            Solver::MonteCarolAlg monteCarolAlg,
            int warmup,
            float accuracy,
            bool use_isomorphism,
            int use_halffloats,
            int num_threads,
            bool profile_enabled = false
    );
    ~PCfrSolver();
    void train() override;
    void stop() override;
    json dumps(bool with_status,int depth) override;
    std::vector<std::vector<std::vector<float>>> get_strategy(std::shared_ptr<ActionNode> node,std::vector<Card> chance_cards) override;
    std::vector<std::vector<std::vector<float>>> get_evs(std::shared_ptr<ActionNode> node,std::vector<Card> chance_cards) override;
private:
    std::vector<std::vector<PrivateCards>> ranges;
    std::vector<PrivateCards> range1;
    std::vector<PrivateCards> range2;
    std::vector<int> initial_board;
    uint64_t initial_board_long;
    int iteration_number;
    int player_number;
    bool debug;
    int print_interval;
    std::string logfile;
    bool nowstop = false;
    std::string trainer;
    int warmup;
    float accuracy;
    bool use_isomorphism;
    int use_halffloats;
    int num_threads;
    bool distributing_task = false;
    bool collecting_statics = false;

    PrivateCardsManager pcm;
    RiverRangeManager rrm;
    Deck deck;

    GameTreeNode::GameRound root_round;
    GameTreeNode::GameRound split_round;

    std::shared_ptr<Compairer> compairer;
    Solver::MonteCarolAlg monteCarolAlg;
    bool statics_collected = false;

    std::vector<int> round_deal;

    static constexpr int kColorIsoTableSize = 52 * 52 * 2;
    int color_iso_offset[kColorIsoTableSize][4] = {0};

    bool profile_enabled = false;
    std::vector<BenchmarkThreadStats> benchmark_thread_stats;
    std::vector<std::shared_ptr<ThreadScratchBuffer>> thread_scratch_buffers;

    void setTrainable(std::shared_ptr<GameTreeNode> root);
    std::vector<int> getAllAbstractionDeal(int deal);
    
    void cfr(int player, std::shared_ptr<GameTreeNode> node, const float* reach_probs, int iter,
            uint64_t current_board, int deal, float* result);

    void chanceUtility(int player, std::shared_ptr<ChanceNode> node, const float* reach_probs, int iter,
                     uint64_t current_board, int deal, float* result);

    void actionUtility(int player, std::shared_ptr<ActionNode> node, const float* reach_probs, int iter,
                     uint64_t current_board, int deal, float* result);

    void showdownUtility(int player, std::shared_ptr<ShowdownNode> node, const float* reach_probs,
                       int iter, uint64_t current_board, int deal, float* result);

    void terminalUtility(int player, std::shared_ptr<TerminalNode> node, const float* reach_probs,
                       int iter, uint64_t current_board, int deal, float* result);

    void findGameSpecificIsomorphisms();
    void purnTree();

    BenchmarkThreadStats& currentBenchmarkThreadStats();
    ThreadScratchBuffer& currentThreadScratchBuffer();
    void resetBenchmarkThreadStats();
    json collectBenchmarkStatsJson() const;

    const std::vector<PrivateCards>& playerHands(int player);
    std::vector<std::vector<float>> getReachProbs();
    
    std::vector<PrivateCards> noDuplicateRange(const std::vector<PrivateCards>& private_range, uint64_t board_long);
    void exchangeRange(json& jo, int suit1, int suit2, std::shared_ptr<ActionNode> node);
    void reConvertJson(const std::shared_ptr<GameTreeNode>& node, json& strategy, std::string key, int depth, int max_depth, std::vector<std::string> prefix, int deal, std::vector<std::vector<int>> exchange_color_list);

    inline void addAllocatorSample(BenchmarkThreadStats* stats, uint64_t bytes, uint64_t ns) {
        if (stats) {
            stats->allocator_calls++;
            stats->allocator_bytes += bytes;
            stats->allocator_ns += ns;
        }
    }
};

#endif //TEXASSOLVER_PCFRSOLVER_H
