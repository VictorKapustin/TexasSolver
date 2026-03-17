//
// Created by Xuefeng Huang on 2020/1/31.
//

#include "include/ranges/PrivateCards.h"

PrivateCards::PrivateCards() {
}

PrivateCards::PrivateCards(int card1, int card2, float weight) {
    this->card1 = card1;
    this->card2 = card2;
    this->weight = weight;
    this->relative_prob = 0;
    if (card1 > card2){
        this->hash_code = card1 * 52 + card2;
    }else{
        this->hash_code = card2 * 52 + card1;
    }
    this->board_long = Card::boardInt2long(card1) | Card::boardInt2long(card2);
}

uint64_t PrivateCards::toBoardLong() const {
    return this->board_long;
    //return Card::boardInts2long(this->card_vec);
}

int PrivateCards::hashCode() const {
    return this->hash_code;
}


string PrivateCards::toString() {
    if (card1 > card2) {
        return Card::intCard2Str(card1) + Card::intCard2Str(card2);
    }else{
        return Card::intCard2Str(card2) + Card::intCard2Str(card1);
    }
}

