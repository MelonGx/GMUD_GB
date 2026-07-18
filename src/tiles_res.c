/* 自動產生:bin2banks.py,勿手改 */
#include "res.h"

extern const uint8_t tiles_b0[];
extern const uint8_t tiles_b1[];
extern const uint8_t tiles_b2[];
extern const uint8_t tiles_b3[];
extern const uint8_t tiles_b4[];
extern const uint8_t tiles_b5[];
extern const uint8_t tiles_b6[];
extern const uint8_t tiles_b7[];

static const uint8_t* const chunks[8] = {
    tiles_b0,
    tiles_b1,
    tiles_b2,
    tiles_b3,
    tiles_b4,
    tiles_b5,
    tiles_b6,
    tiles_b7,
};

const res_t tiles_res = { 9, 8, chunks };
