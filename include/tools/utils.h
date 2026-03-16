//
// Created by bytedance on 20.4.21.
//

#ifndef BINDSOLVER_UTILS_H
#define BINDSOLVER_UTILS_H
#include <include/ranges/PrivateCards.h>
#include <include/compairer/Compairer.h>
#include <include/Deck.h>
#include <include/ranges/RiverRangeManager.h>
#include <include/ranges/PrivateCardsManager.h>
#include <include/trainable/CfrPlusTrainable.h>
#include <include/trainable/DiscountedCfrTrainable.h>
#include <include/nodes/ChanceNode.h>
#include <include/nodes/TerminalNode.h>
#include <include/nodes/ShowdownNode.h>

template <typename T>
void exchange_color_ptr(T* value, int value_size, const vector<PrivateCards>& range, int rank1, int rank2, int* privateint2ind_scratch){
#ifdef DEBUG
    if((int)range.size() != value_size) throw runtime_error("size problem");
    if(rank1 >= rank2) throw runtime_error("rank value problem");
#endif
    if(value_size == 0) return;
    
    // We still need a temporary buffer for original indices, but we can reuse the scratch or put it on stack if small.
    // However, since this is called frequently, let's use the provided scratch for everything if possible.
    // The range.size() is typically 1326 or less.
    thread_local vector<int> self_ind;
    if(self_ind.size() < (size_t)value_size) self_ind.resize(value_size);

    int* privateint2ind = privateint2ind_scratch;
    std::fill(privateint2ind, privateint2ind + (52 * 52 * 2), -1);
    
    for(std::size_t i = 0; i < range.size(); i++){
        const PrivateCards& pc = range[i];
        int card1 = pc.card1;
        int card2 = pc.card2;
        if(card1 > card2){
            int tmp = card1;
            card1 = card2;
            card2 = tmp;
        }
        self_ind[i] = card1 * 52 + card2;

        if(card1 % 4 == rank1) card1 = card1 - rank1 + rank2;
        else if(card1 % 4 == rank2) card1 = card1 - rank2 + rank1;

        if(card2 % 4 == rank1) card2 = card2 - rank1 + rank2;
        else if(card2 % 4 == rank2) card2 = card2 - rank2 + rank1;

        if(card1 > card2){
            int tmp = card1;
            card1 = card2;
            card2 = tmp;
        }
        privateint2ind[card1 * 52 + card2] = i;
    }

    for(std::size_t i = 0; i < range.size(); i++) {
        if(self_ind[i] == -1) continue;
        int target_ind = privateint2ind[self_ind[i]];
        if(target_ind != (int)i && target_ind != -1){
            self_ind[target_ind] = -1;
            T tmp = value[i];
            value[i] = value[target_ind];
            value[target_ind] = tmp;
        }
    }
}

template <typename T>
void exchange_color(vector<T>& value, const vector<PrivateCards>& range, int rank1, int rank2, int* privateint2ind_scratch){
    exchange_color_ptr(value.data(), (int)value.size(), range, rank1, rank2, privateint2ind_scratch);
}

#endif //BINDSOLVER_UTILS_H
