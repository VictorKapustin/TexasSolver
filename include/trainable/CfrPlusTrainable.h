//
// Created by Xuefeng Huang on 2020/1/31.
// Refactored to full production-ready CFR+ trainable.
//

#ifndef TEXASSOLVER_CFRPLUSTRAINABLE_H
#define TEXASSOLVER_CFRPLUSTRAINABLE_H

#include <include/nodes/ActionNode.h>
#include <include/ranges/PrivateCards.h>
#include "include/trainable/Trainable.h"
using namespace std;

class CfrPlusTrainable : public Trainable {
private:
    ActionNode& action_node;
    vector<PrivateCards>* privateCards;
    int action_number;
    int card_number;
    // Cumulative positive regrets (floor at 0, never negative).
    vector<float> r_plus;
    // Caches 1 / sum(r_plus) per hand for fast getcurrentStrategyInPlace (0 if sum==0).
    vector<float> r_plus_sum;
    // Linear average accumulator: sum of (t * sigma_t) per (action, hand).
    vector<float> cum_r_plus;
    vector<float> evs;

public:
    CfrPlusTrainable(vector<PrivateCards>* privateCards, ActionNode& actionNode);

    const vector<float> getAverageStrategy() override;
    const vector<float> getcurrentStrategy() override;
    void getcurrentStrategyInPlace(float* buffer) override;

    void updateRegrets(const vector<float>& regrets, int iteration_number, const vector<float>& reach_probs) override;
    void updateRegretsInPlace(const float* regrets, int iteration_number, const float* reach_probs) override;

    void setEv(const vector<float>& evs) override;
    void copyStrategy(Trainable* other_trainable) override;
    json dump_strategy(bool with_state) override;
    json dump_evs() override;
    bool isActionPrunable(int action_id) override;
    TrainableType get_type() override;
};

#endif //TEXASSOLVER_CFRPLUSTRAINABLE_H
