// SPDX-License-Identifier: BSD-2-Clause-Patent
// Callbacks for the pinned upstream CPU presenter. No user-client interposition.
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>

extern "C" {
int tb371fc_console_handoff(void);
static IOService *mappingOwner;
static unsigned validationReports, transferReports, clientReports;
static unsigned long long completedFrames;

bool native_legacy_gop_mapping_observed(IOService *owner, IOMemoryMap *mapping,
                                      uint32_t width, uint32_t height) {
    PE_Video video = {};
    if (!owner || !mapping || PE_current_console(&video) ||
        video.v_width != width || video.v_height != height || video.v_depth != 32 ||
        video.v_rotate || !mapping->getVirtualAddress() ||
        mapping->getPhysicalAddress() != video.v_baseAddr ||
        mapping->getLength() != video.v_rowBytes * video.v_height ||
        (mappingOwner && mappingOwner != owner)) return false;
    mappingOwner = owner;
    printf("TB DISPLAY: GOP mapped %ux%u stride=%lu bytes=%llu\n", width, height,
           (unsigned long)video.v_rowBytes, (unsigned long long)mapping->getLength());
    return true;
}

bool native_legacy_prepare_scanout(void) {
    if (!mappingOwner || ml_at_interrupt_context() || !ml_get_interrupts_enabled()) return false;
    // A busy console writer causes the frame to fail, with no spin or retry loop.
    return tb371fc_console_handoff() != 0;
}

__attribute__((noreturn)) void native_legacy_backend_guard(unsigned slot, void *object, void *caller) {
    panic("TB DISPLAY: unsupported backend slot=%u object=%p caller=%p", slot, object, caller);
}
void native_legacy_registration_observed(IOService *) {
    printf("TB DISPLAY: framebuffer registered\n");
}
void native_legacy_pm_observed(unsigned stage, IOReturn status, unsigned long ordinal) {
    printf("TB DISPLAY: PM stage=%u status=%x ordinal=%lu\n", stage, (unsigned)status, ordinal);
}
void native_legacy_client_enter(task_t, uint32_t) { }
void native_legacy_client_observed(IOReturn status, IOUserClient *) {
    if (clientReports++ < 8) printf("TB DISPLAY: client status=%x\n", (unsigned)status);
}
void native_legacy_swap_validation_observed(bool accepted, unsigned reason, const void *, const void *) {
    if (validationReports++ < 8)
        printf("TB DISPLAY: swap accepted=%u reason=%u\n", (unsigned)accepted, reason);
}
void native_legacy_transfer_observed(IOReturn status, const void *, uint64_t bytes) {
    if (transferReports++ < 8)
        printf("TB DISPLAY: transfer status=%x bytes=%llu\n", (unsigned)status, (unsigned long long)bytes);
}
void native_legacy_retirement_observed(IOService *) {
    if (++completedFrames <= 8) printf("TB DISPLAY: completed=%llu\n", completedFrames);
}
// The pinned implementation calls these optional observation callbacks. They
// do not participate in validation, copying or original family completion.
void native_legacy_frame_observed(unsigned, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) { }
void native_legacy_frame_colors_observed(const uint32_t *, const uint64_t *, uint64_t) { }
void native_legacy_surface_rejected(uint64_t, uint64_t, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t) { }
void native_legacy_swap_stage(unsigned) { }
void native_legacy_copy_timing_observed(uint64_t, uint64_t) { }
void native_legacy_vsync_observed(unsigned, unsigned) { }
// Offer the validated surface to the upstream presenter; it declines until
// the real GPU client is ready, preserving the existing CPU copy fallback.
extern IOService *tb371fc_gpu_presenter;
bool native_gop_gpu_offer(IOService *, void *, IOMemoryDescriptor *, uint64_t, uint32_t, uint32_t);
bool native_legacy_gpu_offer(void *display, IOMemoryDescriptor *memory, uint64_t stride, uint32_t width, uint32_t height) {
    return tb371fc_gpu_presenter && native_gop_gpu_offer(tb371fc_gpu_presenter, display, memory, stride, width, height);
}
void native_legacy_gpu_fallback_observed(int) { }
}
