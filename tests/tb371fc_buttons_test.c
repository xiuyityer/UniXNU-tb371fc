#include <assert.h>
#include "../package/Tb371fcButtons/Tb371fcButtonsCore.h"
int main(void) {
    for (unsigned state = 0; state < 8; ++state) {
        unsigned pon = (state & 1) | ((state >> 1) & 2);
        unsigned gpio = (state & 2) ? 0 : 1;
        assert(tb_buttons_decode(pon,gpio) == state * 2);
        assert(!(tb_buttons_decode(pon,gpio) & 1));
    }
    struct TbButtonsDebounce d = {0};
    assert(!tb_buttons_step(&d,TB_BUTTON_POWER));
    assert(!tb_buttons_step(&d,0));
    assert(!tb_buttons_step(&d,TB_BUTTON_POWER));
    assert(!tb_buttons_step(&d,TB_BUTTON_POWER));
    assert(tb_buttons_step(&d,TB_BUTTON_POWER));
    assert(d.stable == TB_BUTTON_POWER);
    assert(!tb_buttons_step(&d,TB_BUTTON_POWER));
    assert(!tb_buttons_step(&d,14));
    d.seen = 0; // IO failure must break consecutive debounce samples.
    assert(!tb_buttons_step(&d,14));
    assert(!tb_buttons_step(&d,14));
    assert(tb_buttons_step(&d,14));
    assert(d.stable == 14);
    assert(!tb_buttons_step(&d,0));
    assert(!tb_buttons_step(&d,0));
    assert(tb_buttons_step(&d,0));
    assert(d.stable == 0);
    return 0;
}
