#include <stdint.h>
extern "C" int boot_log_export_put(uint32_t generation, uint32_t total,
    uint32_t offset, const void *data, uint32_t size) {
    (void)generation; (void)total; (void)offset; (void)data; (void)size;
    return -4;
}
