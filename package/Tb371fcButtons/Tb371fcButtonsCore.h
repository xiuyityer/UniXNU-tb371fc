/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef TB371FC_BUTTONS_CORE_H
#define TB371FC_BUTTONS_CORE_H
#include <stdint.h>

/* Target FDT: SID0 PON@800 power/resin; SID0 GPIO6@c500 active-low. */
#define TB_BUTTON_PON_PPID 0x008u
#define TB_BUTTON_GPIO_PPID 0x0c5u
#define TB_BUTTON_RT_STS 0x10u
/* NativeHidInput Consumer report2: Home bit0 is never generated here. */
#define TB_BUTTON_POWER 2u
#define TB_BUTTON_VOLUME_UP 4u
#define TB_BUTTON_VOLUME_DOWN 8u
static inline uint32_t tb_buttons_decode(uint8_t pon, uint8_t gpio) {
    return ((pon & 1u) ? TB_BUTTON_POWER : 0u) |
           ((pon & 2u) ? TB_BUTTON_VOLUME_DOWN : 0u) |
           ((gpio & 1u) ? 0u : TB_BUTTON_VOLUME_UP);
}
struct TbButtonsDebounce {uint32_t stable, candidate; unsigned seen;};
/* Three 10ms samples require at least20ms stable, exceeding DT's15ms. */
static inline int tb_buttons_step(struct TbButtonsDebounce *d, uint32_t mask) {
    if (mask != d->candidate) {d->candidate = mask; d->seen = 1;}
    else if (d->seen < 3) ++d->seen;
    if (d->seen < 3 || d->stable == d->candidate) return 0;
    d->stable = d->candidate;
    return 1;
}
#endif
