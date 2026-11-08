// SPDX-License-Identifier: BSD-2-Clause-Patent
// TB371FC board lifecycle. GENI/Novatek sequencing follows UniXNU-pipa
// cdb3d62771e31d48d832853ee0c90964346d78da; all board values are TB371FC.
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "Tb371fcTouchBoard.h"
#include "Tb371fcTouchPublish.h"
extern "C" {
#include "GeniSpi.h"
#include "NvtProtocol.h"
extern const uint8_t native_touch_firmware[];
extern const uint32_t native_touch_firmware_size;
volatile uint32_t tb371fc_touch_stage, tb371fc_touch_reads, tb371fc_touch_valid;
volatile uint32_t tb371fc_touch_bad, tb371fc_touch_recoveries, tb371fc_touch_edges;
volatile int tb371fc_touch_error;
}
namespace {
struct Region {
    IOMemoryMap *mapping = nullptr;
    uintptr_t va = 0;
    bool map(uint64_t physical, uint64_t bytes) {
        IODeviceMemory *memory = IODeviceMemory::withRange(physical, bytes);
        if (!memory) return false;
        mapping = memory->map(kIOMapAnywhere | kIOMapInhibitCache);
        memory->release();
        va = mapping ? mapping->getVirtualAddress() : 0;
        return va != 0;
    }
};
uint32_t rd(uintptr_t base, unsigned reg) {return *reinterpret_cast<volatile uint32_t *>(base + reg);}
void wr(uintptr_t base, unsigned reg, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(base + reg) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
uint32_t ioRead(void *p, uint32_t r) {return rd(reinterpret_cast<uintptr_t>(p), r);}
void ioWrite(void *p, uint32_t r, uint32_t v) {wr(reinterpret_cast<uintptr_t>(p), r, v);}
void ioDelay(void *, unsigned us) {IODelay(us);}
struct Session {
    Region gccQup, gccCore, gccVote, spiPins, resetPin, irqPin, panelPin, wrapper, engine;
    geni_spi spi = {};
    nvt_bus bus = {};
    nvt_chip chip = {};
    nvt_fw firmware = {};
    nvt_fw_info info = {};
    uint64_t clocks[8] = {};
    unsigned selectedLevel = 0, selectedHz = 0;
};
uint64_t clockRate(uint32_t cfg, uint32_t m, uint32_t n) {
    unsigned source = (cfg >> 8) & 7, hid = cfg & 31, mode = (cfg >> 12) & 3;
    uint64_t hz = source == 0 ? 19200000ULL : source == 1 ? 600000000ULL : source == 6 ? 300000000ULL : 0;
    if (hid) hz = hz * 2 / (hid + 1);
    if (mode) {m &= 0xffff; n = ((~n) & 0xffff) + m; if (!n) return 0; hz = hz * m / n;}
    return hz;
}
bool stage(unsigned n, int error = 0) {
    tb371fc_touch_stage = n; tb371fc_touch_error = error;
    printf("TB TOUCH: stage=%u error=%d reads=%u valid=%u bad=%u edges=%u recover=%u\n", n, error,
           tb371fc_touch_reads, tb371fc_touch_valid, tb371fc_touch_bad, tb371fc_touch_edges, tb371fc_touch_recoveries);
    return error == 0;
}
int transfer(void *context, const uint8_t *tx, uint8_t *rx, size_t length) {
    Session *s = static_cast<Session *>(context);
    int rc = geni_spi_transfer(&s->spi, tx, rx, length, 0);
    if (rc && !tb371fc_touch_error) {
        tb371fc_touch_error = rc;
        printf("TB TOUCH: SPI error=%d len=%lu irq=%x status=%x tx=%x rx=%x\n", rc,
               (unsigned long)length, s->spi.snap_irq, s->spi.snap_status, s->spi.snap_txfs, s->spi.snap_rxfs);
    }
    return rc;
}
void delayMs(void *, unsigned ms) {IOSleep(ms);}
void reset(void *context, int asserted) {
    Session *s = static_cast<Session *>(context);
    // Output value before OE avoids a transient high pulse while asserting reset.
    wr(s->resetPin.va, 4, asserted ? 0 : 2);
    wr(s->resetPin.va, 0, (TB371FC_TOUCH_DRIVE_6MA << 6) | (1u << 9));
}
bool startBus(Session &s) {
    stage(1);
    if (!s.gccQup.map(TB371FC_TOUCH_GCC_BASE + 0x1e000, 0x1000) ||
        !s.gccCore.map(TB371FC_TOUCH_GCC_BASE + 0x23000, 0x1000) ||
        !s.gccVote.map(TB371FC_TOUCH_GCC_BASE + 0x52000, 0x1000) ||
        !s.spiPins.map(TB371FC_TOUCH_TLMM_BASE + tb371fc_touch_tlmm_ctl(40), 0x4000) ||
        !s.resetPin.map(TB371FC_TOUCH_TLMM_BASE + tb371fc_touch_tlmm_ctl(56), 0x1000) ||
        !s.irqPin.map(TB371FC_TOUCH_TLMM_BASE + tb371fc_touch_tlmm_ctl(68), 0x1000) ||
        !s.panelPin.map(TB371FC_TOUCH_TLMM_BASE + tb371fc_touch_tlmm_ctl(82), 0x1000))
        return stage(1, -12);
    // GPIO82=1 is the measured Tianma panel; refuse the other panel's firmware.
    wr(s.panelPin.va, 0, rd(s.panelPin.va, 0) & ~((15u << 2) | (1u << 9)));
    unsigned panel = rd(s.panelPin.va, 4) & 1;
    printf("TB TOUCH: panel-id=%u firmware-bytes=%u\n", panel, native_touch_firmware_size);
    if (panel != 1 || native_touch_firmware_size != 245760) return stage(1, -19);
    wr(s.gccVote.va, 0x10, rd(s.gccVote.va, 0x10) | TB371FC_TOUCH_GCC_VOTE_MASK);
    auto running = [](uint32_t value) {return !(value & (1u << 31)) || (value & 2);};
    bool on = false;
    for (unsigned i = 0; i < 2000; ++i) {
        on = running(rd(s.gccQup.va, 4)) && running(rd(s.gccQup.va, 8)) &&
            !(rd(s.gccQup.va, 0xc) & (1u << 31)) && !(rd(s.gccCore.va, 0x270) & (1u << 31)) &&
            !(rd(s.gccCore.va, 0x278) & (1u << 31));
        if (on) break;
        IODelay(10);
    }
    if (!on) return stage(2, -110);
    const bool dfs = rd(s.gccQup.va, 0x24) & 1;
    for (unsigned i = 0; i < (dfs ? 8u : 1u); ++i) {
        unsigned cfg = dfs ? 0x2c + 4 * i : 0x14;
        unsigned m = dfs ? 0x6c + 4 * i : 0x18;
        unsigned n = dfs ? 0xac + 4 * i : 0x1c;
        s.clocks[i] = clockRate(rd(s.gccQup.va, cfg), rd(s.gccQup.va, m), rd(s.gccQup.va, n));
    }
    for (unsigned i = 0; i < 4; ++i)
        wr(s.spiPins.va, i * TB371FC_TOUCH_GPIO_STRIDE, (TB371FC_TOUCH_QUP14_FUNC << 2) | (TB371FC_TOUCH_DRIVE_6MA << 6));
    // IRQ is an input. Latch rising edges without delivering an interrupt to XNU.
    wr(s.irqPin.va, 0, (TB371FC_TOUCH_DRIVE_6MA << 6));
    if (!s.wrapper.map(TB371FC_TOUCH_QUP_BASE, TB371FC_TOUCH_QUP_SIZE) ||
        !s.engine.map(TB371FC_TOUCH_SE_BASE, TB371FC_TOUCH_SE_SIZE)) return stage(3, -12);
    s.spi.se = {reinterpret_cast<void *>(s.engine.va), ioRead, ioWrite, ioDelay};
    s.spi.wrapper = {reinterpret_cast<void *>(s.wrapper.va), ioRead, ioWrite, ioDelay};
    s.spi.poll_limit = 400000;
    s.spi.poll_only = 2;
    // Linux spi-geni-qcom uses FIFO when no GPI channel is available. This path
    // does not program a foreign SMMU bank or DMA into UFS's existing context.
    s.spi.allow_fifo_disabled = 1;
    int rc = geni_spi_init(&s.spi);
    printf("TB TOUCH: GENI proto=%u fifo-disabled=%u depth=%u width=%u dfs=%u\n", s.spi.info.protocol,
           s.spi.info.fifo_disabled, s.spi.info.tx_fifo_depth, s.spi.info.fifo_width_bits, dfs);
    if (rc) return stage(3, rc);
    unsigned best = 0;
    for (unsigned i = 0; i < 8; ++i) {
        uint64_t hz = s.clocks[i], denominator = (uint64_t)s.spi.oversampling * TB371FC_TOUCH_SPI_MAX_HZ;
        if (!hz) continue;
        uint64_t div = (hz + denominator - 1) / denominator;
        if (!div || div > 0xfff) continue;
        unsigned actual = static_cast<unsigned>(hz / (s.spi.oversampling * div));
        if (actual > best && actual <= TB371FC_TOUCH_SPI_MAX_HZ) {best = actual; s.selectedLevel = i;}
    }
    if (!best) return stage(4, -22);
    s.selectedHz = best;
    rc = geni_spi_set_clock(&s.spi, TB371FC_TOUCH_SPI_MAX_HZ, static_cast<unsigned>(s.clocks[s.selectedLevel]), s.selectedLevel);
    if (!rc) rc = geni_spi_setup(&s.spi, 0, 0); // Target Linux SPI_MODE_0, CS0.
    printf("TB TOUCH: clock-level=%u source=%llu spi=%u\n", s.selectedLevel,
           (unsigned long long)s.clocks[s.selectedLevel], s.selectedHz);
    return stage(4, rc);
}
void armAttention(Session &s) {
    wr(s.irqPin.va, 8, (1u << 4) | (1u << 2) | (1u << 1));
    wr(s.irqPin.va, 12, 0);
}
bool download(Session &s) {
    int rc = nvt_download_firmware(&s.chip, &s.firmware);
    if (rc) {
        printf("TB TOUCH: download=%d attempt=%u phase=%u reset=%02x crc=%02x\n", rc,
               s.chip.dl_attempt, s.chip.dl_stage, s.chip.dl_reset_state[0], s.chip.crc_flags);
        return stage(6, rc);
    }
    rc = nvt_start_reports(&s.chip, &s.info);
    printf("TB TOUCH: firmware=%02x pid=%04x range=%ux%u cascade=%u start=%d\n", s.info.fw_ver,
           s.info.pid, s.info.abs_x_max, s.info.abs_y_max, s.chip.cascade, rc);
    if (rc || s.info.pid != 0x6068 || s.info.abs_x_max != 18400 || s.info.abs_y_max != 29440)
        return stage(7, rc ? rc : -19);
    armAttention(s);
    return stage(8);
}
void run(void *, wait_result_t) {
    // Session and mappings live for the boot. Do not tear down an engine with
    // ambiguous completion; no restart thread or reboot loop is created.
    Session *s = new Session;
    if (!s) {stage(0, -12); return;}
    if (!startBus(*s)) return;
    s->bus = {s, transfer, delayMs, reset};
    s->chip.bus = &s->bus;
    s->chip.scratch = static_cast<uint8_t *>(IOMalloc(NVT_TRANSFER_LEN + 8));
    s->chip.rxbuf = static_cast<uint8_t *>(IOMalloc(NVT_READ_MAX + 2));
    if (!s->chip.scratch || !s->chip.rxbuf) {stage(5, -12); return;}
    int rc = nvt_identify(&s->chip);
    printf("TB TOUCH: identify=%d trim=%02x%02x%02x%02x%02x%02x map=%s\n", rc, s->chip.id[0], s->chip.id[1],
           s->chip.id[2], s->chip.id[3], s->chip.id[4], s->chip.id[5], s->chip.map ? s->chip.map->name : "none");
    if (rc || !s->chip.cascade || !s->chip.map || s->chip.map->event_buf != 0x125800) {stage(5, rc ? rc : -19); return;}
    rc = nvt_fw_parse(&s->firmware, native_touch_firmware, native_touch_firmware_size);
    if (rc) {stage(6, rc); return;}
    if (!download(*s)) return;
    unsigned idle = 0, watchdog = 0, errors = 0, published = 0, ticks = 0;
    uint8_t report[TB371FC_TOUCH_REPORT_BYTES] = {};
    for (;;) {
        IOSleep(2);
        if (++ticks % 5000 == 0) stage(8); // Ten-second compact status in normal kernel log.
        bool edge = (rd(s->irqPin.va, 12) & 1) != 0;
        if (edge) {wr(s->irqPin.va, 12, 0); (tb371fc_touch_edges = tb371fc_touch_edges + 1);}
        if (!edge && ++idle < 250) continue;
        idle = 0;
        rc = nvt_read_points(&s->chip, report + 1); // nvt_read strips the wire address/dummy.
        (tb371fc_touch_reads = tb371fc_touch_reads + 1);
        if (rc) {
            Tb371fcTouchFrame empty = {}; if (published) {tb371fc_touch_publish(&empty); published = 0;}
            if (++errors >= 3) {stage(9, rc); return;} // Do not spin forever on a dead bus.
            continue;
        }
        errors = 0;
        int recovery = tb371fc_touch_recovery(report, sizeof report);
        if (recovery) {
            (tb371fc_touch_recoveries = tb371fc_touch_recoveries + 1);
            Tb371fcTouchFrame empty = {}; if (published) {tb371fc_touch_publish(&empty); published = 0;}
            if (recovery == 2 && ++watchdog >= 10) {watchdog = 0; if (!download(*s)) return;}
            continue;
        }
        watchdog = 0;
        Tb371fcTouchFrame frame = {};
        if (tb371fc_touch_decode(report, sizeof report, &frame)) {(tb371fc_touch_bad = tb371fc_touch_bad + 1); continue;}
        (tb371fc_touch_valid = tb371fc_touch_valid + 1);
        if (frame.count || published) tb371fc_touch_publish(&frame);
        published = frame.count;
    }
}
}
extern "C" void tb371fc_touch_start(void) {
    static bool started;
    unsigned enabled = 0;
    if (started || !PE_parse_boot_argn("tb371fc-touch", &enabled, sizeof enabled) || enabled != 1) return;
    started = true;
    thread_t worker = THREAD_NULL;
    kern_return_t rc = kernel_thread_start(run, nullptr, &worker);
    if (rc == KERN_SUCCESS) thread_deallocate(worker);
    else stage(0, rc);
}
