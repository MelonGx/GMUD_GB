/* 自動產生:bin2banks.py,勿手改 */
#include "res.h"

extern const uint8_t mapblob_b0[];
extern const uint8_t mapblob_b1[];

static const uint8_t* const chunks[2] = {
    mapblob_b0,
    mapblob_b1,
};

const res_t mapblob_res = { 17, 2, chunks };
