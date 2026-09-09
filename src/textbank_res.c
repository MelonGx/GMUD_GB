/* 自動產生:bin2banks.py,勿手改 */
#include "res.h"

extern const uint8_t textbank_b0[];
extern const uint8_t textbank_b1[];
extern const uint8_t textbank_b2[];

static const uint8_t* const chunks[3] = {
    textbank_b0,
    textbank_b1,
    textbank_b2,
};

const res_t textbank_res = { 12, 3, chunks };
