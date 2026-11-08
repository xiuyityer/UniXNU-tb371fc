// SPDX-License-Identifier: MIT
// Adapted from UniXNU-sheng 552b67499135bae191f415f444d782a6942c252e.
// Host-reviewed CPU0 compatibility only; real original nub, no Apple MMIO.
// No clocks, sleep, reset or secondary-CPU success is advertised.
#include "OriginalPMGRABI.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/c++/OSSymbol.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "PublicationLifecycle.h"

extern "C" {
extern kmod_info_t g_kernel_kmod_info;
kern_return_t OSKextLoadKextWithIdentifier(const char *);
void tb371fc_pmgr_static_init(void) __asm__("__GLOBAL__sub_I_Tb371fcPlatformPower.cpp");
IOService *tb371fc_original_pmgr_nub_create(IORegistryEntry *, ApplePMGR *);
uint32_t tb371fc_original_pmgr_nub_read(IOService *);
void tb371fc_original_pmgr_nub_write(IOService *, uint64_t);
// stage,status,parent-size,own-size,provider,nub,bound-provider,read-before,
// read-after,own-before,own-after,nub-before,nub-after,attached,registered,mode.
volatile uint64_t tb371fc_pmgr_observation[16];
__attribute__((noinline)) void tb371fc_pmgr_checkpoint(void) { __asm__ volatile("" ::: "memory"); }
}

__attribute__((noreturn)) static void unavailable(unsigned slot)
{
  tb371fc_pmgr_observation[0] = 0x100 + slot;
  tb371fc_pmgr_observation[1] = static_cast<uint32_t>(kIOReturnUnsupported);
  tb371fc_pmgr_checkpoint();
  panic("Tb371fcPlatformPower: unimplemented operation %u", slot);
}

class Tb371fcPlatformPower : public ApplePMGR {
  OSDeclareDefaultStructors(Tb371fcPlatformPower)
public:
  void enableCPUCore(unsigned, uint64_t) override { unavailable(168); }
  void enableCPUCore(unsigned) override { unavailable(169); }
  void disableCPUCore(unsigned) override { unavailable(170); }
  void enableCPUCluster(unsigned) override { unavailable(171); }
  void disableCPUCluster(unsigned) override { unavailable(172); }
  void initCPUIdle(ml_processor_info_t *) override { unavailable(173); }
  void enterCPUIdle(UInt64 *) override { unavailable(174); }
  void exitCPUIdle(UInt64 *) override { unavailable(175); }
  void updateCPUIdle(UInt64 *) override { unavailable(176); }
#include "GuardedPMGRSlots.inc"
};
OSDefineMetaClassWithInit(Tb371fcPlatformPower, ApplePMGR, )
OSMetaClassConstructorInit(Tb371fcPlatformPower, ApplePMGR, )
OSDefineBasicStructors(Tb371fcPlatformPower, ApplePMGR)
// The original object's storage exceeds kalloc_type's 16 KiB size limit.
// Use XNU's existing zeroed, size-aware OSObject heap allocation pair.
void *Tb371fcPlatformPower::operator new(size_t size) { return OSObject::operator new(size); }
void Tb371fcPlatformPower::operator delete(void *memory, size_t size)
{
  OSObject::operator delete(memory, size);
}
static_assert(sizeof(Tb371fcPlatformPower) == 0x4b8a8, "Exact original provider storage");

static const OSMetaClass *lookup(const char *text)
{
  const OSSymbol *name = OSSymbol::withCString(text);
  const OSMetaClass *meta = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
  OSSafeReleaseNULL(name);
  return meta;
}

extern "C" void tb371fc_pmgr_run(void)
{
  // Reachable only from the isolated opt-in pre-MAC bootstrap hook.
  const unsigned mode = 2;
  tb371fc_pmgr_observation[15] = mode;
  tb371fc_pmgr_observation[1] = static_cast<uint32_t>(kIOReturnError);
  if (mode > 3 || tb371fc_pmgr_observation[0]) { tb371fc_pmgr_checkpoint(); return; }
  tb371fc_pmgr_observation[0] = 1;
  kern_return_t result = OSKextLoadKextWithIdentifier("com.apple.driver.ApplePMGR");
  if (result) { tb371fc_pmgr_observation[1] = static_cast<uint32_t>(result); tb371fc_pmgr_checkpoint(); return; }
  const OSMetaClass *parent = lookup("ApplePMGR"), *nubMeta = lookup("ApplePMGRNub");
  if (!parent || parent != &ApplePMGR::gMetaClass || parent->getClassSize() != 0x4b8a8 ||
      !nubMeta || nubMeta->getClassSize() != 0x740) { tb371fc_pmgr_checkpoint(); return; }
  tb371fc_pmgr_observation[2] = parent->getClassSize();
  void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
  if (!handle) { tb371fc_pmgr_checkpoint(); return; }
  tb371fc_pmgr_static_init();
  result = OSMetaClass::postModLoad(handle);
  if (result) { tb371fc_pmgr_observation[1] = static_cast<uint32_t>(result); tb371fc_pmgr_checkpoint(); return; }
  tb371fc_pmgr_observation[9] = Tb371fcPlatformPower::gMetaClass.getInstanceCount();
  tb371fc_pmgr_observation[11] = nubMeta->getInstanceCount();
  Tb371fcPlatformPower *provider = new Tb371fcPlatformPower;
  IORegistryEntry *description = new IORegistryEntry;
  IOService *nub = nullptr;
  Tb371fcPublicationLifecycle<IOService> lifecycle;
  const char *failed = "unknown publication substep";
  do {
    if (!provider) { failed = "provider allocation"; break; }
    if (!description) { failed = "description allocation"; break; }
    if (!provider->init()) { failed = "provider.init"; break; }
    if (!description->init()) { failed = "description.init"; break; }
    tb371fc_pmgr_observation[0] = 2;
    tb371fc_pmgr_observation[3] = provider->getMetaClass()->getClassSize();
    tb371fc_pmgr_observation[4] = reinterpret_cast<uintptr_t>(provider);
    // Empty resource description: no Apple register map or device capabilities.
    description->setName("tb371fc-platform-power");
    nub = tb371fc_original_pmgr_nub_create(description, provider);
    tb371fc_pmgr_observation[5] = reinterpret_cast<uintptr_t>(nub);
    if (!nub) { failed = "original nub creation"; break; }
    if (nub->getMetaClass() != nubMeta) { failed = "original nub metaclass"; break; }
    if (!provider->metaCast("ApplePMGR")) { failed = "provider metacast"; break; }
    uintptr_t bound = 0;
    memcpy(&bound, reinterpret_cast<const char *>(nub) + 0x88, sizeof(bound));
    tb371fc_pmgr_observation[6] = bound;
    if (bound != reinterpret_cast<uintptr_t>(provider)) { failed = "nub provider binding"; break; }
    tb371fc_pmgr_observation[0] = 3;
    uint32_t value = tb371fc_original_pmgr_nub_read(nub);
    tb371fc_pmgr_observation[7] = value;
    tb371fc_original_pmgr_nub_write(nub, uint64_t(value | 4));
    tb371fc_pmgr_observation[8] = tb371fc_original_pmgr_nub_read(nub);
    tb371fc_pmgr_observation[13] = nub->getProvider() != nullptr || provider->getProvider() != nullptr;
    tb371fc_pmgr_observation[14] = (nub->getState() | provider->getState()) & kIOServiceRegisteredState;
    // Inherited original ApplePMGR base readAon/writeAon slots are no-MMIO.
    // They represent unsupported AON storage; no power capability is claimed.
    if (value != 0xffffffff) { failed = "original AON read"; break; }
    if (tb371fc_pmgr_observation[8] != 0xffffffff) { failed = "original AON read after write"; break; }
    if (tb371fc_pmgr_observation[13]) { failed = "unexpected pre-attach provider"; break; }
    if (tb371fc_pmgr_observation[14]) { failed = "unexpected pre-attach registration"; break; }
    if (mode == 3) provider->disableCPUCore(1);
    if (mode == 2) {
      IOService *platform = IOService::getPlatform();
      if (!platform) {
        // InitIOKit attaches this genuine root before early MAC initialization.
        // A platform expert is matched later; do not fabricate/set one here.
        IOService *root = IOService::getServiceRoot();
        if (!root) { failed = "IOService root unavailable"; break; }
        if (!OSDynamicCast(IOPlatformExpertDevice, root)) {
          failed = "IOService root type"; break;
        }
        if (root->getProvider() != nullptr ||
            root->getParentEntry(gIOServicePlane) != IORegistryEntry::getRegistryRoot()) {
          failed = "IOService root parent contract"; break;
        }
        platform = root;
        IOLog("TB371FC PMGR: using original IOKit service root before platform match\n");
      }
      if (!lifecycle.attach(provider, nub, platform)) { failed = lifecycle.failure(); break; }
      lifecycle.commit(); // Registered objects are retained permanently.
      provider->registerService(kIOServiceSynchronous);
      nub->registerService(kIOServiceSynchronous);
      tb371fc_pmgr_observation[0] = 5;
      tb371fc_pmgr_observation[13] = nub->getProvider() == provider && provider->getProvider() == platform;
      tb371fc_pmgr_observation[14] = (nub->getState() & provider->getState()) & kIOServiceRegisteredState;
      auto matching = IOService::serviceMatching("ApplePMGRNub");
      IOService *matched = matching ? IOService::waitForMatchingService(matching, 1000000) : nullptr;
      OSSafeReleaseNULL(matching);
      // Check actual service discovery and genuine type identity, then retain
      // both live objects. The registry and original nub refer to this provider.
      if (matched != nub || matched->getMetaClass() != nubMeta ||
          !tb371fc_pmgr_observation[13] || !tb371fc_pmgr_observation[14]) {
        tb371fc_pmgr_checkpoint();
        panic("Tb371fcPlatformPower: original nub publication failed");
      }
      tb371fc_pmgr_observation[10] = Tb371fcPlatformPower::gMetaClass.getInstanceCount();
      tb371fc_pmgr_observation[12] = nubMeta->getInstanceCount();
      tb371fc_pmgr_observation[1] = kIOReturnSuccess;
      OSSafeReleaseNULL(matched);
      OSSafeReleaseNULL(description);
      tb371fc_pmgr_checkpoint();
      return;
    }
    tb371fc_pmgr_observation[1] = kIOReturnSuccess;
  } while (false);
  const uint64_t failed_stage = tb371fc_pmgr_observation[0];
  lifecycle.rollback();
  OSSafeReleaseNULL(nub);
  OSSafeReleaseNULL(description);
  OSSafeReleaseNULL(provider);
  tb371fc_pmgr_observation[10] = Tb371fcPlatformPower::gMetaClass.getInstanceCount();
  tb371fc_pmgr_observation[12] = nubMeta->getInstanceCount();
  if (tb371fc_pmgr_observation[9] != tb371fc_pmgr_observation[10] ||
      tb371fc_pmgr_observation[11] != tb371fc_pmgr_observation[12]) tb371fc_pmgr_observation[1] = static_cast<uint32_t>(kIOReturnError);
  tb371fc_pmgr_observation[0] = 4;
  tb371fc_pmgr_checkpoint();
  panic("Tb371fcPlatformPower: publication failed at %s stage=%llu status=%#llx AON=%#llx/%#llx",
      failed, failed_stage, tb371fc_pmgr_observation[1],
      tb371fc_pmgr_observation[7], tb371fc_pmgr_observation[8]);
}
