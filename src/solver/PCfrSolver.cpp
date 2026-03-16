//
// Created by Xuefeng Huang on 2020/1/31.
//

#include <solver/BestResponse.h>
#include <solver/PCfrSolver.h>
#include <QtCore>
#include <QObject>
#include <QTranslator>
#include <chrono>
#include <cmath>
#include <cstdint>

//#define DEBUG;

namespace {
uint64_t steadyNowNs() {
    return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
            ).count()
    );
}

double nsToMs(uint64_t ns) {
    return static_cast<double>(ns) / 1000000.0;
}
} // namespace

PCfrSolver::~PCfrSolver(){
    //cout << "Pcfr destroyed" << endl;
}

PCfrSolver::PCfrSolver(shared_ptr<GameTree> tree, vector<PrivateCards> range1, vector<PrivateCards> range2,
                     vector<int> initial_board, shared_ptr<Compairer> compairer, Deck deck, int iteration_number, bool debug,
                     int print_interval, string logfile, string trainer, Solver::MonteCarolAlg monteCarolAlg,int warmup,
                     float accuracy,bool use_isomorphism,int use_halffloats,int num_threads,bool profile_enabled,
                     bool task_parallelism)
                     :Solver(tree), rrm(compairer){
    this->initial_board = initial_board;
    this->initial_board_long = Card::boardInts2long(initial_board);
    this->logfile = logfile;
    this->trainer = trainer;
    this->warmup = warmup;

    range1 = this->noDuplicateRange(range1,initial_board_long);
    range2 = this->noDuplicateRange(range2,initial_board_long);

    this->range1 = range1;
    this->range2 = range2;
    this->player_number = 2;
    this->ranges = vector<vector<PrivateCards>>(this->player_number);
    this->ranges[0] = range1;
    this->ranges[1] = range2;

    this->compairer = compairer;

    this->deck = deck;
    this->use_isomorphism = use_isomorphism;
    this->use_halffloats = use_halffloats;

    this->iteration_number = iteration_number;

    vector<vector<PrivateCards>> private_cards(this->player_number);
    private_cards[0] = range1;
    private_cards[1] = range2;
    pcm = PrivateCardsManager(private_cards,this->player_number,Card::boardInts2long(this->initial_board));
    this->debug = debug;
    this->print_interval = print_interval;
    this->monteCarolAlg = monteCarolAlg;
    this->accuracy = accuracy;
    this->profile_enabled = profile_enabled;
    this->task_parallelism = task_parallelism;
    if(num_threads == -1){
        num_threads = omp_get_num_procs();
    }
    qDebug().noquote() << QString::fromStdString(tfm::format(QObject::tr("Using %s threads").toStdString().c_str(),num_threads));
    this->num_threads = num_threads;
    this->distributing_task = false;
    omp_set_num_threads(this->num_threads);
    this->benchmark_thread_stats = vector<BenchmarkThreadStats>(this->num_threads > 0 ? this->num_threads : 1);
    this->thread_scratch_buffers = std::vector<std::shared_ptr<ThreadScratchBuffer>>(this->num_threads > 0 ? this->num_threads : 1);
    for(std::size_t i = 0; i < this->thread_scratch_buffers.size(); ++i) {
        this->thread_scratch_buffers[i] = std::make_shared<ThreadScratchBuffer>();
    }

    setTrainable(this->tree->getRoot());
    this->root_round = this->tree->getRoot()->getRound();
    if(this->root_round == GameTreeNode::GameRound::PREFLOP){
        this->split_round = GameTreeNode::GameRound::FLOP;
    }else if(this->root_round == GameTreeNode::GameRound::FLOP){
        this->split_round = GameTreeNode::GameRound::TURN;
    }else if(this->root_round == GameTreeNode::GameRound::TURN){
        this->split_round = GameTreeNode::GameRound::RIVER;
    }else{
        // do not use multithread in river, really not necessary
        this->split_round = GameTreeNode::GameRound::PREFLOP;
    }
}

PCfrSolver::BenchmarkThreadStats& PCfrSolver::currentBenchmarkThreadStats() {
    return this->benchmark_thread_stats[omp_in_parallel() ? omp_get_thread_num() : 0];
}

PCfrSolver::ThreadScratchBuffer& PCfrSolver::currentThreadScratchBuffer() {
    std::size_t thread_index = omp_in_parallel() ? static_cast<std::size_t>(omp_get_thread_num()) : 0;
    if(thread_index >= this->thread_scratch_buffers.size()) {
        thread_index = 0;
    }
    return *(this->thread_scratch_buffers[thread_index]);
}

void PCfrSolver::resetBenchmarkThreadStats() {
    for(BenchmarkThreadStats& one_stats : this->benchmark_thread_stats){
        one_stats = BenchmarkThreadStats();
    }
    for(const std::shared_ptr<ThreadScratchBuffer>& scratch : this->thread_scratch_buffers) {
        scratch->resetAllocatorStats();
    }
}

json PCfrSolver::collectBenchmarkStatsJson() const {
    BenchmarkThreadStats total;
    for(const BenchmarkThreadStats& one_stats : this->benchmark_thread_stats){
        total.action_nodes += one_stats.action_nodes;
        total.chance_nodes += one_stats.chance_nodes;
        total.showdown_nodes += one_stats.showdown_nodes;
        total.terminal_nodes += one_stats.terminal_nodes;
        total.strategy_fetch_ns += one_stats.strategy_fetch_ns;
        total.regret_update_ns += one_stats.regret_update_ns;
        total.ev_update_ns += one_stats.ev_update_ns;
        total.chance_setup_ns += one_stats.chance_setup_ns;
        total.chance_merge_ns += one_stats.chance_merge_ns;
        total.showdown_ns += one_stats.showdown_ns;
        total.terminal_ns += one_stats.terminal_ns;
        total.allocator_ns += one_stats.allocator_ns;
        total.allocator_calls += one_stats.allocator_calls;
        total.allocator_bytes += one_stats.allocator_bytes;
    }
    for(const std::shared_ptr<ThreadScratchBuffer>& scratch : this->thread_scratch_buffers) {
        total.allocator_ns += scratch->allocator_ns;
        total.allocator_calls += scratch->allocator_calls;
        total.allocator_bytes += scratch->allocator_bytes;
    }

    RiverRangeManager::CacheStats river_cache_stats = this->rrm.getStats();
    double river_cache_hit_rate = 0.0;
    if(river_cache_stats.lookups != 0){
        river_cache_hit_rate = static_cast<double>(river_cache_stats.hits) / static_cast<double>(river_cache_stats.lookups);
    }

    json retval;
    retval["node_counts"] = {
            {"action", total.action_nodes},
            {"chance", total.chance_nodes},
            {"showdown", total.showdown_nodes},
            {"terminal", total.terminal_nodes}
    };
    retval["timings_ms"] = {
            {"strategy_fetch", nsToMs(total.strategy_fetch_ns)},
            {"regret_update", nsToMs(total.regret_update_ns)},
            {"ev_update", nsToMs(total.ev_update_ns)},
            {"chance_setup", nsToMs(total.chance_setup_ns)},
            {"chance_merge", nsToMs(total.chance_merge_ns)},
            {"showdown_eval", nsToMs(total.showdown_ns)},
            {"terminal_eval", nsToMs(total.terminal_ns)},
            {"allocator", nsToMs(total.allocator_ns)}
    };
    retval["allocator_profile"] = {
            {"calls", total.allocator_calls},
            {"bytes", total.allocator_bytes},
            {"megabytes", static_cast<double>(total.allocator_bytes) / (1024.0 * 1024.0)}
    };
    retval["river_cache"] = {
            {"lookups", river_cache_stats.lookups},
            {"hits", river_cache_stats.hits},
            {"misses", river_cache_stats.misses},
            {"builds", river_cache_stats.builds},
            {"hit_rate", river_cache_hit_rate},
            {"lookup_ms", nsToMs(river_cache_stats.lookup_ns)},
            {"build_ms", nsToMs(river_cache_stats.build_ns)},
            {"lock_wait_ms", nsToMs(river_cache_stats.lock_wait_ns)},
            {"entries", {
                    {"player0", river_cache_stats.p1_entries},
                    {"player1", river_cache_stats.p2_entries}
            }}
    };
    return retval;
}

bool PCfrSolver::canUseTaskParallelism() const {
    return this->task_parallelism
            && this->num_threads > 1
            && this->monteCarolAlg == MonteCarolAlg::NONE;
}

bool PCfrSolver::isAboveSplitRound(GameTreeNode::GameRound round) const {
    if(this->root_round == GameTreeNode::GameRound::RIVER) {
        return false;
    }
    return GameTreeNode::gameRound2int(round) < GameTreeNode::gameRound2int(this->split_round);
}

bool PCfrSolver::shouldUseActionTasks(GameTreeNode::GameRound round, int action_count, int range_size) const {
    return this->canUseTaskParallelism()
            && omp_in_parallel()
            && action_count > 1
            && (action_count * range_size) >= 512
            && this->isAboveSplitRound(round);
}

bool PCfrSolver::shouldUseChanceTasks(std::size_t valid_card_count) const {
    return this->canUseTaskParallelism()
            && omp_in_parallel()
            && valid_card_count >= 8;
}

const vector<PrivateCards> &PCfrSolver::playerHands(int player) {
    if(player == 0){
        return range1;
    }else if (player == 1){
        return range2;
    }else{
        throw runtime_error("player not found");
    }
}

vector<vector<float>> PCfrSolver::getReachProbs() {
    vector<vector<float>> retval(this->player_number);
    for(int player = 0;player < this->player_number;player ++){
        vector<PrivateCards> player_cards = this->playerHands(player);
        vector<float> reach_prob(player_cards.size());
        for(std::size_t i = 0;i < player_cards.size();i ++){
            reach_prob[i] = player_cards[i].weight;
        }
        retval[player] = reach_prob;
    }
    return retval;
}

vector<PrivateCards>
PCfrSolver::noDuplicateRange(const vector<PrivateCards> &private_range, uint64_t board_long) {
    vector<PrivateCards> range_array;
    unordered_map<int,bool> rangekv;
    for(PrivateCards one_range:private_range){
        if(rangekv.find(one_range.hashCode()) != rangekv.end())
            throw runtime_error(tfm::format("duplicated key %s",one_range.toString()));
        rangekv[one_range.hashCode()] = true;
        uint64_t hand_long = Card::boardInts2long(one_range.get_hands());
        if(!Card::boardsHasIntercept(hand_long,board_long)){
            range_array.push_back(one_range);
        }
    }
    return range_array;

}

void PCfrSolver::setTrainable(shared_ptr<GameTreeNode> root) {
    if(root->getType() == GameTreeNode::ACTION){
        shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(root);

        int player = action_node->getPlayer();

        if(this->trainer == "cfr_plus"){
            //vector<PrivateCards> player_privates = this->ranges[player];
            //action_node->setTrainable(make_shared<CfrPlusTrainable>(action_node,player_privates));
            throw runtime_error(tfm::format("trainer %s not supported",this->trainer));
        }else if(this->trainer == "discounted_cfr"){
            vector<PrivateCards>* player_privates = &this->ranges[player];
            //action_node->setTrainable(make_shared<DiscountedCfrTrainable>(action_node,player_privates));
            int num;
            GameTreeNode::GameRound gr = this->tree->getRoot()->getRound();
            int root_round = GameTreeNode::gameRound2int(gr);
            int current_round = GameTreeNode::gameRound2int(root->getRound());
            int gap = current_round - root_round;

            if(gap == 2) {
                num = this->deck.getCards().size() * this->deck.getCards().size() +
                          this->deck.getCards().size() + 1;
            }else if(gap == 1) {
                num = this->deck.getCards().size() + 1;
            }else if(gap == 0) {
                num =  1;
            }else{
                throw runtime_error("gap not understand");
            }
            action_node->setTrainable(vector<shared_ptr<Trainable>>(num),player_privates);
        }else{
            throw runtime_error(tfm::format("trainer %s not found",this->trainer));
        }

        vector<shared_ptr<GameTreeNode>> childrens =  action_node->getChildrens();
        for(shared_ptr<GameTreeNode> one_child:childrens) setTrainable(one_child);
    }else if(root->getType() == GameTreeNode::CHANCE) {
        shared_ptr<ChanceNode> chance_node = std::dynamic_pointer_cast<ChanceNode>(root);
        shared_ptr<GameTreeNode> children = chance_node->getChildren();
        setTrainable(children);
    }
    else if(root->getType() == GameTreeNode::TERMINAL){

    }else if(root->getType() == GameTreeNode::SHOWDOWN){

    }
}

vector<int> PCfrSolver::getAllAbstractionDeal(int deal){
    vector<int> all_deal;
    int card_num = this->deck.getCards().size();
    if(deal == 0){
        all_deal.push_back(deal);
    } else if (deal > 0 && deal <= card_num){
        int origin_deal = int((deal - 1) / 4) * 4;
        for(int i = 0;i < 4;i ++){
            int one_card = origin_deal + i + 1;

            Card *first_card = const_cast<Card *>(&(this->deck.getCards()[origin_deal + i]));
            uint64_t first_long = Card::boardInt2long(
                    first_card->getCardInt());
            if (Card::boardsHasIntercept(first_long, this->initial_board_long))continue;
            all_deal.push_back(one_card);
        }
    } else{
        //cout << "______________________" << endl;
        int c_deal = deal - (1 + card_num);
        int first_deal = int((c_deal / card_num) / 4) * 4;
        int second_deal = int((c_deal % card_num) / 4) * 4;

        for(int i = 0;i < 4;i ++) {
            for(int j = 0;j < 4;j ++) {
                if(first_deal == second_deal && i == j) continue;

                Card *first_card = const_cast<Card *>(&(this->deck.getCards()[first_deal + i]));
                uint64_t first_long = Card::boardInt2long(
                        first_card->getCardInt());
                if (Card::boardsHasIntercept(first_long, this->initial_board_long))continue;

                Card *second_card = const_cast<Card *>(&(this->deck.getCards()[second_deal + j]));
                uint64_t second_long = Card::boardInt2long(
                        second_card->getCardInt());
                if (Card::boardsHasIntercept(second_long, this->initial_board_long))continue;

                int one_card = card_num * (first_deal + i) + (second_deal + j) + 1 + card_num;
                //cout << ";" << this->deck.getCards()[first_deal + i].toString() << "," << this->deck.getCards()[second_deal + j].toString();
                all_deal.push_back(one_card);
            }
        }
        //cout << endl;
    }
    return all_deal;
}

void PCfrSolver::cfr(int player, shared_ptr<GameTreeNode> node, const float* reach_probs, int iter,
                    uint64_t current_board, int deal, float* result) {
    switch(node->getType()) {
        case GameTreeNode::ACTION: {
            shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(node);
            actionUtility(player, action_node, reach_probs, iter, current_board, deal, result);
            break;
        } case GameTreeNode::SHOWDOWN: {
            shared_ptr<ShowdownNode> showdown_node = std::dynamic_pointer_cast<ShowdownNode>(node);
            showdownUtility(player, showdown_node, reach_probs, iter, current_board, deal, result);
            break;
        } case GameTreeNode::TERMINAL: {
            shared_ptr<TerminalNode> terminal_node = std::dynamic_pointer_cast<TerminalNode>(node);
            terminalUtility(player, terminal_node, reach_probs, iter, current_board, deal, result);
            break;
        } case GameTreeNode::CHANCE: {
            shared_ptr<ChanceNode> chance_node = std::dynamic_pointer_cast<ChanceNode>(node);
            chanceUtility(player, chance_node, reach_probs, iter, current_board, deal, result);
            break;
        } default:
            throw runtime_error("node type unknown");
    }
}

void PCfrSolver::chanceUtility(int player, shared_ptr<ChanceNode> node, const float* reach_probs, int iter,
                          uint64_t current_board, int deal, float* result) {
    BenchmarkThreadStats* benchmark_stats = this->profile_enabled ? &this->currentBenchmarkThreadStats() : nullptr;
    if(benchmark_stats != nullptr){
        benchmark_stats->chance_nodes += 1;
    }

    int card_num = node->getCards().size();
    if(card_num % 4 != 0) throw runtime_error("card num cannot round 4");
    int possible_deals = node->getCards().size() - Card::long2board(current_board).size() - 2;
    int oppo = 1 - player;

    uint64_t chance_setup_start = this->profile_enabled ? steadyNowNs() : 0;
    
    // Use result as chance_utility
    int my_range_size = this->ranges[player].size();
    std::fill(result, result + my_range_size, 0.0f);

    int random_deal = 0;
    if(this->monteCarolAlg == MonteCarolAlg::PUBLIC) {
        if (this->round_deal[GameTreeNode::gameRound2int(node->getRound())] == -1) {
            random_deal = random(1, possible_deals + 1 + 2);
            this->round_deal[GameTreeNode::gameRound2int(node->getRound())] = random_deal;
        } else {
            random_deal = this->round_deal[GameTreeNode::gameRound2int(node->getRound())];
        }
    }

    ThreadScratchBuffer& main_scratch = this->currentThreadScratchBuffer();
    float* results_flat = main_scratch.push_floats(52 * my_range_size);
    std::fill(results_flat, results_flat + 52 * my_range_size, 0.0f);
    
    float* multiplier = nullptr;
    if(iter <= this->warmup){
        multiplier = main_scratch.push_floats(card_num);
        std::fill(multiplier, multiplier + card_num, 0.0f);
        for (int card_base = 0; card_base < card_num / 4; card_base++) {
            int cardr = std::rand() % 4;
            int card_target = card_base * 4 + cardr;
            int multiplier_num = 0;
            for (int i = 0; i < 4; i++) {
                int i_card = card_base * 4 + i;
                Card *one_card = const_cast<Card *>(&(node->getCards()[i_card]));
                if (!Card::boardsHasIntercept(Card::boardInt2long(one_card->getCardInt()), current_board)) {
                    multiplier_num += 1;
                }
            }
            multiplier[card_target] = (float)multiplier_num;
        }
    }

    vector<int> valid_cards;
    valid_cards.reserve(node->getCards().size());
    for(std::size_t card = 0; card < node->getCards().size(); card++) {
        Card *one_card = const_cast<Card *>(&(node->getCards()[card]));
        uint64_t card_long = Card::boardInt2long(one_card->getCardInt());
        if (Card::boardsHasIntercept(card_long, current_board)) continue;
        if (iter <= this->warmup && multiplier[card] == 0) continue;
        if (this->color_iso_offset[deal][one_card->getCardInt() % 4] < 0) continue;
        valid_cards.push_back(card);
    }

    if(benchmark_stats != nullptr) benchmark_stats->chance_setup_ns += (this->profile_enabled ? steadyNowNs() - chance_setup_start : 0);

    auto evaluate_valid_card = [&](std::size_t valid_ind) {
        ThreadScratchBuffer& scratch = this->currentThreadScratchBuffer();
        int card = valid_cards[valid_ind];
        shared_ptr<GameTreeNode> one_child = node->getChildren();
        Card *one_card = const_cast<Card *>(&(node->getCards()[card]));
        uint64_t card_long = Card::boardInt2long(one_card->getCardInt());
        uint64_t new_board_long = current_board | card_long;

        int player_hand_len = this->ranges[oppo].size();
        float* new_reach_probs = scratch.push_floats(player_hand_len);

        for (int player_hand = 0; player_hand < player_hand_len; player_hand++) {
            const PrivateCards &one_private = this->ranges[oppo][player_hand];
            if (Card::boardsHasIntercept(card_long, one_private.toBoardLong())) {
                new_reach_probs[player_hand] = 0;
            } else {
                new_reach_probs[player_hand] = reach_probs[player_hand] / possible_deals;
            }
        }

        int new_deal;
        if(deal == 0) new_deal = card + 1;
        else {
            int origin_deal = deal - 1;
            new_deal = card_num * origin_deal + card + (1 + card_num);
        }

        float* child_result_ptr = results_flat + one_card->getNumberInDeckInt() * my_range_size;
        this->cfr(player, one_child, new_reach_probs, iter, new_board_long, new_deal, child_result_ptr);
        scratch.pop_floats(player_hand_len);
    };

    if(this->shouldUseChanceTasks(valid_cards.size())) {
        #pragma omp taskgroup
        {
            for(int valid_ind = 0; valid_ind < static_cast<int>(valid_cards.size()); valid_ind++) {
                #pragma omp task firstprivate(valid_ind)
                {
                    evaluate_valid_card(static_cast<std::size_t>(valid_ind));
                }
            }
        }
    } else {
        if(!omp_in_parallel()) {
            #pragma omp parallel for schedule(static)
            for(int valid_ind = 0; valid_ind < static_cast<int>(valid_cards.size()); valid_ind++) {
                evaluate_valid_card(static_cast<std::size_t>(valid_ind));
            }
        } else {
            for(std::size_t valid_ind = 0; valid_ind < valid_cards.size(); valid_ind++) {
                evaluate_valid_card(valid_ind);
            }
        }
    }

    uint64_t chance_merge_start = this->profile_enabled ? steadyNowNs() : 0;
    for(std::size_t card = 0; card < node->getCards().size(); card++) {
        Card *one_card = const_cast<Card *>(&(node->getCards()[card]));
        int offset = this->color_iso_offset[deal][one_card->getCardInt() % 4];
        float* child_utility_ptr;
        if(offset < 0) {
            int rank1 = one_card->getCardInt() % 4;
            int rank2 = rank1 + offset;
            child_utility_ptr = results_flat + (one_card->getNumberInDeckInt() + offset) * my_range_size;
            
            // Still need a temporary buffer for exchange_color if we want to avoid mutating the flat buffer
            // but we can just use the scratch!
            ThreadScratchBuffer& scratch = this->currentThreadScratchBuffer();
            float* exchanged_utility = scratch.push_floats(my_range_size);
            std::copy(child_utility_ptr, child_utility_ptr + my_range_size, exchanged_utility);
            
            int* exchange_scratch = scratch.push_ints(52 * 52 * 2);
            exchange_color_ptr(exchanged_utility, my_range_size, this->pcm.getPreflopCards(player), rank1, rank2, exchange_scratch);
            scratch.pop_ints(52 * 52 * 2);
            
            float mult = (iter > this->warmup) ? 1.0f : multiplier[card];
            if (mult != 0) {
                for (std::size_t i = 0; i < (std::size_t)my_range_size; i++)
                    result[i] += exchanged_utility[i] * mult;
            }
            scratch.pop_floats(my_range_size);
        } else {
            child_utility_ptr = results_flat + one_card->getNumberInDeckInt() * my_range_size;
            float mult = (iter > this->warmup) ? 1.0f : multiplier[card];
            if (mult != 0) {
                for (std::size_t i = 0; i < (std::size_t)my_range_size; i++)
                    result[i] += child_utility_ptr[i] * mult;
            }
        }
    }
    if(benchmark_stats != nullptr) benchmark_stats->chance_merge_ns += (this->profile_enabled ? steadyNowNs() - chance_merge_start : 0);

    // Pop scratch space
    if(iter <= this->warmup) main_scratch.pop_floats(card_num);
    main_scratch.pop_floats(52 * my_range_size);
}

void PCfrSolver::actionUtility(int player, shared_ptr<ActionNode> node, const float* reach_probs, int iter,
                          uint64_t current_board, int deal, float* result) {
    BenchmarkThreadStats* benchmark_stats = this->profile_enabled ? &this->currentBenchmarkThreadStats() : nullptr;
    if(benchmark_stats != nullptr) benchmark_stats->action_nodes += 1;

    int oppo = 1 - player;
    const vector<PrivateCards>& node_player_private_cards = this->ranges[node->getPlayer()];
    int node_player_size = node_player_private_cards.size();
    int my_range_size = this->ranges[player].size();
    
    // Clear result (payoffs accumulator)
    std::fill(result, result + my_range_size, 0.0f);

    vector<shared_ptr<GameTreeNode>>& children = node->getChildrens();
    vector<GameActions>& actions = node->getActions();
    int action_count = actions.size();

    shared_ptr<Trainable> trainable = node->getTrainable(deal, true, this->use_halffloats);
    
    ThreadScratchBuffer& scratch = this->currentThreadScratchBuffer();
    
    // Push scratch space
    float* current_strategy = scratch.push_floats(action_count * node_player_size);
    float* regrets = scratch.push_floats(action_count * node_player_size);
    float* all_action_utilities = scratch.push_floats(action_count * my_range_size);
    const bool use_action_tasks = this->shouldUseActionTasks(node->getRound(), action_count, my_range_size);
    float* child_utility = nullptr;
    if(!use_action_tasks) {
        child_utility = scratch.push_floats(my_range_size);
    }

    uint64_t strategy_fetch_start = this->profile_enabled ? steadyNowNs() : 0;
    trainable->getcurrentStrategyInPlace(current_strategy);
    if(benchmark_stats != nullptr) benchmark_stats->strategy_fetch_ns += (this->profile_enabled ? steadyNowNs() - strategy_fetch_start : 0);

    auto evaluate_action_branch = [&](int action_id, float* branch_utility) {
        if (node->getPlayer() != player) {
            // Oppo is making a decision, update their reach probs passing down
            ThreadScratchBuffer& branch_scratch = this->currentThreadScratchBuffer();
            float* new_reach_probs = branch_scratch.push_floats(node_player_size);
            for (int hand_id = 0; hand_id < node_player_size; hand_id++) {
                float strategy_prob = current_strategy[hand_id + action_id * node_player_size];
                new_reach_probs[hand_id] = reach_probs[hand_id] * strategy_prob;
            }
            this->cfr(player, children[action_id], new_reach_probs, iter, current_board, deal, branch_utility);
            branch_scratch.pop_floats(node_player_size);
        } else {
            // I am making the decision, reach probs don't change for child calls
            this->cfr(player, children[action_id], reach_probs, iter, current_board, deal, branch_utility);
        }
    };

    if(use_action_tasks) {
        #pragma omp taskgroup
        {
            for (int action_id = 0; action_id < action_count; action_id++) {
                #pragma omp task firstprivate(action_id)
                {
                    float* branch_utility = all_action_utilities + action_id * my_range_size;
                    evaluate_action_branch(action_id, branch_utility);
                }
            }
        }
    } else {
        for (int action_id = 0; action_id < action_count; action_id++) {
            evaluate_action_branch(action_id, child_utility);
            std::copy(child_utility, child_utility + my_range_size, all_action_utilities + action_id * my_range_size);
        }
    }

    for (int action_id = 0; action_id < action_count; action_id++) {
        float* branch_utility = all_action_utilities + action_id * my_range_size;
        for (int hand_id = 0; hand_id < my_range_size; hand_id++) {
            if (player == node->getPlayer()) {
                float strategy_prob = current_strategy[hand_id + action_id * my_range_size];
                result[hand_id] += strategy_prob * branch_utility[hand_id];
            } else {
                result[hand_id] += branch_utility[hand_id];
            }
        }
    }

    if (player == node->getPlayer()) {
        for (int i = 0; i < my_range_size; i++) {
            for (int action_id = 0; action_id < action_count; action_id++) {
                regrets[action_id * my_range_size + i] = all_action_utilities[action_id * my_range_size + i] - result[i];
            }
        }

        if(!this->distributing_task && !this->collecting_statics) {
            uint64_t regret_update_start = this->profile_enabled ? steadyNowNs() : 0;
            if (iter > this->warmup) {
                trainable->updateRegretsInPlace(regrets, iter + 1, reach_probs);
            } else if (iter == this->warmup) {
                vector<int> deals = this->getAllAbstractionDeal(deal);
                shared_ptr<Trainable> standard_trainable = nullptr;
                for (int one_deal : deals) {
                    shared_ptr<Trainable> one_trainable = node->getTrainable(one_deal, true, this->use_halffloats);
                    if (standard_trainable == nullptr) {
                        one_trainable->updateRegretsInPlace(regrets, iter + 1, reach_probs);
                        standard_trainable = one_trainable;
                    } else {
                        one_trainable->copyStrategy(standard_trainable);
                    }
                }
            }
            if((iter >= this->warmup) && benchmark_stats != nullptr) {
                benchmark_stats->regret_update_ns += (this->profile_enabled ? steadyNowNs() - regret_update_start : 0);
            }
        }

        if(this->collecting_statics || (iter % this->print_interval == 0)) {
            float oppo_sum = 0.0f;
            float oppo_card_sum[52] = {0.0f};

            const vector<PrivateCards>& oppo_hand = this->playerHands(oppo);
            for(std::size_t i = 0; i < oppo_hand.size(); i++) {
                oppo_card_sum[oppo_hand[i].card1] += reach_probs[i];
                oppo_card_sum[oppo_hand[i].card2] += reach_probs[i];
                oppo_sum += reach_probs[i];
            }

            const vector<PrivateCards>& player_hand = this->playerHands(player);
            vector<float> evs(action_count * my_range_size, 0.0f);
            uint64_t ev_update_start = this->profile_enabled ? steadyNowNs() : 0;

            for (int action_id = 0; action_id < action_count; action_id++) {
                for (int hand_id = 0; hand_id < my_range_size; hand_id++) {
                    const float one_ev = all_action_utilities[action_id * my_range_size + hand_id];
                    const PrivateCards& one_player_hand = player_hand[hand_id];
                    const int oppo_same_card_ind = this->pcm.indPlayer2Player(player, oppo, hand_id);
                    const float plus_reach_prob = (oppo_same_card_ind == -1) ? 0.0f : reach_probs[oppo_same_card_ind];
                    const float rp_sum = oppo_sum
                            - oppo_card_sum[one_player_hand.card1]
                            - oppo_card_sum[one_player_hand.card2]
                            + plus_reach_prob;

                    if(rp_sum > 0.0f && std::isfinite(one_ev)) {
                        evs[hand_id + action_id * my_range_size] = one_ev / rp_sum;
                    } else {
                        // EV is undefined for unreachable hands; keep the dump stable with 0.
                        evs[hand_id + action_id * my_range_size] = 0.0f;
                    }
                }
            }

            trainable->setEv(evs);
            if(benchmark_stats != nullptr) {
                benchmark_stats->ev_update_ns += (this->profile_enabled ? steadyNowNs() - ev_update_start : 0);
            }
        }
    }

    // Pop scratch space
    scratch.pop_floats(action_count * my_range_size);
    if(!use_action_tasks) {
        scratch.pop_floats(my_range_size);
    }
    scratch.pop_floats(action_count * node_player_size);
    scratch.pop_floats(action_count * node_player_size);
}
void PCfrSolver::showdownUtility(int player, shared_ptr<ShowdownNode> node, const float* reach_probs,
                           int iter, uint64_t current_board, int deal, float* result) {
    BenchmarkThreadStats* benchmark_stats = this->profile_enabled ? &this->currentBenchmarkThreadStats() : nullptr;
    uint64_t showdown_start = this->profile_enabled ? steadyNowNs() : 0;
    if(benchmark_stats != nullptr) benchmark_stats->showdown_nodes += 1;
    
    int my_range_size = this->ranges[player].size();
    std::fill(result, result + my_range_size, 0.0f);
    
    int oppo = 1 - player;
    float win_payoff = node->get_payoffs(ShowdownNode::ShowDownResult::NOTTIE, player, player);
    float lose_payoff = node->get_payoffs(ShowdownNode::ShowDownResult::NOTTIE, oppo, player);
    const vector<PrivateCards>& player_private_cards = this->ranges[player];
    const vector<PrivateCards>& oppo_private_cards = this->ranges[oppo];

    const vector<RiverCombs>& player_combs = this->rrm.getRiverCombos(player, player_private_cards, current_board);
    const vector<RiverCombs>& oppo_combs = this->rrm.getRiverCombos(oppo, oppo_private_cards, current_board);

    float winsum = 0;
    float card_winsum[52] = {0};

    std::size_t j = 0;
    for(std::size_t i = 0; i < player_combs.size(); i++) {
        const RiverCombs& one_player_comb = player_combs[i];
        while (j < oppo_combs.size() && one_player_comb.rank < oppo_combs[j].rank) {
            const RiverCombs& one_oppo_comb = oppo_combs[j];
            float rp = reach_probs[one_oppo_comb.reach_prob_index];
            winsum += rp;
            card_winsum[one_oppo_comb.private_cards.card1] += rp;
            card_winsum[one_oppo_comb.private_cards.card2] += rp;
            j++;
        }
        result[one_player_comb.reach_prob_index] = (winsum
                                                     - card_winsum[one_player_comb.private_cards.card1]
                                                     - card_winsum[one_player_comb.private_cards.card2]
                                                    ) * win_payoff;
    }

    float losssum = 0;
    float card_losssum[52] = {0};

    int64_t j2 = (int64_t)oppo_combs.size() - 1;
    for(int i = player_combs.size() - 1; i >= 0; i--) {
        const RiverCombs& one_player_comb = player_combs[i];
        while (j2 >= 0 && one_player_comb.rank > oppo_combs[j2].rank) {
            const RiverCombs& one_oppo_comb = oppo_combs[j2];
            float rp = reach_probs[one_oppo_comb.reach_prob_index];
            losssum += rp;
            card_losssum[one_oppo_comb.private_cards.card1] += rp;
            card_losssum[one_oppo_comb.private_cards.card2] += rp;
            j2--;
        }
        result[one_player_comb.reach_prob_index] += (losssum
                                                      - card_losssum[one_player_comb.private_cards.card1]
                                                      - card_losssum[one_player_comb.private_cards.card2]
                                                     ) * lose_payoff;
    }
    if(benchmark_stats != nullptr && this->profile_enabled) benchmark_stats->showdown_ns += steadyNowNs() - showdown_start;
}

void PCfrSolver::terminalUtility(int player, shared_ptr<TerminalNode> node, const float* reach_probs, int iter,
                           uint64_t current_board, int deal, float* result) {
    BenchmarkThreadStats* benchmark_stats = this->profile_enabled ? &this->currentBenchmarkThreadStats() : nullptr;
    uint64_t terminal_start = this->profile_enabled ? steadyNowNs() : 0;
    if(benchmark_stats != nullptr) benchmark_stats->terminal_nodes += 1;
    
    float player_payoff = node->get_payoffs()[player];
    int oppo = 1 - player;
    const vector<PrivateCards>& player_hand = this->ranges[player];
    const vector<PrivateCards>& oppo_hand = this->ranges[oppo];

    int my_range_size = player_hand.size();
    float oppo_sum = 0;
    float oppo_card_sum[52] = {0};
    for(std::size_t i = 0; i < oppo_hand.size(); i++) {
        oppo_sum += reach_probs[i];
        oppo_card_sum[oppo_hand[i].card1] += reach_probs[i];
        oppo_card_sum[oppo_hand[i].card2] += reach_probs[i];
    }

    for(int i = 0; i < my_range_size; i++) {
        const PrivateCards& pct = player_hand[i];
        if (Card::boardsHasIntercept(pct.toBoardLong(), current_board)) {
            result[i] = 0.0f;
            continue;
        }
        int oppo_same_card_ind = this->pcm.indPlayer2Player(player, oppo, i);
        float plus_reach_prob = (oppo_same_card_ind == -1) ? 0.0f : reach_probs[oppo_same_card_ind];
        
        float rp_sum = oppo_sum - oppo_card_sum[pct.card1] - oppo_card_sum[pct.card2] + plus_reach_prob;
        result[i] = rp_sum * player_payoff;
    }
    if(benchmark_stats != nullptr && this->profile_enabled) benchmark_stats->terminal_ns += steadyNowNs() - terminal_start;
}

void PCfrSolver::findGameSpecificIsomorphisms() {
    // hand isomorphisms
    vector<Card> board_cards = Card::long2boardCards(this->initial_board_long);
    for(int i = 0;i <= 1;i ++){
        vector<PrivateCards>& range = i == 0?this->range1:this->range2;
        for(std::size_t i_range = 0;i_range < range.size();i_range ++) {
            PrivateCards one_range = range[i_range];
            uint32_t range_hash[4]; // four colors, hash of the isomorphisms range + hand combos
            for(int i = 0;i < 4;i ++)range_hash[i] = 0;
            for (int color = 0; color < 4; color++) {
                for (Card one_card:board_cards) {
                    if (one_card.getCardInt() % 4 == color) {
                        range_hash[color] = range_hash[color] | (1 << (one_card.getCardInt() / 4));
                    }
                }
            }
            for (int color = 0; color < 4; color++) {
                for (int one_card_int:{one_range.card1,one_range.card2}) {
                    if (one_card_int % 4 == color) {
                        range_hash[color] = range_hash[color] | (1 << (one_card_int / 4 + 16));
                    }
                }
            }
            // TODO check whethe hash is equal with others
        }
    }

    // chance node isomorphisms
    uint16_t color_hash[4];
    for(int i = 0;i < 4;i ++)color_hash[i] = 0;
    for (Card one_card:board_cards) {
        int rankind = one_card.getCardInt() % 4;
        int suitind = one_card.getCardInt() / 4;
        color_hash[rankind] = color_hash[rankind] | (1 << suitind);
    }
    for(int i = 0;i < 4;i ++){
        this->color_iso_offset[0][i] = 0;
        for(int j = 0;j < i;j ++){
            if(color_hash[i] == color_hash[j]){
                this->color_iso_offset[0][i] = j - i;
                continue;
            }
        }
    }
    for(std::size_t deal = 0;deal < this->deck.getCards().size();deal ++) {
        uint16_t color_hash[4];
        for(int i = 0;i < 4;i ++)color_hash[i] = 0;
        // chance node isomorphisms
        for (Card one_card:board_cards) {
            int rankind = one_card.getCardInt() % 4;
            int suitind = one_card.getCardInt() / 4;
            color_hash[rankind] = color_hash[rankind] | (1 << suitind);
        }
        Card one_card = this->deck.getCards()[deal];
        int rankind = one_card.getCardInt() % 4;
        int suitind = one_card.getCardInt() / 4;
        color_hash[rankind] = color_hash[rankind] | (1 << suitind);
        for (int i = 0; i < 4; i++) {
            this->color_iso_offset[deal + 1][i] = 0;
            for (int j = 0; j < i; j++) {
                if (color_hash[i] == color_hash[j]) {
                    this->color_iso_offset[deal + 1][i] = j - i;
                    continue;
                }
            }
        }
    }
}

void PCfrSolver::purnTree() {
    // TODO how to purn the tree, use wramup to start training in memory-save mode, and switch to purn tree directly to both save memory and speedup
}

void PCfrSolver::stop() {
    this->nowstop = true;
}

void PCfrSolver::train() {

    vector<vector<PrivateCards>> player_privates(this->player_number);
    player_privates[0] = pcm.getPreflopCards(0);
    player_privates[1] = pcm.getPreflopCards(1);
    if(this->use_isomorphism){
        this->findGameSpecificIsomorphisms();
    }

    BestResponse br = BestResponse(player_privates,this->player_number,this->pcm,this->rrm,this->deck,this->debug,this->color_iso_offset,this->split_round,this->num_threads,this->use_halffloats);

    vector<vector<float>> reach_probs = this->getReachProbs();
    ofstream fileWriter;
    if(!this->logfile.empty()){
        if(this->profile_enabled){
            fileWriter.open(this->logfile, ios::out | ios::app);
        }else{
            fileWriter.open(this->logfile);
        }
    }

    if(this->profile_enabled && !this->logfile.empty()){
        json session_meta;
        session_meta["type"] = "solver_session";
        session_meta["threads"] = this->num_threads;
        session_meta["iteration_limit"] = this->iteration_number;
        session_meta["print_interval"] = this->print_interval;
        session_meta["warmup"] = this->warmup;
        session_meta["accuracy_target"] = this->accuracy;
        session_meta["use_isomorphism"] = this->use_isomorphism;
        session_meta["use_halffloats"] = this->use_halffloats;
        session_meta["task_parallelism"] = this->task_parallelism;
        session_meta["range_sizes"] = {this->range1.size(), this->range2.size()};
        session_meta["root_round"] = GameTreeNode::gameRound2int(this->root_round);
        fileWriter << session_meta << endl;
    }

    uint64_t initial_br_start = steadyNowNs();
    this->rrm.resetStats();
    float initial_exploitability = br.printExploitability(tree->getRoot(), 0, tree->getRoot()->getPot(), initial_board_long);
    uint64_t initial_br_ns = steadyNowNs() - initial_br_start;
    if(this->profile_enabled && !this->logfile.empty()){
        json initial_event;
        initial_event["type"] = "initial_best_response";
        initial_event["iteration"] = 0;
        initial_event["exploitability"] = initial_exploitability;
        initial_event["best_response_ms"] = nsToMs(initial_br_ns);
        json initial_benchmark = this->collectBenchmarkStatsJson();
        initial_event["river_cache"] = initial_benchmark["river_cache"];
        fileWriter << initial_event << endl;
    }

    uint64_t begintime = timeSinceEpochMillisec();
    uint64_t endtime = timeSinceEpochMillisec();
    uint64_t solve_start_ns = steadyNowNs();

    for(int i = 0;i < this->iteration_number;i++){
        uint64_t iteration_start_ns = steadyNowNs();
        double player_cfr_ms[2] = {0.0, 0.0};
        this->resetBenchmarkThreadStats();
        this->rrm.resetStats();
        for(int player_id = 0;player_id < this->player_number;player_id ++) {
            this->round_deal = vector<int>{-1,-1,-1,-1};
            uint64_t player_cfr_start = steadyNowNs();
            auto run_player_cfr = [&]() {
                ThreadScratchBuffer& root_scratch = this->currentThreadScratchBuffer();
                float* root_result = root_scratch.push_floats(this->ranges[player_id].size());
                cfr(player_id, this->tree->getRoot(), reach_probs[1 - player_id].data(), i, this->initial_board_long, 0, root_result);
                root_scratch.pop_floats(this->ranges[player_id].size());
            };
            if(this->canUseTaskParallelism()) {
                #pragma omp parallel
                {
                    #pragma omp single
                    {
                        run_player_cfr();
                    }
                }
            } else {
                {
                    float* root_result = this->currentThreadScratchBuffer().push_floats(this->ranges[player_id].size());
                    cfr(player_id, this->tree->getRoot(), reach_probs[1 - player_id].data(), i, this->initial_board_long, 0, root_result);
                    this->currentThreadScratchBuffer().pop_floats(this->ranges[player_id].size());
                }
            }
            player_cfr_ms[player_id] = nsToMs(steadyNowNs() - player_cfr_start);
        }
        double best_response_ms = 0.0;
        float expliotibility = 0.0f;
        if( (i % this->print_interval == 0 && i != 0 && i >= this->warmup) || this->nowstop) {
            endtime = timeSinceEpochMillisec();
            long time_ms = endtime - begintime;
            qDebug().noquote() << "-------------------";
            uint64_t best_response_start = steadyNowNs();
            expliotibility = br.printExploitability(tree->getRoot(), i + 1, tree->getRoot()->getPot(), initial_board_long);
            best_response_ms = nsToMs(steadyNowNs() - best_response_start);
            qDebug().noquote() << QObject::tr("time used: ") << float(time_ms) / 1000 << QObject::tr(" second.");
            if(!this->logfile.empty()){
                json jo;
                jo["type"] = "iteration_summary";
                jo["iteration"] = i;
                jo["exploitibility"] = expliotibility;
                jo["time_ms"] = time_ms;
                if(this->profile_enabled){
                    jo["iteration_total_ms"] = nsToMs(steadyNowNs() - iteration_start_ns);
                    jo["player_cfr_ms"] = {player_cfr_ms[0], player_cfr_ms[1]};
                    jo["best_response_ms"] = best_response_ms;
                    json benchmark_json = this->collectBenchmarkStatsJson();
                    jo["solver_profile"] = benchmark_json["timings_ms"];
                    jo["node_counts"] = benchmark_json["node_counts"];
                    jo["allocator_profile"] = benchmark_json["allocator_profile"];
                    jo["river_cache"] = benchmark_json["river_cache"];
                }
                fileWriter << jo << endl;
            }
            if(expliotibility <= this->accuracy){
                break;
            }
            if(this->nowstop){
                this->nowstop = false;
                break;
            }
            //begintime = timeSinceEpochMillisec();
        }else if(this->profile_enabled && !this->logfile.empty()){
            json iteration_event;
            iteration_event["type"] = "iteration_profile";
            iteration_event["iteration"] = i;
            iteration_event["iteration_total_ms"] = nsToMs(steadyNowNs() - iteration_start_ns);
            iteration_event["player_cfr_ms"] = {player_cfr_ms[0], player_cfr_ms[1]};
            json benchmark_json = this->collectBenchmarkStatsJson();
            iteration_event["solver_profile"] = benchmark_json["timings_ms"];
            iteration_event["node_counts"] = benchmark_json["node_counts"];
            iteration_event["allocator_profile"] = benchmark_json["allocator_profile"];
            iteration_event["river_cache"] = benchmark_json["river_cache"];
            fileWriter << iteration_event << endl;
        }
    }

    qDebug().noquote() << QObject::tr("collecting statics");
    uint64_t collect_statics_start = steadyNowNs();
    this->collecting_statics = true;
    this->resetBenchmarkThreadStats();
    this->rrm.resetStats();
    for(int player_id = 0;player_id < this->player_number;player_id ++) {
        this->round_deal = vector<int>{-1,-1,-1,-1};
        auto collect_player_statics = [&]() {
            ThreadScratchBuffer& root_scratch = this->currentThreadScratchBuffer();
            float* result = root_scratch.push_floats(this->ranges[player_id].size());
            std::fill(result, result + this->ranges[player_id].size(), 0.0f);
            cfr(player_id, this->tree->getRoot(), reach_probs[1 - player_id].data(), this->iteration_number, this->initial_board_long,0, result);
            root_scratch.pop_floats(this->ranges[player_id].size());
        };
        if(this->canUseTaskParallelism()) {
            #pragma omp parallel
            {
                #pragma omp single
                {
                    collect_player_statics();
                }
            }
        } else {
            {
                vector<float> result(this->ranges[player_id].size(), 0.0f);
                cfr(player_id, this->tree->getRoot(), reach_probs[1 - player_id].data(), this->iteration_number, this->initial_board_long,0, result.data());
            }
        }
    }
    this->collecting_statics = false;
    this->statics_collected = true;
    qDebug().noquote() << QObject::tr("statics collected");
    if(this->profile_enabled && !this->logfile.empty()){
        json final_event;
        final_event["type"] = "final_statics";
        final_event["collect_statics_ms"] = nsToMs(steadyNowNs() - collect_statics_start);
        final_event["solve_total_ms"] = nsToMs(steadyNowNs() - solve_start_ns);
        json benchmark_json = this->collectBenchmarkStatsJson();
        final_event["solver_profile"] = benchmark_json["timings_ms"];
        final_event["node_counts"] = benchmark_json["node_counts"];
        final_event["allocator_profile"] = benchmark_json["allocator_profile"];
        final_event["river_cache"] = benchmark_json["river_cache"];
        fileWriter << final_event << endl;
    }

    if(!this->logfile.empty()) {
        fileWriter.flush();
        fileWriter.close();
    }

}

void PCfrSolver::exchangeRange(json& strategy,int rank1,int rank2,shared_ptr<ActionNode> one_node){
    if(rank1 == rank2)return;
    int player = one_node->getPlayer();
    vector<string> range_strs;
    vector<vector<float>> strategies;

    for(std::size_t i = 0;i < this->ranges[player].size();i ++){
        string one_range_str = this->ranges[player][i].toString();
        if(!strategy.contains(one_range_str)){
            for(auto one_key:strategy.items()){
                cout << one_key.key() << endl;
            }
            cout << "strategy: " << strategy  << endl;
            throw runtime_error(tfm::format("%s not exist in strategy",one_range_str));
        }
        vector<float> one_strategy = strategy[one_range_str];
        range_strs.push_back(one_range_str);
        strategies.push_back(one_strategy);
    }
    vector<int> ex_scratch(52 * 52 * 2); exchange_color(strategies,this->ranges[player],rank1,rank2, ex_scratch.data());

    for(std::size_t i = 0;i < this->ranges[player].size();i ++) {
        string one_range_str = this->ranges[player][i].toString();
        vector<float> one_strategy = strategies[i];
        strategy[one_range_str] = one_strategy;
    }
}

void PCfrSolver::reConvertJson(const shared_ptr<GameTreeNode>& node,json& strategy,string key,int depth,int max_depth,vector<string> prefix,int deal,vector<vector<int>> exchange_color_list) {
    if(depth >= max_depth) return;
    if(node->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        json* retval;
        if(key != ""){
            strategy[key] = json();
            retval = &(strategy[key]);
        }else{
            retval = &strategy;
        }

        shared_ptr<ActionNode> one_node = std::dynamic_pointer_cast<ActionNode>(node);

        vector<string> actions_str;
        for(GameActions one_action:one_node->getActions()) actions_str.push_back(one_action.toString());

        (*retval)["actions"] = actions_str;
        (*retval)["player"] = one_node->getPlayer();

        (*retval)["childrens"] = json();
        json& childrens = (*retval)["childrens"];

        for(std::size_t i = 0;i < one_node->getActions().size();i ++){
            GameActions& one_action = one_node->getActions()[i];
            shared_ptr<GameTreeNode> one_child = one_node->getChildrens()[i];
            vector<string> new_prefix(prefix);
            new_prefix.push_back(one_action.toString());
            this->reConvertJson(one_child,childrens,one_action.toString(),depth,max_depth,new_prefix,deal,exchange_color_list);
        }
        if((*retval)["childrens"].empty()){
            (*retval).erase("childrens");
        }
        shared_ptr<Trainable> trainable = one_node->getTrainable(deal,false);
        if(trainable != nullptr) {
            (*retval)["strategy"] = trainable->dump_strategy(false);
            for(vector<int> one_exchange:exchange_color_list){
                int rank1 = one_exchange[0];
                int rank2 = one_exchange[1];
                this->exchangeRange((*retval)["strategy"]["strategy"],rank1,rank2,one_node);

            }
        }
        (*retval)["node_type"] = "action_node";

    }else if(node->getType() == GameTreeNode::GameTreeNodeType::SHOWDOWN) {
    }else if(node->getType() == GameTreeNode::GameTreeNodeType::TERMINAL) {
    }else if(node->getType() == GameTreeNode::GameTreeNodeType::CHANCE) {
        json* retval;
        if(key != ""){
            strategy[key] = json();
            retval = &(strategy[key]);
        }else{
            retval = &strategy;
        }

        shared_ptr<ChanceNode> chanceNode = std::dynamic_pointer_cast<ChanceNode>(node);
        const vector<Card>& cards = chanceNode->getCards();
        shared_ptr<GameTreeNode> childerns = chanceNode->getChildren();
        vector<string> card_strs;
        for(Card card:cards)
            card_strs.push_back(card.toString());

        json& dealcards = (*retval)["dealcards"];
        for(std::size_t i = 0;i < cards.size();i ++){
            vector<vector<int>> new_exchange_color_list(exchange_color_list);
            Card& one_card = const_cast<Card &>(cards[i]);
            vector<string> new_prefix(prefix);
            new_prefix.push_back("Chance:" + one_card.toString());

            std::size_t card = i;

            int offset = this->color_iso_offset[deal][one_card.getCardInt() % 4];
            if(offset < 0) {
                for(std::size_t x = 0;x < cards.size();x ++){
                    if(
                            Card::card2int(cards[x]) ==
                            (Card::card2int(cards[card]) + offset)
                    ){
                        card = x;
                        break;
                    }
                }
                if(card == i){
                    throw runtime_error("isomorphism not found while dump strategy");
                }
                vector<int> one_exchange{one_card.getCardInt() % 4,one_card.getCardInt() % 4 + offset};
                new_exchange_color_list.push_back(one_exchange);
            }

            int card_num = this->deck.getCards().size();
            int new_deal;
            if(deal == 0){
                new_deal = card + 1;
            } else if (deal > 0 && deal <= card_num){
                int origin_deal = deal - 1;

#ifdef DEBUG
                if(origin_deal == card) throw runtime_error("deal should not be equal");
#endif
                new_deal = card_num * origin_deal + card;
                new_deal += (1 + card_num);
            } else{
                throw runtime_error(tfm::format("deal out of range : %s ",deal));
            }

            if(exchange_color_list.size() > 1){
                throw runtime_error("exchange color list shouldn't be exceed size 1 here");
            }

            string one_card_str = one_card.toString();
            if(exchange_color_list.size() == 1) {
                int rank1 = exchange_color_list[0][0];
                int rank2 = exchange_color_list[0][1];
                if(one_card.getCardInt() % 4 == rank1){
                    one_card_str = Card::intCard2Str(one_card.getCardInt() - rank1 + rank2);
                }else if(one_card.getCardInt() % 4 == rank2){
                    one_card_str = Card::intCard2Str(one_card.getCardInt() - rank2 + rank1);
                }

            }

            this->reConvertJson(childerns,dealcards,one_card_str,depth + 1,max_depth,new_prefix,new_deal,new_exchange_color_list);
        }
        if((*retval)["dealcards"].empty()){
            (*retval).erase("dealcards");
        }

        (*retval)["deal_number"] = dealcards.size();
        (*retval)["node_type"] = "chance_node";
    }else{
        throw runtime_error("node type unknown!!");
    }
}

vector<vector<vector<float>>> PCfrSolver::get_strategy(shared_ptr<ActionNode> node,vector<Card> chance_cards){
    int deal = 0;
    int card_num = this->deck.getCards().size();
    vector<vector<int>> exchange_color_list;

    vector<vector<vector<float>>> ret_strategy = vector<vector<vector<float>>>(52);
    for(int i = 0;i < 52;i ++){
        ret_strategy[i] = vector<vector<float>>(52);
        for(int j = 0;j < 52;j ++){
            ret_strategy[i][j] = vector<float>();
        }
    }

    vector<Card>& cards = this->deck.getCards();

    for(Card one_card: chance_cards){
        int card = one_card.getNumberInDeckInt();
        int offset = this->color_iso_offset[deal][one_card.getCardInt() % 4];
        if(offset < 0) {
            for(std::size_t x = 0;x < cards.size();x ++){
                if(
                    Card::card2int(cards[x]) ==
                    (Card::card2int(cards[card]) + offset)
                ){
                    card = x;
                    break;
                }
            }
            if(card == one_card.getNumberInDeckInt()){
                throw runtime_error("isomorphism not found while dump strategy");
            }
            vector<int> one_exchange{one_card.getCardInt() % 4,one_card.getCardInt() % 4 + offset};
            exchange_color_list.push_back(one_exchange);
        }

        int new_deal;
        if(deal == 0){
            new_deal = card + 1;
        } else if (deal > 0 && deal <= card_num){
            int origin_deal = deal - 1;
            new_deal = card_num * origin_deal + card;
            new_deal += (1 + card_num);
        } else{
            throw runtime_error(tfm::format("deal out of range : %s ",deal));
        }
        deal = new_deal;
    }
    shared_ptr<Trainable> trainable = node->getTrainable(deal,true,this->use_halffloats);
    json retjson = trainable->dump_strategy(false);;

    for(vector<int> one_exchange:exchange_color_list){
        int rank1 = one_exchange[0];
        int rank2 = one_exchange[1];
        this->exchangeRange((retjson["strategy"]),rank1,rank2,node);
    }

    int player = node->getPlayer();

    json& strategy = retjson["strategy"];
    for(std::size_t i = 0;i < this->ranges[player].size();i ++){
        PrivateCards pc = this->ranges[player][i];
        string one_range_str = pc.toString();
        if(!strategy.contains(one_range_str)){
            for(auto one_key:strategy.items()){
                cout << one_key.key() << endl;
            }
            cout << "strategy: " << strategy  << endl;
            cout << "Eror when get_strategy in PCfrSolver" << endl;
            throw runtime_error(tfm::format("%s not exist in strategy",one_range_str));
        }
        vector<float> one_strategy = strategy[one_range_str];
        bool intercept = false;
        for(auto one_card:chance_cards){
            if(one_card.getCardInt() == pc.card1 || one_card.getCardInt() == pc.card2){
                intercept = true;
            }
        }
        if(intercept) continue;
        ret_strategy[pc.card1][pc.card2] = one_strategy;
    }
    return ret_strategy;
}

vector<vector<vector<float>>> PCfrSolver::get_evs(shared_ptr<ActionNode> node,vector<Card> chance_cards){
    // If solving process has not finished, then no evs is set, therefore we shouldn't return anything
    int deal = 0;
    int card_num = this->deck.getCards().size();
    vector<vector<int>> exchange_color_list;

    vector<vector<vector<float>>> ret_evs = vector<vector<vector<float>>>(52);
    for(int i = 0;i < 52;i ++){
        ret_evs[i] = vector<vector<float>>(52);
        for(int j = 0;j < 52;j ++){
            ret_evs[i][j] = vector<float>();
        }
    }

    if(!this->statics_collected) {
        return ret_evs;
    }

    vector<Card>& cards = this->deck.getCards();

    for(Card one_card: chance_cards){
        int card = one_card.getNumberInDeckInt();
        int offset = this->color_iso_offset[deal][one_card.getCardInt() % 4];
        if(offset < 0) {
            for(std::size_t x = 0;x < cards.size();x ++){
                if(
                    Card::card2int(cards[x]) ==
                    (Card::card2int(cards[card]) + offset)
                ){
                    card = x;
                    break;
                }
            }
            if(card == one_card.getNumberInDeckInt()){
                throw runtime_error("isomorphism not found while dump evs");
            }
            vector<int> one_exchange{one_card.getCardInt() % 4,one_card.getCardInt() % 4 + offset};
            exchange_color_list.push_back(one_exchange);
        }

        int new_deal;
        if(deal == 0){
            new_deal = card + 1;
        } else if (deal > 0 && deal <= card_num){
            int origin_deal = deal - 1;
            new_deal = card_num * origin_deal + card;
            new_deal += (1 + card_num);
        } else{
            throw runtime_error(tfm::format("deal out of range : %s ",deal));
        }
        deal = new_deal;
    }
    shared_ptr<Trainable> trainable = node->getTrainable(deal,false,this->use_halffloats);
    if(trainable == nullptr) {
        return ret_evs;
    }
    json retjson = trainable->dump_evs();

    for(vector<int> one_exchange:exchange_color_list){
        int rank1 = one_exchange[0];
        int rank2 = one_exchange[1];
        this->exchangeRange((retjson["evs"]),rank1,rank2,node);
    }

    int player = node->getPlayer();

    json& evs = retjson["evs"];
    for(std::size_t i = 0;i < this->ranges[player].size();i ++){
        PrivateCards pc = this->ranges[player][i];
        string one_range_str = pc.toString();
        if(!evs.contains(one_range_str)){
            for(auto one_key:evs.items()){
                cout << one_key.key() << endl;
            }
            cout << "evs: " << evs  << endl;
            cout << "Eror when get_evs in PCfrSolver" << endl;
            throw runtime_error(tfm::format("%s not exist in evs",one_range_str));
        }
        vector<float> one_evs = evs[one_range_str];
        bool intercept = false;
        for(auto one_card:chance_cards){
            if(one_card.getCardInt() == pc.card1 || one_card.getCardInt() == pc.card2){
                intercept = true;
            }
        }
        if(intercept) continue;
        ret_evs[pc.card1][pc.card2] = one_evs;
    }
    return ret_evs;
}

json PCfrSolver::dumps(bool with_status,int depth) {
    if(with_status == true){
        throw runtime_error("");
    }
    json retjson;
    this->reConvertJson(this->tree->getRoot(),retjson,"",0,depth,vector<string>({"begin"}),0,vector<vector<int>>());
    return std::move(retjson);
}
