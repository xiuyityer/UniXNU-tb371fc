// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>

extern "C" kmod_info_t g_kernel_kmod_info;
extern "C" void native_legacy_display_static_init(void)
    __asm__("__GLOBAL__sub_I_NativeLegacyDisplay.cpp");
extern "C" IOService *native_legacy_display_allocate(void);
extern "C" kern_return_t OSKextLoadKextWithIdentifier(const char *);
extern "C" IORegistryPlane *gIODTPlane;

extern "C" {
IOService *tb371fc_gpu_presenter;
IOService *native_gop_presenter_create(void);
void native_gop_presenter_set_source(IOService *, IOService *);
void native_gpu_probe(void);
}
static IOService *pendingDisplay;
static IOService *activeDisplay;
static IOService *framebufferNub;
static bool startAttempted;

extern "C" void native_legacy_display_start(void);
extern "C" void native_legacy_display_late_start(void);
extern "C" void tb371fc_input_start(void);

static bool validConsole(PE_Video &video) {
    return !PE_current_console(&video) && video.v_baseAddr && video.v_width && video.v_height &&
        video.v_depth == 32 && !video.v_rotate && video.v_width <= 16384 && video.v_height <= 16384 &&
        video.v_rowBytes >= video.v_width * 4 && video.v_rowBytes <= UINT64_MAX / video.v_height;
}

extern "C" void native_legacy_display_start(void) {
    if (startAttempted) return;
    startAttempted = true;

    void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if (!handle) return;
    native_legacy_display_static_init();
    if (OSMetaClass::postModLoad(handle)) return;

    PE_Video video = {};
    if (!validConsole(video)) return;
    IOService *display = native_legacy_display_allocate();
    if (!display) return;
    if (!display->init()) {
        display->release();
        return;
    }
    pendingDisplay = display;
}

extern "C" void native_legacy_display_late_start(void) {
    IOService *display = pendingDisplay;
    if (!display || activeDisplay) return;
    pendingDisplay = nullptr;

    if (OSKextLoadKextWithIdentifier("com.apple.iokit.IOSurface")) {
        display->release();
        return;
    }
    OSDictionary *matching = IOService::nameMatching("IOCoreSurfaceRoot");
    IOService *surfaceRoot = matching ? IOService::waitForMatchingService(matching, 5000000000ULL) : nullptr;
    const bool surfaceReady = surfaceRoot != nullptr;
    OSSafeReleaseNULL(matching);
    OSSafeReleaseNULL(surfaceRoot);
    if (!surfaceReady) {
        display->release();
        return;
    }

    PE_Video video = {};
    if (!validConsole(video)) {
        display->release();
        return;
    }
    IOService *resource = IOService::getResourceService();
    IODeviceMemory *memory = IODeviceMemory::withRange(video.v_baseAddr, video.v_rowBytes * video.v_height);
    OSArray *ranges = OSArray::withCapacity(1);
    IOService *nub = new IOService;
    IORegistryEntry *displayTree = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
    OSData *type = OSData::withBytes("display-subsystem", sizeof("display-subsystem"));
    bool nubAttached = false, nubStarted = false, treeAttached = false;
    bool displayAttached = false, displayStarted = false;

    if (!resource || !memory || !ranges || !nub || !displayTree || !type || !nub->init()) goto cleanup;
    ranges->setObject(memory);
    nub->setDeviceMemory(ranges);
    nub->setName("NativeGOPFramebuffer");
    if (!(nubAttached = nub->attach(resource)) || !(nubStarted = nub->start(resource))) goto cleanup;
    if (!nub->setProperty("device_type", type)) goto cleanup;
    nub->setName("gop-display", gIODTPlane);
    if (!(treeAttached = nub->attachToParent(displayTree, gIODTPlane))) goto cleanup;

    {
        SInt32 score = 0;
        if (display->probe(nub, &score) != display) goto cleanup;
    }
    if (!(displayAttached = display->attach(nub)) || !(displayStarted = display->start(nub))) goto cleanup;

    activeDisplay = display;
    tb371fc_input_start();
    framebufferNub = nub;
    {
    unsigned gpu = 0;
    if (PE_parse_boot_argn("sheng-fb-gpu", &gpu, sizeof gpu) && gpu == 1) {
        IOService *presenter = native_gop_presenter_create();
        bool attached = false;
        if (presenter) native_gop_presenter_set_source(presenter, nub);
        if (presenter && presenter->init() && (attached = presenter->attach(nub)) && presenter->start(nub)) {
            tb371fc_gpu_presenter = presenter;
            printf("TB GPU: firmware scanout handoff registered\n");
        } else {
            if (attached) presenter->detach(nub);
            OSSafeReleaseNULL(presenter);
            printf("TB GPU: scanout handoff unavailable; CPU presenter active\n");
        }
    }
    native_gpu_probe();
    }
    OSSafeReleaseNULL(type);
    OSSafeReleaseNULL(displayTree);
    OSSafeReleaseNULL(ranges);
    OSSafeReleaseNULL(memory);
    return;

cleanup:
    if (displayStarted) display->stop(nub);
    if (displayAttached) display->detach(nub);
    if (treeAttached) nub->detachFromParent(displayTree, gIODTPlane);
    if (nubStarted) nub->stop(resource);
    if (nubAttached) nub->detach(resource);
    OSSafeReleaseNULL(type);
    OSSafeReleaseNULL(displayTree);
    OSSafeReleaseNULL(ranges);
    OSSafeReleaseNULL(memory);
    OSSafeReleaseNULL(nub);
    display->release();
}
