/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef TB371FC_TOUCH_REPORT_H
#define TB371FC_TOUCH_REPORT_H

#include <stddef.h>
#include <stdint.h>

#define TB371FC_TOUCH_PAYLOAD_BYTES 108u
#define TB371FC_TOUCH_REPORT_BYTES (TB371FC_TOUCH_PAYLOAD_BYTES + 1u)
#define TB371FC_TOUCH_CONTACTS 10u
#define TB371FC_HID_INPUT_X_MAX 30480u
#define TB371FC_HID_INPUT_Y_MAX 20320u

typedef struct {
  uint8_t id;
  uint16_t x;
  uint16_t y;
  uint8_t width;
  uint8_t pressure;
} Tb371fcTouchContact;

typedef struct {
  uint8_t count;
  Tb371fcTouchContact contacts[TB371FC_TOUCH_CONTACTS];
} Tb371fcTouchFrame;

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 for a valid frame and -1 for malformed input. */
int tb371fc_touch_decode(const uint8_t *report, size_t length,
                         Tb371fcTouchFrame *frame);

/* Reports include one SPI dummy byte before the 108-byte Novatek payload. */
int tb371fc_touch_checksum_ok(const uint8_t *report, size_t length);
/* 0 = point report, 1 = firmware handshake, 2 = watchdog recovery. */
int tb371fc_touch_recovery(const uint8_t *report, size_t length);

/* Convert native coordinates to the input ranges consumed by NativeHidInput. */
void tb371fc_touch_to_hid(uint16_t native_x, uint16_t native_y,
                          uint16_t *hid_x, uint16_t *hid_y);

#ifdef __cplusplus
}
#endif

#endif
