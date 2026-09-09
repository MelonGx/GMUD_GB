/* Palette data used by the NONBANKED framebuffer VBL ISR.  This translation
 * unit deliberately has no #pragma bank, so the data remains readable in HOME
 * no matter which switchable ROM bank the ISR interrupted. */
#include <gb/cgb.h>

const palette_color_t fb_pal_plane0[4] = {
    RGB(26, 29, 24), RGB(2, 3, 2), RGB(26, 29, 24), RGB(2, 3, 2)
};
const palette_color_t fb_pal_plane1[4] = {
    RGB(26, 29, 24), RGB(26, 29, 24), RGB(2, 3, 2), RGB(2, 3, 2)
};
