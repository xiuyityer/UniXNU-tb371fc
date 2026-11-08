// SPDX-License-Identifier: BSD-2-Clause-Patent
// Runtime registration follows UniXNU-pipa's HidInputProbe. The service uses
// the original, build-qualified iOS 17 IOHIDDevice and generic event matching.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSKext.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSIterator.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
extern "C" kmod_info_t g_kernel_kmod_info;
extern "C" void native_hid_static_init(void) __asm__("__GLOBAL__sub_I_NativeHidInput.cpp");
extern "C" IOService *native_hid_input_allocate(void);
extern "C" kern_return_t OSKextLoadKextWithIdentifier(const char *);
extern "C" void tb371fc_touch_start(void);
extern "C" void tb371fc_buttons_start(void);
// The upstream combined device matches through its keyboard collection. Once
// matched, backboardd requires the event service and interface to identify as
// an integrated touch screen. Publish once; repeated property changes wake hidd.
static IORegistryEntry *findTouchEventService(IORegistryEntry *parent, unsigned depth) {
    if (!depth) return nullptr;
    OSIterator *iterator = parent->getChildIterator(gIOServicePlane);
    if (!iterator) return nullptr;
    IORegistryEntry *found = nullptr;
    while (OSObject *child = iterator->getNextObject()) {
        IORegistryEntry *entry = OSDynamicCast(IORegistryEntry, child);
        if (!entry) continue;
        if (entry->metaCast("IOHIDEventDriver")) {found = entry; break;}
        if ((found = findTouchEventService(entry, depth - 1))) break;
    }
    iterator->release();
    return found;
}
extern "C" {
volatile uint32_t tb371fc_hid_stage, tb371fc_hid_reports, tb371fc_hid_errors;
volatile int tb371fc_hid_error;
bool native_hid_input_requested(void) {return false;}
void native_hid_live_checkpoint(IOService *device) {
    static IORegistryEntry *published;
    IORegistryEntry *driver = findTouchEventService(device, 4);
    if (!driver || published == driver || !device->getProperty("Built-In")) return;
    published = driver;
    IORegistryEntry *entries[] = {driver, driver->getParentEntry(gIOServicePlane)};
    for (IORegistryEntry *entry : entries) {
        if (!entry) continue;
        entry->setProperty("Built-In", kOSBooleanTrue);
        entry->setProperty("DisplayIntegrated", kOSBooleanTrue);
        entry->setProperty("PrimaryUsagePage", (unsigned long long)0x0d, 32);
        entry->setProperty("PrimaryUsage", (unsigned long long)4, 32);
    }
    printf("TB HID: integrated touch event service classified\n");
}
void native_hid_input_observed(unsigned event, int result) {
    if (event == 6) {
        (tb371fc_hid_reports = tb371fc_hid_reports + 1);
        if (result) {
            tb371fc_hid_errors = tb371fc_hid_errors + 1;
            if (tb371fc_hid_errors <= 8) printf("TB HID: touch report error=%x\n", result);
        }
    } else if (event != 3) printf("TB HID: event=%u result=%x\n", event, result);
}
void tb371fc_input_start(void) {
    unsigned enabled = 0;
    if (tb371fc_hid_stage || !PE_parse_boot_argn("tb371fc-touch", &enabled, sizeof enabled) || enabled != 1) return;
    tb371fc_hid_stage = 1;
    int error = OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDFamily");
    if (!error) error = OSKext::loadKextWithIdentifier("com.apple.iokit.IOHIDEventDriver", true, false,
                                                     kOSKextExcludeNone, kOSKextExcludeNone, nullptr);
    tb371fc_hid_error = error;
    if (error) {printf("TB HID: family/matching load error=%x\n", error); return;}
    const OSSymbol *name = OSSymbol::withCString("IOHIDDevice");
    const OSMetaClass *meta = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
    const unsigned size = meta ? meta->getClassSize() : 0;
    OSSafeReleaseNULL(name);
    if (size != 208) {tb371fc_hid_error = -22; printf("TB HID: unexpected IOHIDDevice size=%u\n", size); return;}
    OSDictionary *match = IOService::serviceMatching("IOHIDResource");
    IOService *resource = match ? IOService::waitForMatchingService(match, 5000000000ULL) : nullptr;
    OSSafeReleaseNULL(match);
    if (!resource) {tb371fc_hid_error = -19; return;}
    void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if (!handle) {resource->release(); tb371fc_hid_error = -12; return;}
    native_hid_static_init();
    error = OSMetaClass::postModLoad(handle);
    if (error) {resource->release(); tb371fc_hid_error = error; return;}
    // Upstream's combined keyboard+digitizer service is its physically tested
    // generic-event-driver matching arrangement. No synthetic keys are sent.
    IOService *device = native_hid_input_allocate();
    bool valid = device && device->metaCast("IOHIDDevice") && device->init();
    bool attached = valid && device->attach(resource);
    bool started = attached && device->start(resource);
    if (!started) {
        if (attached) device->detach(resource);
        OSSafeReleaseNULL(device);
        resource->release(); tb371fc_hid_error = -5; printf("TB HID: service start failed\n"); return;
    }
    resource->release();
    tb371fc_hid_stage = 2;
    printf("TB HID: digitizer started; starting SE14 producer\n");
    tb371fc_buttons_start();
    tb371fc_touch_start();
}
}
