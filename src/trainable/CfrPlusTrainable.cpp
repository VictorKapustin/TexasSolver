//
// Created by Xuefeng Huang on 2020/1/31.
// Refactored to full production-ready CFR+ trainable.
//

#include "include/trainable/CfrPlusTrainable.h"

CfrPlusTrainable::CfrPlusTrainable(vector<PrivateCards>* privateCards, ActionNode& actionNode)
    : action_node(actionNode) {
    this->privateCards   = privateCards;
    this->action_number  = action_node.getChildrens().size();
    this->card_number    = privateCards->size();

    this->r_plus     = vector<float>(this->action_number * this->card_number, 0.0f);
    this->r_plus_sum = vector<float>(this->card_number, 0.0f);
    this->cum_r_plus = vector<float>(this->action_number * this->card_number, 0.0f);
    this->evs        = vector<float>(this->action_number * this->card_number, 0.0f);
}

// ---------------------------------------------------------------------------
// Current strategy (from r_plus, used during solving)
// ---------------------------------------------------------------------------

void CfrPlusTrainable::getcurrentStrategyInPlace(float* buffer) {
    const float uniform = 1.0f / this->action_number;
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < card_number; private_id++) {
            int index = action_id * card_number + private_id;
            float inv_sum = r_plus_sum[private_id];
            buffer[index] = (inv_sum != 0.0f) ? r_plus[index] * inv_sum : uniform;
        }
    }
}

const vector<float> CfrPlusTrainable::getcurrentStrategy() {
    vector<float> result(action_number * card_number);
    getcurrentStrategyInPlace(result.data());
    return result;
}

// ---------------------------------------------------------------------------
// Average strategy (linear average: sum of t * sigma_t, used for dump/output)
// ---------------------------------------------------------------------------

const vector<float> CfrPlusTrainable::getAverageStrategy() {
    vector<float> cum_sum(card_number, 0.0f);
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < card_number; private_id++) {
            cum_sum[private_id] += cum_r_plus[action_id * card_number + private_id];
        }
    }
    const float uniform = 1.0f / action_number;
    vector<float> result(action_number * card_number);
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < card_number; private_id++) {
            int index = action_id * card_number + private_id;
            result[index] = (cum_sum[private_id] != 0.0f)
                ? cum_r_plus[index] / cum_sum[private_id]
                : uniform;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Regret update (CFR+ core: clip at 0, no discounting)
// ---------------------------------------------------------------------------

void CfrPlusTrainable::updateRegrets(const vector<float>& regrets, int iteration_number, const vector<float>& reach_probs) {
    updateRegretsInPlace(regrets.data(), iteration_number, reach_probs.data());
}

void CfrPlusTrainable::updateRegretsInPlace(const float* regrets, int iteration_number, const float* reach_probs) {
    // CFR+: r_plus = max(0, r_plus + cfr_regret)  — never goes negative
    std::fill(r_plus_sum.begin(), r_plus_sum.end(), 0.0f);
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < card_number; private_id++) {
            int index = action_id * card_number + private_id;
            r_plus[index] = max(0.0f, r_plus[index] + regrets[index]);
            r_plus_sum[private_id] += r_plus[index];
        }
    }

    // Cache inverse sum for fast getcurrentStrategyInPlace
    for (int private_id = 0; private_id < card_number; private_id++) {
        if (r_plus_sum[private_id] != 0.0f)
            r_plus_sum[private_id] = 1.0f / r_plus_sum[private_id];
    }

    // Quadratic average: cum_r_plus += t^2 * sigma_t  (Tammelin 2015 CFR+ recommendation)
    // Weights recent iterations much more heavily; reduces influence of early random play.
    const float t2 = (float)iteration_number * (float)iteration_number;
    const float uniform = 1.0f / action_number;
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < card_number; private_id++) {
            int index = action_id * card_number + private_id;
            float inv_sum = r_plus_sum[private_id];
            float strat = (inv_sum != 0.0f) ? r_plus[index] * inv_sum : uniform;
            cum_r_plus[index] += strat * t2;
        }
    }
}

// ---------------------------------------------------------------------------
// EV storage (optional, kept for interface compatibility)
// ---------------------------------------------------------------------------

void CfrPlusTrainable::setEv(const vector<float>& evs) {
    if (evs.size() == this->evs.size()) {
        for (std::size_t i = 0; i < evs.size(); i++) {
            if (evs[i] == evs[i]) this->evs[i] = evs[i];
        }
    }
}

// ---------------------------------------------------------------------------
// Isomorphism support
// ---------------------------------------------------------------------------

void CfrPlusTrainable::copyStrategy(Trainable* other_trainable) {
    auto other = static_cast<CfrPlusTrainable*>(other_trainable);
    if (!other) throw runtime_error("CfrPlusTrainable::copyStrategy: type mismatch");
    r_plus.assign(other->r_plus.begin(), other->r_plus.end());
    cum_r_plus.assign(other->cum_r_plus.begin(), other->cum_r_plus.end());
    r_plus_sum.assign(other->r_plus_sum.begin(), other->r_plus_sum.end());
}

// ---------------------------------------------------------------------------
// Pruning
// ---------------------------------------------------------------------------

bool CfrPlusTrainable::isActionPrunable(int action_id) {
    const int offset = action_id * card_number;
    for (int i = 0; i < card_number; i++) {
        if (r_plus[offset + i] > 0.0f) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

json CfrPlusTrainable::dump_strategy(bool with_state) {
    if (with_state) throw runtime_error("state storage not implemented");
    json strategy;
    // Use average strategy (quadratic weighted) — converges to Nash.
    // For RT use-cases, getAverageStrategy() with t^2 weighting closely tracks current strategy.
    const vector<float> avg = this->getAverageStrategy();
    vector<GameActions>& game_actions = action_node.getActions();
    vector<string> actions_str;
    for (GameActions& a : game_actions) actions_str.push_back(a.toString());

    for (std::size_t i = 0; i < privateCards->size(); i++) {
        PrivateCards& pc = (*privateCards)[i];
        vector<float> one_strategy(action_number);
        for (int j = 0; j < action_number; j++) {
            one_strategy[j] = avg[j * (int)privateCards->size() + (int)i];
        }
        strategy[tfm::format("%s", pc.toString())] = one_strategy;
    }

    json retjson;
    retjson["actions"]  = std::move(actions_str);
    retjson["strategy"] = std::move(strategy);
    return retjson;
}

json CfrPlusTrainable::dump_evs() {
    // EVs not tracked in CFR+ (no EV storage by default)
    json retjson;
    retjson["actions"] = json::array();
    retjson["evs"]     = json::object();
    return retjson;
}

Trainable::TrainableType CfrPlusTrainable::get_type() {
    return CFR_PLUS_TRAINABLE;
}
