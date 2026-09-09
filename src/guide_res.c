/* 自動產生:bin2banks.py,勿手改 */
#include "res.h"

extern const uint8_t guide_b0[];
extern const uint8_t guide_b1[];

static const uint8_t* const chunks[2] = {
    guide_b0,
    guide_b1,
};

const res_t guide_res = { 29, 2, chunks };
