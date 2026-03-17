// Half float version created by Martin Ostermann on 2022-5-12,
// based DiscountableCfrTrainable.h from Xuefeng Huang on 2020/1/31.

#include "include/trainable/DiscountedCfrTrainableHF.h"
//#define DEBUG;

DiscountedCfrTrainableHF::DiscountedCfrTrainableHF(vector<PrivateCards> *privateCards,
                                               ActionNode &actionNode) : action_node(actionNode) {
    this->privateCards = privateCards;
    this->action_number = action_node.getChildrens().size();
    this->card_number = privateCards->size();
    this->evs = vector<EvsStorage>(this->action_number * this->card_number, (EvsStorage) 0.0);
    this->r_plus = vector<RplusStorage>(this->action_number * this->card_number, (RplusStorage) 0.0);
    this->cum_r_plus = vector<CumRplusStorage>(this->action_number * this->card_number, (CumRplusStorage) 0.0);
    this->r_plus_sum = vector<float>(this->card_number, 0.0f);
}

bool DiscountedCfrTrainableHF::isAllZeros(const vector<float>& input_array) {
    for(float i:input_array){
        if (i != 0)return false;
    }
    return true;
}

const vector<float> DiscountedCfrTrainableHF::getAverageStrategy() {
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
                // we stored this->cum_r_plus[index] in average_strategy[index] above
                // this is to avoid converting from half float twice
                average_strategy[index] = average_strategy[index] / r_plus_sum;
            }else{
                average_strategy[index] = 1.0 / this->action_number;
            }
        }
    }
    return average_strategy;
}

const vector<float> DiscountedCfrTrainableHF::getcurrentStrategy() {
    return this->getcurrentStrategyNoCache();
}

void DiscountedCfrTrainableHF::copyStrategy(Trainable* other_trainable){
    DiscountedCfrTrainableHF* trainable = static_cast<DiscountedCfrTrainableHF*>(other_trainable);
    this->r_plus.assign(trainable->r_plus.begin(),trainable->r_plus.end());
    this->cum_r_plus.assign(trainable->cum_r_plus.begin(),trainable->cum_r_plus.end());
    this->r_plus_sum.assign(trainable->r_plus_sum.begin(), trainable->r_plus_sum.end());
}

const vector<float> DiscountedCfrTrainableHF::getcurrentStrategyNoCache() {
    vector<float> current_strategy;
    current_strategy = vector<float>(this->action_number * this->card_number);
    const float uniform_strategy = 1.0f / this->action_number;
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < this->card_number; private_id++) {
            int index = action_id * this->card_number + private_id;
            float this_r_plus_of_index = this->r_plus[index];
            float inv_r_plus_sum = this->r_plus_sum[private_id];
            if(inv_r_plus_sum != 0.0f) {
                current_strategy[index] = max(0.0f,this_r_plus_of_index) * inv_r_plus_sum;
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

void DiscountedCfrTrainableHF::setEv(const vector<float>& evs){
    if(evs.size() != this->evs.size()) throw runtime_error("size mismatch in discountcfrtrainable setEV");
    for(std::size_t i = 0;i < evs.size();i ++) if(evs[i] == evs[i])this->evs[i] = evs[i];
}

void DiscountedCfrTrainableHF::getcurrentStrategyInPlace(float* buffer) {
    const float uniform_strategy = 1.0f / this->action_number;
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < this->card_number; private_id++) {
            int index = action_id * this->card_number + private_id;
            float val = (float)this->r_plus[index];
            float inv_r_plus_sum = this->r_plus_sum[private_id];
            if (inv_r_plus_sum != 0.0f) {
                buffer[index] = max(0.0f, val) * inv_r_plus_sum;
            } else {
                buffer[index] = uniform_strategy;
            }
        }
    }
}

void DiscountedCfrTrainableHF::updateRegrets(const vector<float>& regrets, int iteration_number, const vector<float>& reach_probs) {
    this->updateRegretsInPlace(regrets.data(), iteration_number, reach_probs.data());
}

void DiscountedCfrTrainableHF::updateRegretsInPlace(const float* regrets, int iteration_number, const float* reach_probs) {
    auto alpha_coef = pow(iteration_number, this->alpha);
    alpha_coef = alpha_coef / (1 + alpha_coef);

    std::fill(this->r_plus_sum.begin(), this->r_plus_sum.end(), 0.0f);
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < this->card_number; private_id++) {
            int index = action_id * this->card_number + private_id;
            float one_reg = regrets[index];

            // 更新 R+
            float this_r_plus_of_index = (float)this->r_plus[index];
            this_r_plus_of_index = one_reg + this_r_plus_of_index;
            if (this_r_plus_of_index > 0) {
                this_r_plus_of_index *= alpha_coef;
            } else {
                this_r_plus_of_index *= beta;
            }
            this->r_plus_sum[private_id] += max(0.0f, this_r_plus_of_index);
            this->r_plus[index] = (half)this_r_plus_of_index;
        }
    }

    for (int private_id = 0; private_id < this->card_number; private_id++) {
        if (this->r_plus_sum[private_id] != 0.0f) {
            this->r_plus_sum[private_id] = 1.0f / this->r_plus_sum[private_id];
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
        float strategy_coef = pow(((float)iteration_number / (iteration_number + 1)), gamma);
        const float freeze_thr = Trainable::s_freeze_threshold;
        if (freeze_thr > 0.0f) {
            float max_delta = 0.0f;
            for (int action_id = 0; action_id < action_number; action_id++) {
                for (int private_id = 0; private_id < this->card_number; private_id++) {
                    int index = action_id * this->card_number + private_id;
                    float inv_r_plus_sum = this->r_plus_sum[private_id];
                    float strat = (inv_r_plus_sum != 0.0f)
                        ? max(0.0f, (float)this->r_plus[index]) * inv_r_plus_sum
                        : uniform_strategy;
                    float old_val = (float)this->cum_r_plus[index];
                    float new_val = old_val * this->theta + strat * strategy_coef;
                    float d = new_val - old_val; if (d < 0) d = -d;
                    if (d > max_delta) max_delta = d;
                    this->cum_r_plus[index] = (half)new_val;
                }
            }
            cum_frozen_ = (max_delta < freeze_thr);
            if (cum_frozen_) frozen_skip_count_ = 0;
        } else {
            for (int action_id = 0; action_id < action_number; action_id++) {
                for (int private_id = 0; private_id < this->card_number; private_id++) {
                    int index = action_id * this->card_number + private_id;
                    float inv_r_plus_sum = this->r_plus_sum[private_id];
                    float strat = (inv_r_plus_sum != 0.0f)
                        ? max(0.0f, (float)this->r_plus[index]) * inv_r_plus_sum
                        : uniform_strategy;
                    this->cum_r_plus[index] = (half)((float)this->cum_r_plus[index] * this->theta + strat * strategy_coef);
                }
            }
        }
    }
}

json DiscountedCfrTrainableHF::dump_strategy(bool with_state) {
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
            int strategy_index = j * this->privateCards->size() + i;
            one_strategy[j] = average_strategy[strategy_index];
        }
        strategy[tfm::format("%s",one_private_card.toString())] = one_strategy;
    }

    json retjson;
    retjson["actions"] = std::move(actions_str);
    retjson["strategy"] = std::move(strategy);
    return std::move(retjson);
}

json DiscountedCfrTrainableHF::dump_evs() {
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
            int evs_index = j * this->privateCards->size() + i;
            one_evs[j] = average_evs[evs_index];
        }
        evs[tfm::format("%s",one_private_card.toString())] = one_evs;
    }

    json retjson;
    retjson["actions"] = std::move(actions_str);
    retjson["evs"] = std::move(evs);
    return std::move(retjson);
}

bool DiscountedCfrTrainableHF::isActionPrunable(int action_id) {
    const int offset = action_id * this->card_number;
    for (int i = 0; i < this->card_number; i++) {
        if (float(this->r_plus[offset + i]) > 0.0f) return false;
    }
    return true;
}

Trainable::TrainableType DiscountedCfrTrainableHF::get_type() {
    return DISCOUNTED_CFR_TRAINABLE;
}
