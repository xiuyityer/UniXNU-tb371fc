#include "../package/Tb371fcTouch/Tb371fcTouchReport.h"
#include "../package/Tb371fcTouch/Tb371fcTouchPublish.h"

#include <assert.h>
#include <string.h>

extern volatile uint32_t native_touch_seq, native_touch_count;
extern volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];

static unsigned notifications;
static void notified(void *context)
{
  assert(context == &notifications);
  ++notifications;
}

static void set_contact(uint8_t *report, unsigned slot, uint8_t id,
                        uint8_t state, uint16_t x, uint16_t y,
                        uint8_t width, uint8_t pressure)
{
  unsigned pos = 1u + 6u * slot;
  report[pos] = (uint8_t)((id << 3) | state);
  report[pos + 1] = (uint8_t)(x >> 8);
  report[pos + 2] = (uint8_t)x;
  report[pos + 3] = (uint8_t)(y >> 8);
  report[pos + 4] = (uint8_t)y;
  report[pos + 5] = width;
  report[99u + slot] = pressure;
}

static void set_checksum(uint8_t *report)
{
  uint8_t sum = 0;
  unsigned i;
  for (i = 1; i < 65; ++i)
    sum = (uint8_t)(sum + report[i]);
  report[65] = (uint8_t)(0u - sum);
}

int main(void)
{
  uint8_t report[TB371FC_TOUCH_REPORT_BYTES] = {0};
  Tb371fcTouchFrame frame;
  uint16_t x, y;

  set_contact(report, 0, 4, 1, 0, 0, 0, 0);
  set_contact(report, 1, 9, 2, 18400, 29440, 12, 80);
  set_contact(report, 9, 10, 1, 9200, 14720, 5, 127);
  set_checksum(report);
  assert(tb371fc_touch_checksum_ok(report, sizeof(report)));
  assert(tb371fc_touch_recovery(report, sizeof(report)) == 0);
  assert(tb371fc_touch_decode(report, sizeof(report), &frame) == 0);
  assert(frame.count == 3);
  assert(frame.contacts[0].id == 4 && frame.contacts[0].width == 1 &&
         frame.contacts[0].pressure == 1);
  assert(frame.contacts[1].id == 9 && frame.contacts[1].x == 18400 &&
         frame.contacts[1].y == 29440 && frame.contacts[1].pressure == 80);
  assert(frame.contacts[2].id == 10 && frame.contacts[2].pressure == 127);
  report[65] ^= 1;
  assert(!tb371fc_touch_checksum_ok(report, sizeof(report)));
  assert(tb371fc_touch_decode(report, sizeof(report), &frame) == -1);

  memset(report, 0, sizeof(report));
  set_checksum(report);
  assert(tb371fc_touch_decode(report, sizeof(report), &frame) == 0);
  assert(frame.count == 0);
  assert(tb371fc_touch_decode(report, TB371FC_TOUCH_PAYLOAD_BYTES, &frame) == -1);
  assert(tb371fc_touch_decode(0, sizeof(report), &frame) == -1);

  set_contact(report, 0, 1, 1, 0, 0, 1, 1);
  set_contact(report, 1, 1, 2, 10, 10, 1, 1);
  set_checksum(report);
  assert(tb371fc_touch_decode(report, sizeof(report), &frame) == -1);
  memset(report, 0, sizeof(report));
  set_contact(report, 1, 2, 2, 18401, 10, 1, 1);
  set_checksum(report);
  assert(tb371fc_touch_decode(report, sizeof(report), &frame) == -1);

  memset(report, 0, sizeof(report));
  memset(report + 1, 0x77, 6);
  assert(tb371fc_touch_recovery(report, sizeof(report)) == 1);
  assert(tb371fc_touch_decode(report, sizeof(report), &frame) == -2);
  memset(report + 1, 0xfd, 6);
  assert(tb371fc_touch_recovery(report, sizeof(report)) == 2);
  assert(tb371fc_touch_decode(report, sizeof(report), &frame) == -2);
  report[1] = 0;
  assert(tb371fc_touch_recovery(report, sizeof(report)) == 0);
  assert(tb371fc_touch_decode(report, sizeof(report), &frame) == -1);

  tb371fc_touch_to_hid(0, 0, &x, &y);
  assert(x == 0 && y == TB371FC_HID_INPUT_Y_MAX - 1u);
  tb371fc_touch_to_hid(18400, 29440, &x, &y);
  assert(x == TB371FC_HID_INPUT_X_MAX - 1u && y == 0);

  frame.count = 1;
  frame.contacts[0].id = 3;
  frame.contacts[0].x = 9200;
  frame.contacts[0].y = 14720;
  native_touch_set_sink(notified, &notifications);
  tb371fc_touch_publish(&frame);
  assert(native_touch_seq == 2 && !(native_touch_seq & 1u));
  assert(native_touch_count == 1 && native_touch_ids[0] == 3);
  assert(native_touch_xs[0] == 15240);
  assert(native_touch_ys[0] == 10160);
  assert(notifications == 1);
  frame.count = 0;
  tb371fc_touch_publish(&frame);
  assert(native_touch_seq == 4 && native_touch_count == 0 && notifications == 2);
  native_touch_set_sink(0, 0);
  return 0;
}
