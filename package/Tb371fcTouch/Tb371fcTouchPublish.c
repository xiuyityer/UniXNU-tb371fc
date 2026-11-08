/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Tb371fcTouchPublish.h"
#include <stdatomic.h>

#define NATIVE_TOUCH_SLOTS 12u

volatile uint32_t native_touch_seq;
volatile uint32_t native_touch_count;
volatile uint32_t native_touch_hid_reports;
volatile int native_touch_xs[NATIVE_TOUCH_SLOTS];
volatile int native_touch_ys[NATIVE_TOUCH_SLOTS];
volatile int native_touch_ids[NATIVE_TOUCH_SLOTS];

static _Atomic(void (*)(void *)) touch_sink;
static _Atomic(void *) touch_sink_context;

static void
touch_barrier(void)
{
  atomic_thread_fence(memory_order_seq_cst);
}

void
native_touch_set_sink(void (*sink)(void *), void *context)
{
  atomic_store_explicit(&touch_sink_context, context, memory_order_relaxed);
  atomic_store_explicit(&touch_sink, sink, memory_order_release);
}

void
tb371fc_touch_publish(const Tb371fcTouchFrame *frame)
{
  uint32_t seq = native_touch_seq;
  unsigned i, count;

  if (frame == 0)
    return;
  if (seq & 1u)
    ++seq;
  native_touch_seq = seq + 1u;
  touch_barrier();

  count = frame->count;
  if (count > TB371FC_TOUCH_CONTACTS)
    count = TB371FC_TOUCH_CONTACTS;
  for (i = 0; i < count; ++i) {
    uint16_t x, y;
    tb371fc_touch_to_hid(frame->contacts[i].x, frame->contacts[i].y, &x, &y);
    native_touch_ids[i] = frame->contacts[i].id;
    native_touch_xs[i] = x;
    native_touch_ys[i] = y;
  }
  native_touch_count = count;
  touch_barrier();
  native_touch_seq = seq + 2u;

  {
    void (*sink)(void *) = atomic_load_explicit(&touch_sink, memory_order_acquire);
    void *context = atomic_load_explicit(&touch_sink_context, memory_order_relaxed);
    if (sink != 0)
      sink(context);
  }
}
