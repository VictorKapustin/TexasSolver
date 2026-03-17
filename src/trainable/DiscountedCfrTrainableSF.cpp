// Half float version created by Martin Ostermann on 2022-5-16,
// based DiscountableCfrTrainable.h from Xuefeng Huang on 2020/1/31.

#include "include/trainable/DiscountedCfrTrainableSF.h"
//#define DEBUG;

DiscountedCfrTrainableSF::DiscountedCfrTrainableSF(vector<PrivateCards> *privateCards,
                                               ActionNode &actionNode) : action_node(actionNode) {
    this->privateCards = privateCards;
    this->action_number = action_node.getChildrens().size();
    this->card_number = privateCards->size();
    this->evs = vector<EvsStorage>(this->action_number * this->card_number, (EvsStorage) 0.0);
    this->r_plus = vector<RplusStorage>(this->action_number * this->card_number, (RplusStorage) 0.0);
    this->cum_r_plus = vector<CumRplusStorage>(this->action_number * this->card_number, (CumRplusStorage) 0.0);
    this->r_plus_sum = vector<float>(this->card_number, 0.0f);
}

bool DiscountedCfrTrainableSF::isAllZeros(const vector<float>& input_array) {
    for(float i:input_array){
        if (i != 0)return false;
    }
    return true;
}

const vector<float> DiscountedCfrTrainableSF::getAverageStrategy() {
    vector<float> average_strategy;
    average_strategy = vector<float>(this->action_number * this->card_number);
    for (int private_id = 0; private_id < this->card_number; private_id++) {
        float r_plus_sum = 0;
        for (int action_id = 0; action_id < action_number; action_id++) {
            int index = action_id * this->card_number + private_id;
            average_strategy[index] = this->cum_r_plus[index];
            r_plus_sum += average_strategy[index];
        }

        for (int action_id = 0; action_id < action_number; action_id++) {
            int index = action_id * this->card_number + private_id;
            if(r_plus_sum) {
                average_strategy[index] = average_strategy[index] / r_plus_sum;
            }else{
                average_strategy[index] = 1.0 / this->action_number;
            }
        }
    }
    return average_strategy;
}

const vector<float> DiscountedCfrTrainableSF::getcurrentStrategy() {
    return this->getcurrentStrategyNoCache();
}

void DiscountedCfrTrainableSF::copyStrategy(Trainable* other_trainable){
    DiscountedCfrTrainableSF* trainable = static_cast<DiscountedCfrTrainableSF*>(other_trainable);
    this->r_plus.assign(trainable->r_plus.begin(),trainable->r_plus.end());
    this->cum_r_plus.assign(trainable->cum_r_plus.begin(),trainable->cum_r_plus.end());
    this->r_plus_sum.assign(trainable->r_plus_sum.begin(), trainable->r_plus_sum.end());
}

const vector<float> DiscountedCfrTrainableSF::getcurrentStrategyNoCache() {
    vector<float> current_strategy;
    current_strategy = vector<float>(this->action_number * this->card_number);
    const float uniform_strategy = 1.0f / this->action_number;
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < this->card_number; private_id++) {
            int index = action_id * this->card_number + private_id;
            float inv_r_plus_sum = this->r_plus_sum[private_id];
            if(inv_r_plus_sum != 0.0f) {
                current_strategy[index] = max(0.0f,this->r_plus[index]) * inv_r_plus_sum;
            }else{
                current_strategy[index] = uniform_strategy;
            }
#ifdef DEBUG
            if(this->r_plus[index] != this->r_plus[index]) throw runtime_error("nan found");
#endif
        }
    }
    return current_strategy;
}

void DiscountedCfrTrainableSF::setEv(const vector<float>& evs){
    if(evs.size() != this->evs.size()) throw runtime_error("size mismatch in discountcfrtrainable setEV");
    for(std::size_t i = 0;i < evs.size();i ++) if(evs[i] == evs[i])this->evs[i] = evs[i];
}

void DiscountedCfrTrainableSF::getcurrentStrategyInPlace(float* buffer) {
    const float uniform_strategy = 1.0f / this->action_number;
    const float* const rp = r_plus.data();
    const float* const rps = r_plus_sum.data();
    const int N = card_number;
    for (int action_id = 0; action_id < action_number; action_id++) {
        const int base = action_id * N;
        #pragma GCC ivdep
        for (int private_id = 0; private_id < N; private_id++) {
            float inv_rps = rps[private_id];
            buffer[base + private_id] = (inv_rps != 0.0f)
                ? max(0.0f, rp[base + private_id]) * inv_rps
                : uniform_strategy;
        }
    }
}

void DiscountedCfrTrainableSF::updateRegrets(const vector<float>& regrets, int iteration_number, const vector<float>& reach_probs) {
    this->updateRegretsInPlace(regrets.data(), iteration_number, reach_probs.data());
}

void DiscountedCfrTrainableSF::updateRegretsInPlace(const float* regrets, int iteration_number, const float* reach_probs) {
    // Compute as double for precision, store as float to keep inner loop in float SIMD
    const double _acp = pow((double)iteration_number, (double)this->alpha);
    const float alpha_coef = (float)(_acp / (1.0 + _acp));

    // Completely bounded, no dynamic memory heap allocations inside the rapid recursion path!
    std::fill(this->r_plus_sum.begin(), this->r_plus_sum.end(), 0.0f);
    float* const rp = r_plus.data();
    float* const rps = r_plus_sum.data();
    const int N = card_number;
    for (int action_id = 0; action_id < action_number; action_id++) {
        const int base = action_id * N;
        #pragma GCC ivdep
        for (int private_id = 0; private_id < N; private_id++) {
            float v = rp[base + private_id] + regrets[base + private_id];
            float r = v * (v > 0.0f ? alpha_coef : beta);
            rp[base + private_id] = r;
            rps[private_id] += max(0.0f, r);
        }
    }

    for (int private_id = 0; private_id < N; private_id++) {
        if (rps[private_id] != 0.0f) {
            rps[private_id] = 1.0f / rps[private_id];
        }
    }

    // Strategy freeze: skip cum_r_plus accumulator update for converged trainables.
    bool do_cum_update;
    if (!cum_frozen_) {
        do_cum_update = true;
    } else {
        frozen_skip_count_++;
        do_cum_update = (frozen_skip_count_ >= 50);
        if (do_cum_update) frozen_skip_count_ = 0;
    }

    if (do_cum_update) {
        const float uniform_strategy = 1.0f / this->action_number;
        const float strategy_coef = (float)pow(((float)iteration_number / (iteration_number + 1)), (double)gamma);
        const float freeze_thr = Trainable::s_freeze_threshold;
        float* const cum = cum_r_plus.data();
        if (freeze_thr > 0.0f) {
            float max_delta = 0.0f;
            for (int action_id = 0; action_id < action_number; action_id++) {
                const int base = action_id * N;
                for (int private_id = 0; private_id < N; private_id++) {
                    float strat = (rps[private_id] != 0.0f)
                        ? max(0.0f, rp[base + private_id]) * rps[private_id]
                        : uniform_strategy;
                    float old_val = cum[base + private_id];
                    float new_val = old_val * theta + strat * strategy_coef;
                    float d = new_val - old_val; if (d < 0) d = -d;
                    if (d > max_delta) max_delta = d;
                    cum[base + private_id] = new_val;
                }
            }
            cum_frozen_ = (max_delta < freeze_thr);
            if (cum_frozen_) frozen_skip_count_ = 0;
        } else {
            for (int action_id = 0; action_id < action_number; action_id++) {
                const int base = action_id * N;
                #pragma GCC ivdep
                for (int private_id = 0; private_id < N; private_id++) {
                    float strat = (rps[private_id] != 0.0f)
                        ? max(0.0f, rp[base + private_id]) * rps[private_id]
                        : uniform_strategy;
                    cum[base + private_id] = cum[base + private_id] * theta + strat * strategy_coef;
                }
            }
        }
    }
}

json DiscountedCfrTrainableSF::dump_strategy(bool with_state) {
    if(with_state) throw runtime_error("state storage not implemented");

    json strategy;
    const vector<float>& average_strategy = this->getAverageStrategy();
    vector<GameActions>& game_actions = action_node.getActions();
    vector<string> actions_str;
    for(GameActions& one_action:game_actions) {
        actions_str.push_back(
                one_action.toString()
        );
    }

    for(std::size_t i = 0;i < this->privateCards->size();i ++){
        PrivateCards& one_private_card = (*this->privateCards)[i];
        vector<float> one_strategy(this->action_number);

        for(int j = 0;j < this->action_number;j ++){
            std::size_t strategy_index = j * this->privateCards->size() + i;
            one_strategy[j] = average_strategy[strategy_index];
        }
        strategy[tfm::format("%s",one_private_card.toString())] = one_strategy;
    }

    json retjson;
    retjson["actions"] = std::move(actions_str);
    retjson["strategy"] = std::move(strategy);
    return std::move(retjson);
}

json DiscountedCfrTrainableSF::dump_evs() {
    json evs;
    const vector<EvsStorage>& average_evs = this->evs;
    vector<GameActions>& game_actions = action_node.getActions();
    vector<string> actions_str;
    for(GameActions& one_action:game_actions) {
        actions_str.push_back(
                one_action.toString()
        );
    }

    for(std::size_t i = 0;i < this->privateCards->size();i ++){
        PrivateCards& one_private_card = (*this->privateCards)[i];
        vector<float> one_evs(this->action_number);

        for(int j = 0;j < this->action_number;j ++){
            std::size_t evs_index = j * this->privateCards->size() + i;
            one_evs[j] = average_evs[evs_index];
        }
        evs[tfm::format("%s",one_private_card.toString())] = one_evs;
    }

    json retjson;
    retjson["actions"] = std::move(actions_str);
    retjson["evs"] = std::move(evs);
    return std::move(retjson);
}

bool DiscountedCfrTrainableSF::isActionPrunable(int action_id) {
    const int offset = action_id * this->card_number;
    for (int i = 0; i < this->card_number; i++) {
        if (this->r_plus[offset + i] > 0.0f) return false;
    }
    return true;
}

Trainable::TrainableType DiscountedCfrTrainableSF::get_type() {
    return DISCOUNTED_CFR_TRAINABLE;
}
