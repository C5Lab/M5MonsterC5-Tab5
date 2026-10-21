#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HS_SCHED_JOB_BYTES 32U

typedef enum {
    HS_SCHED_PENDING = 0,
    HS_SCHED_LEASED,
    HS_SCHED_RECOVERING,
    HS_SCHED_DONE,
    HS_SCHED_LOCAL,
} hs_sched_state_t;

typedef enum {
    HS_SCHED_OWNER_NONE = -1,
    HS_SCHED_OWNER_GROVE = 0,
    HS_SCHED_OWNER_USB,
    HS_SCHED_OWNER_MBUS,
    HS_SCHED_OWNER_LOCAL,
} hs_sched_owner_t;

typedef enum {
    HS_SCHED_NO_PONG = 0,
    HS_SCHED_OLD_JOB_RUNNING,
    HS_SCHED_OLD_JOB_FOUND,
    HS_SCHED_OLD_JOB_NOT_FOUND,
    HS_SCHED_UNKNOWN_JOB,
    HS_SCHED_AMBIGUOUS,
} hs_sched_recovery_event_t;

typedef enum {
    HS_SCHED_WAIT = 0,
    HS_SCHED_REATTACH,
    HS_SCHED_PREPARE_RESTART,
    HS_SCHED_CANCEL_RETIRED,
    HS_SCHED_QUEUE_SUFFIX,
    HS_SCHED_COMPLETE,
    HS_SCHED_VERIFY_FOUND,
} hs_sched_action_t;

typedef struct {
    uint64_t start;
    uint64_t end;
    uint32_t generation;
    hs_sched_owner_t owner;
    char job[HS_SCHED_JOB_BYTES];
} hs_sched_assignment_t;

typedef struct {
    uint64_t range_start;
    uint64_t range_end;
    uint64_t confirmed_safe_offset;
    hs_sched_state_t state;
    hs_sched_owner_t owner;
    hs_sched_owner_t retired_owner;
    uint32_t generation;
    uint64_t generation_checked;
    uint64_t generation_accounted;
    char active_job[HS_SCHED_JOB_BYTES];
    char retired_job[HS_SCHED_JOB_BYTES];
} hs_sched_shard_t;

void hs_sched_init(hs_sched_shard_t *shard, uint64_t range_start,
                   uint64_t range_end, hs_sched_owner_t owner);
bool hs_sched_bind_active_job(hs_sched_shard_t *shard, const char *job);
bool hs_sched_note_progress(hs_sched_shard_t *shard, uint64_t safe_offset);
bool hs_sched_note_loss(hs_sched_shard_t *shard, const char *job);
bool hs_sched_can_reattach(const hs_sched_shard_t *shard,
                           hs_sched_owner_t owner, const char *job);
bool hs_sched_reattach(hs_sched_shard_t *shard, hs_sched_owner_t owner,
                       const char *job);
bool hs_sched_queue_suffix(hs_sched_shard_t *shard);
bool hs_sched_lease_pending(hs_sched_shard_t *shard,
                            hs_sched_owner_t owner, const char *job,
                            hs_sched_assignment_t *assignment);
bool hs_sched_lease_local(hs_sched_shard_t *shard,
                          hs_sched_assignment_t *assignment);
bool hs_sched_complete(hs_sched_shard_t *shard,
                       uint64_t reported_safe_offset);
uint64_t hs_sched_pending_start(const hs_sched_shard_t *shard);
uint64_t hs_sched_account_checked(hs_sched_shard_t *shard,
                                  uint64_t checked);
hs_sched_action_t hs_sched_recovery_action(
    const hs_sched_shard_t *shard, hs_sched_recovery_event_t event,
    uint64_t reported_safe_offset);
