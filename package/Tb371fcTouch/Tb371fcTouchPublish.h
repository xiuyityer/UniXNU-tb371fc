/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef TB371FC_TOUCH_PUBLISH_H
#define TB371FC_TOUCH_PUBLISH_H

#include "Tb371fcTouchReport.h"

#ifdef __cplusplus
extern "C" {
#endif

void tb371fc_touch_publish(const Tb371fcTouchFrame *frame);
void native_touch_set_sink(void (*sink)(void *), void *context);

#ifdef __cplusplus
}
#endif

#endif
