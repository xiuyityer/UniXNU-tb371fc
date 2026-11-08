// SPDX-License-Identifier: BSD-2-Clause-Patent
// Target-specific port of UniXNU-pipa Buttons.c and Linux v6.12 PMIC arbiter.
// Observer read commands only: no PMIC configuration/reset writes.
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "Tb371fcButtonsCore.h"
extern "C" {
volatile uint32_t tb371fc_buttons_stage, tb371fc_buttons_mask, tb371fc_buttons_changes;
volatile uint32_t tb371fc_buttons_errors, tb371fc_buttons_samples;
volatile int tb371fc_buttons_error;
}
namespace {
constexpr uint64_t coreBase = 0x0c440000, configBase = 0x0c40a000, observerBase = 0x0e600000;
constexpr unsigned ee = 0, maxApids = 512;
void (*sink)(void *, uint32_t);
void *sinkContext;
struct Region {
    IOMemoryMap *mapping = nullptr;
    uintptr_t va = 0;
    bool map(uint64_t physical, uint64_t size) {
        IODeviceMemory *memory = IODeviceMemory::withRange(physical, size);
        if (!memory) return false;
        mapping = memory->map(kIOMapAnywhere | kIOMapInhibitCache);
        memory->release();
        va = mapping ? mapping->getVirtualAddress() : 0;
        return va != 0;
    }
};
uint32_t rd(uintptr_t base, unsigned offset) {return *reinterpret_cast<volatile uint32_t *>(base + offset);}
void command(uintptr_t base, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(base) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
int read8(uintptr_t observer, uint8_t address, uint8_t &value) {
    // Linux PMIC_ARB_OP_EXT_READL, byte count0 (one byte), v5 address encoding.
    command(observer, (1u << 27) | (static_cast<uint32_t>(address) << 4));
    for (unsigned poll = 0; poll < 1000; ++poll) {
        uint32_t status = rd(observer, 8);
        if (status & 1) {
            if (status & 0xe) return -5;
            value = static_cast<uint8_t>(rd(observer, 0x18));
            return 0;
        }
        IODelay(1);
    }
    return -110;
}
int findApid(uintptr_t core, uintptr_t config, unsigned count, unsigned ppid) {
    int result = -1;
    // Linux v5: retain first PPID, prefer each subsequent APID owned by this EE.
    // Observer channels remain read-only even if no write-owned APID exists.
    for (unsigned i = 0; i < count; ++i) {
        uint32_t entry = rd(core, 0x900 + 4 * i);
        if (!entry || ((entry >> 8) & 0xfff) != ppid) continue;
        if (result < 0 || (rd(config, 0x700 + 4 * i) & 7) == ee) result = static_cast<int>(i);
    }
    return result;
}
void publish(uint32_t mask) {
    tb371fc_buttons_mask = mask;
    tb371fc_buttons_changes = tb371fc_buttons_changes + 1;
    auto callback = __atomic_load_n(&sink, __ATOMIC_ACQUIRE);
    if (callback) callback(__atomic_load_n(&sinkContext, __ATOMIC_ACQUIRE), mask);
}
void fail(unsigned stage, int error) {
    tb371fc_buttons_stage = stage; tb371fc_buttons_error = error;
    printf("TB BUTTONS: stage=%u error=%d samples=%u changes=%u\n",stage,error,
           tb371fc_buttons_samples,tb371fc_buttons_changes);
}
void run(void *, wait_result_t) {
    // Mappings live for the boot; no reset/restart loop on ambiguous IO failure.
    Region *core = new Region, *config = new Region, *observers = new Region;
    if (!core || !config || !observers || !core->map(coreBase,0x1100) ||
        !config->map(configBase,0x1000) || !observers->map(observerBase,0x10000)) {fail(1,-12); return;}
    uint32_t version = rd(core->va,0), count = rd(core->va,4) & 0x7ff;
    if (version < 0x50000000 || version >= 0x70000000 || !count || count > maxApids) {fail(1,-19); return;}
    int ponApid = findApid(core->va,config->va,count,TB_BUTTON_PON_PPID);
    int gpioApid = findApid(core->va,config->va,count,TB_BUTTON_GPIO_PPID);
    printf("TB BUTTONS: arbiter=%08x EE=%u APIDs=%u PON=%d GPIO6=%d\n",version,ee,count,ponApid,gpioApid);
    if (ponApid < 0 || gpioApid < 0) {fail(2,-19); return;}
    uintptr_t pon = observers->va + 0x10000 * ee + 0x80 * static_cast<unsigned>(ponApid);
    uintptr_t gpio = observers->va + 0x10000 * ee + 0x80 * static_cast<unsigned>(gpioApid);
    uint8_t type=0, subtype=0, mode=0, pull=0, enable=0;
    int rc = read8(gpio,4,type);
    if (!rc) rc = read8(gpio,5,subtype);
    if (!rc) rc = read8(gpio,0x40,mode);
    if (!rc) rc = read8(gpio,0x42,pull);
    if (!rc) rc = read8(gpio,0x46,enable);
    printf("TB BUTTONS: GPIO6 type=%02x subtype=%02x mode=%02x pull=%02x enable=%02x read=%d\n",
           type,subtype,mode,pull,enable,rc);
    const bool lowVoltage = subtype >= 0x10 && subtype <= 0x13;
    unsigned direction = lowVoltage ? (mode & 3) : ((mode >> 4) & 7);
    if (rc || type != 0x10 || !(enable & 0x80) || direction != 0) {fail(3,rc ? rc : -22); return;}
    tb371fc_buttons_stage = 4;
    printf("TB BUTTONS: power/volume producer started; read-only PMIC, debounce=20ms\n");
    TbButtonsDebounce debounce = {};
    unsigned errors = 0;
    for (;;) {
        IOSleep(10);
        uint8_t ponStatus = 0, gpioStatus = 0;
        rc = read8(pon,TB_BUTTON_RT_STS,ponStatus);
        if (!rc) rc = read8(gpio,TB_BUTTON_RT_STS,gpioStatus);
        tb371fc_buttons_samples = tb371fc_buttons_samples + 1;
        if (rc) {
            tb371fc_buttons_errors = tb371fc_buttons_errors + 1;
            debounce.seen = 0; // Failed reads cannot count toward consecutive samples.
            if (++errors < 3) continue;
            if (tb371fc_buttons_mask) publish(0); // Never leave a held HID key on IO loss.
            fail(5,rc); return;
        }
        errors = 0;
        if (!tb_buttons_step(&debounce,tb_buttons_decode(ponStatus,gpioStatus))) continue;
        publish(debounce.stable);
        printf("TB BUTTONS: mask=%x pon=%02x gpio6=%02x changes=%u\n",debounce.stable,
               ponStatus,gpioStatus,tb371fc_buttons_changes);
    }
}
}
extern "C" void native_buttons_set_sink(void (*callback)(void *, uint32_t), void *context) {
    __atomic_store_n(&sink,static_cast<void (*)(void *, uint32_t)>(nullptr),__ATOMIC_RELEASE);
    __atomic_store_n(&sinkContext,context,__ATOMIC_RELEASE);
    __atomic_store_n(&sink,callback,__ATOMIC_RELEASE);
}
extern "C" void tb371fc_buttons_start(void) {
    static bool started;
    unsigned enabled = 0;
    if (started || !PE_parse_boot_argn("tb371fc-buttons",&enabled,sizeof enabled) || enabled != 1) return;
    started = true;
    tb371fc_buttons_stage = 1;
    thread_t worker = THREAD_NULL;
    kern_return_t rc = kernel_thread_start(run,nullptr,&worker);
    if (!rc) thread_deallocate(worker); else fail(0,rc);
}
