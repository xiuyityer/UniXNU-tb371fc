/* SPDX-License-Identifier: BSD-2-Clause
 * Keep the existing 91-commit budget: initial snapshot, 30 early and 60 late.
 * Sleep budget is 600 seconds; snapshot/transport work adds elapsed time.
 */
#ifndef TB_ROOT_RECORDER_POLICY_H
#define TB_ROOT_RECORDER_POLICY_H
#define TB_ROOT_LOG_ROUNDS 90U
#define TB_ROOT_LOG_EARLY_ROUNDS 30U
#define TB_ROOT_LOG_EARLY_MS 2000U
#define TB_ROOT_LOG_LATE_MS 9000U
#define TB_ROOT_LOG_WAIT_SECONDS 600U
static inline unsigned tbRootLogInterval(unsigned checkpoint)
{
  return checkpoint <= TB_ROOT_LOG_EARLY_ROUNDS ? TB_ROOT_LOG_EARLY_MS : TB_ROOT_LOG_LATE_MS;
}
#endif
