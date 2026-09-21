#include "hs_crack_scheduler.h"

#include <stddef.h>
#include <string.h>

static void hs_sched_copy_job(char output[HS_SCHED_JOB_BYTES],
                              const char *job)
{
    if (!output) return;
    output[0] = '\0';
    if (!job) return;
    size_t length = strlen(job);
    if (length >= HS_SCHED_JOB_BYTES) length = HS_SCHED_JOB_BYTES - 1U;
    memcpy(output, job, length);
    output[length] = '\0';
}

static uint64_t hs_sched_clamp_offset(const hs_sched_shard_t *shard,
                                      uint64_t offset)
{
    if (offset < shard->range_start) return shard->range_start;
    if (offset > shard->range_end) return shard->range_end;
    return offset;
}

void hs_sched_init(hs_sched_shard_t *shard, uint64_t range_start,
                   uint64_t range_end, hs_sched_owner_t owner)
{
    if (!shard) return;
    memset(shard, 0, sizeof(*shard));
    if (range_end < range_start) range_end = range_start;
    shard->range_start = range_start;
    shard->range_end = range_end;
    shard->confirmed_safe_offset = range_start;
    shard->retired_owner = HS_SCHED_OWNER_NONE;
    shard->generation = 1U;
    if (range_start == range_end) {
        shard->state = HS_SCHED_DONE;
        shard->owner = HS_SCHED_OWNER_NONE;
    } else {
        shard->state = HS_SCHED_LEASED;
        shard->owner = owner;
    }
}

bool hs_sched_bind_active_job(hs_sched_shard_t *shard, const char *job)
{
    if (!shard || shard->state != HS_SCHED_LEASED || !job || !job[0])
        return false;
    hs_sched_copy_job(shard->active_job, job);
    return true;
}

bool hs_sched_note_progress(hs_sched_shard_t *shard, uint64_t safe_offset)
{
    if (!shard) return false;
    uint64_t clamped = hs_sched_clamp_offset(shard, safe_offset);
    if (clamped <= shard->confirmed_safe_offset) return false;
    shard->confirmed_safe_offset = clamped;
    return true;
}

bool hs_sched_note_loss(hs_sched_shard_t *shard, const char *job)
{
    if (!shard || shard->state != HS_SCHED_LEASED ||
        shard->confirmed_safe_offset >= shard->range_end || !job || !job[0]) {
        return false;
    }
    shard->retired_owner = shard->owner;
    hs_sched_copy_job(shard->retired_job, job);
    shard->active_job[0] = '\0';
    shard->owner = HS_SCHED_OWNER_NONE;
    shard->state = HS_SCHED_RECOVERING;
    return true;
}

bool hs_sched_can_reattach(const hs_sched_shard_t *shard,
                           hs_sched_owner_t owner, const char *job)
{
    return shard && job && shard->state == HS_SCHED_RECOVERING &&
           shard->retired_owner == owner && shard->retired_job[0] &&
           strcmp(shard->retired_job, job) == 0 &&
           shard->confirmed_safe_offset < shard->range_end;
}

bool hs_sched_reattach(hs_sched_shard_t *shard, hs_sched_owner_t owner,
                       const char *job)
{
    if (!hs_sched_can_reattach(shard, owner, job)) return false;
    shard->state = HS_SCHED_LEASED;
    shard->owner = owner;
    hs_sched_copy_job(shard->active_job, job);
    return true;
}

bool hs_sched_queue_suffix(hs_sched_shard_t *shard)
{
    if (!shard || shard->state == HS_SCHED_DONE ||
        shard->confirmed_safe_offset >= shard->range_end) return false;
    shard->state = HS_SCHED_PENDING;
    shard->owner = HS_SCHED_OWNER_NONE;
    shard->active_job[0] = '\0';
    return true;
}

bool hs_sched_lease_pending(hs_sched_shard_t *shard,
                            hs_sched_owner_t owner, const char *job,
                            hs_sched_assignment_t *assignment)
{
    if (!shard || !assignment || !job || !job[0] ||
        owner == HS_SCHED_OWNER_NONE || owner == HS_SCHED_OWNER_LOCAL ||
        (shard->state != HS_SCHED_PENDING &&
         shard->state != HS_SCHED_RECOVERING) ||
        shard->confirmed_safe_offset >= shard->range_end) {
        return false;
    }

    shard->generation++;
    shard->state = HS_SCHED_LEASED;
    shard->owner = owner;
    shard->generation_checked = 0;
    shard->generation_accounted = 0;
    hs_sched_copy_job(shard->active_job, job);

    memset(assignment, 0, sizeof(*assignment));
    assignment->start = shard->confirmed_safe_offset;
    assignment->end = shard->range_end;
    assignment->generation = shard->generation;
    assignment->owner = owner;
    hs_sched_copy_job(assignment->job, job);
    return true;
}

bool hs_sched_lease_local(hs_sched_shard_t *shard,
                          hs_sched_assignment_t *assignment)
{
    if (!shard || !assignment ||
        (shard->state != HS_SCHED_PENDING &&
         shard->state != HS_SCHED_RECOVERING) ||
        shard->confirmed_safe_offset >= shard->range_end) {
        return false;
    }

    shard->generation++;
    shard->state = HS_SCHED_LOCAL;
    shard->owner = HS_SCHED_OWNER_LOCAL;
    shard->generation_checked = 0;
    shard->generation_accounted = 0;
    shard->active_job[0] = '\0';

    memset(assignment, 0, sizeof(*assignment));
    assignment->start = shard->confirmed_safe_offset;
    assignment->end = shard->range_end;
    assignment->generation = shard->generation;
    assignment->owner = HS_SCHED_OWNER_LOCAL;
    return true;
}

bool hs_sched_complete(hs_sched_shard_t *shard,
                       uint64_t reported_safe_offset)
{
    if (!shard) return false;
    (void)hs_sched_note_progress(shard, reported_safe_offset);
    if (shard->confirmed_safe_offset < shard->range_end) {
        (void)hs_sched_queue_suffix(shard);
        return false;
    }
    shard->state = HS_SCHED_DONE;
    shard->owner = HS_SCHED_OWNER_NONE;
    shard->active_job[0] = '\0';
    return true;
}

uint64_t hs_sched_pending_start(const hs_sched_shard_t *shard)
{
    if (!shard) return 0;
    return hs_sched_clamp_offset(shard, shard->confirmed_safe_offset);
}

uint64_t hs_sched_account_checked(hs_sched_shard_t *shard,
                                  uint64_t checked)
{
    if (!shard || checked <= shard->generation_accounted) return 0;
    uint64_t delta = checked - shard->generation_accounted;
    shard->generation_checked = checked;
    shard->generation_accounted = checked;
    return delta;
}

hs_sched_action_t hs_sched_recovery_action(
    const hs_sched_shard_t *shard, hs_sched_recovery_event_t event,
    uint64_t reported_safe_offset)
{
    if (!shard) return HS_SCHED_WAIT;

    switch (event) {
    case HS_SCHED_OLD_JOB_FOUND:
        return HS_SCHED_VERIFY_FOUND;
    case HS_SCHED_OLD_JOB_NOT_FOUND:
        if (hs_sched_clamp_offset(shard, reported_safe_offset) >=
            shard->range_end) {
            return HS_SCHED_COMPLETE;
        }
        return shard->state == HS_SCHED_RECOVERING
                   ? HS_SCHED_QUEUE_SUFFIX
                   : HS_SCHED_CANCEL_RETIRED;
    case HS_SCHED_OLD_JOB_RUNNING:
        return shard->state == HS_SCHED_RECOVERING
                   ? HS_SCHED_REATTACH
                   : HS_SCHED_CANCEL_RETIRED;
    case HS_SCHED_UNKNOWN_JOB:
        return HS_SCHED_PREPARE_RESTART;
    case HS_SCHED_NO_PONG:
    case HS_SCHED_AMBIGUOUS:
    default:
        return HS_SCHED_WAIT;
    }
}
