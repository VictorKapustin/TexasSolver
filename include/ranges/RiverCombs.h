//
// Created by Xuefeng Huang on 2020/1/31.
//

#ifndef TEXASSOLVER_RIVERCOMBS_H
#define TEXASSOLVER_RIVERCOMBS_H


#include <include/ranges/PrivateCards.h>

class RiverCombs {
public:
    int rank;
    PrivateCards private_cards;
    int reach_prob_index;
    RiverCombs();
    RiverCombs(const PrivateCards& private_cards, int rank, int reach_prob_index);
};


#endif //TEXASSOLVER_RIVERCOMBS_H
