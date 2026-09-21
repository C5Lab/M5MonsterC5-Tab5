#include "hs_crack_scheduler.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void test_loss_preserves_monotonic_safe_offset(void)
{
    hs_sched_shard_t shard;
    hs_sched_init(&shard, 100U, 1000U, HS_SCHED_OWNER_GROVE);
    CHECK(hs_sched_bind_active_job(&shard, "initial-job"));
    CHECK(strcmp(shard.active_job, "initial-job") == 0);
    CHECK(shard.state == HS_SCHED_LEASED);
    CHECK(shard.generation == 1U);

    CHECK(hs_sched_note_progress(&shard, 420U));
    CHECK(!hs_sched_note_progress(&shard, 400U));
    CHECK(shard.confirmed_safe_offset == 420U);

    CHECK(hs_sched_note_loss(&shard, "old-job"));
    CHECK(shard.state == HS_SCHED_RECOVERING);
    CHECK(shard.owner == HS_SCHED_OWNER_NONE);
    CHECK(shard.retired_owner == HS_SCHED_OWNER_GROVE);
    CHECK(strcmp(shard.retired_job, "old-job") == 0);
    CHECK(shard.confirmed_safe_offset == 420U);
    CHECK(hs_sched_pending_start(&shard) == 420U);
}

static void test_same_generation_can_reattach_before_migration(void)
{
    hs_sched_shard_t shard;
    hs_sched_init(&shard, 0U, 900U, HS_SCHED_OWNER_USB);
    CHECK(hs_sched_note_progress(&shard, 125U));
    CHECK(hs_sched_note_loss(&shard, "usb-old"));
    CHECK(hs_sched_can_reattach(&shard, HS_SCHED_OWNER_USB, "usb-old"));
    CHECK(!hs_sched_can_reattach(&shard, HS_SCHED_OWNER_GROVE, "usb-old"));
    CHECK(!hs_sched_can_reattach(&shard, HS_SCHED_OWNER_USB, "wrong"));

    CHECK(hs_sched_reattach(&shard, HS_SCHED_OWNER_USB, "usb-old"));
    CHECK(shard.state == HS_SCHED_LEASED);
    CHECK(shard.owner == HS_SCHED_OWNER_USB);
    CHECK(shard.generation == 1U);
    CHECK(strcmp(shard.active_job, "usb-old") == 0);
    CHECK(shard.confirmed_safe_offset == 125U);
}

static void test_migration_leases_only_the_unfinished_suffix(void)
{
    hs_sched_shard_t shard;
    hs_sched_assignment_t assignment;
    hs_sched_init(&shard, 100U, 1000U, HS_SCHED_OWNER_GROVE);
    CHECK(hs_sched_note_progress(&shard, 420U));
    CHECK(hs_sched_note_loss(&shard, "grove-old"));

    CHECK(hs_sched_lease_pending(&shard, HS_SCHED_OWNER_USB,
                                 "usb-new", &assignment));
    CHECK(assignment.start == 420U);
    CHECK(assignment.end == 1000U);
    CHECK(assignment.owner == HS_SCHED_OWNER_USB);
    CHECK(assignment.generation == 2U);
    CHECK(shard.state == HS_SCHED_LEASED);
    CHECK(shard.generation == 2U);
    CHECK(strcmp(shard.active_job, "usb-new") == 0);
    CHECK(!hs_sched_can_reattach(&shard, HS_SCHED_OWNER_GROVE,
                                 "grove-old"));
}

static void test_offsets_are_clamped_and_completion_requires_end(void)
{
    hs_sched_shard_t shard;
    hs_sched_init(&shard, 500U, 800U, HS_SCHED_OWNER_MBUS);
    CHECK(!hs_sched_note_progress(&shard, 100U));
    CHECK(shard.confirmed_safe_offset == 500U);
    CHECK(hs_sched_note_progress(&shard, 900U));
    CHECK(shard.confirmed_safe_offset == 800U);
    CHECK(hs_sched_complete(&shard, 799U));
    CHECK(shard.state == HS_SCHED_DONE);
    CHECK(shard.owner == HS_SCHED_OWNER_NONE);
    CHECK(hs_sched_pending_start(&shard) == 800U);
}

static void test_incomplete_completion_queues_suffix(void)
{
    hs_sched_shard_t shard;
    hs_sched_init(&shard, 0U, 100U, HS_SCHED_OWNER_GROVE);
    CHECK(hs_sched_note_progress(&shard, 60U));
    CHECK(!hs_sched_complete(&shard, 60U));
    CHECK(shard.state == HS_SCHED_PENDING);
    CHECK(shard.confirmed_safe_offset == 60U);
}

static void test_checked_accounting_is_per_generation(void)
{
    hs_sched_shard_t shard;
    hs_sched_assignment_t assignment;
    hs_sched_init(&shard, 0U, 1000U, HS_SCHED_OWNER_GROVE);

    CHECK(hs_sched_account_checked(&shard, 10U) == 10U);
    CHECK(hs_sched_account_checked(&shard, 15U) == 5U);
    CHECK(hs_sched_account_checked(&shard, 12U) == 0U);
    CHECK(hs_sched_note_loss(&shard, "first"));
    CHECK(hs_sched_lease_pending(&shard, HS_SCHED_OWNER_MBUS,
                                 "second", &assignment));
    CHECK(hs_sched_account_checked(&shard, 4U) == 4U);
    CHECK(hs_sched_account_checked(&shard, 7U) == 3U);
}

static void test_empty_suffix_cannot_be_leased(void)
{
    hs_sched_shard_t shard;
    hs_sched_assignment_t assignment;
    hs_sched_init(&shard, 10U, 10U, HS_SCHED_OWNER_GROVE);
    CHECK(shard.state == HS_SCHED_DONE);
    CHECK(!hs_sched_note_loss(&shard, "none"));
    CHECK(!hs_sched_lease_pending(&shard, HS_SCHED_OWNER_USB,
                                  "new", &assignment));
}

static void test_recovery_policy_is_status_authoritative(void)
{
    hs_sched_shard_t shard;
    hs_sched_init(&shard, 0U, 1000U, HS_SCHED_OWNER_GROVE);
    CHECK(hs_sched_note_progress(&shard, 300U));
    CHECK(hs_sched_note_loss(&shard, "old"));

    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_NO_PONG, 0U) ==
          HS_SCHED_WAIT);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_AMBIGUOUS, 0U) ==
          HS_SCHED_WAIT);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_OLD_JOB_RUNNING, 450U) ==
          HS_SCHED_REATTACH);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_UNKNOWN_JOB, 0U) ==
          HS_SCHED_PREPARE_RESTART);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_OLD_JOB_FOUND, 310U) ==
          HS_SCHED_VERIFY_FOUND);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_OLD_JOB_NOT_FOUND, 999U) ==
          HS_SCHED_QUEUE_SUFFIX);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_OLD_JOB_NOT_FOUND, 1000U) ==
          HS_SCHED_COMPLETE);
}

static void test_retired_job_cannot_reclaim_migrated_suffix(void)
{
    hs_sched_shard_t shard;
    hs_sched_assignment_t assignment;
    hs_sched_init(&shard, 0U, 1000U, HS_SCHED_OWNER_GROVE);
    CHECK(hs_sched_note_progress(&shard, 300U));
    CHECK(hs_sched_note_loss(&shard, "old"));
    CHECK(hs_sched_lease_pending(&shard, HS_SCHED_OWNER_USB,
                                 "replacement", &assignment));

    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_OLD_JOB_RUNNING, 500U) ==
          HS_SCHED_CANCEL_RETIRED);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_OLD_JOB_FOUND, 500U) ==
          HS_SCHED_VERIFY_FOUND);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_UNKNOWN_JOB, 0U) ==
          HS_SCHED_PREPARE_RESTART);
    CHECK(hs_sched_recovery_action(&shard, HS_SCHED_OLD_JOB_NOT_FOUND, 500U) ==
          HS_SCHED_CANCEL_RETIRED);
    CHECK(hs_sched_note_progress(&shard, 500U));
    CHECK(shard.confirmed_safe_offset == 500U);
    CHECK(shard.state == HS_SCHED_LEASED);
    CHECK(shard.owner == HS_SCHED_OWNER_USB);
    CHECK(strcmp(shard.active_job, "replacement") == 0);
}

static void test_local_drain_claims_only_the_pending_suffix(void)
{
    hs_sched_shard_t shard;
    hs_sched_assignment_t assignment;
    hs_sched_init(&shard, 100U, 1000U, HS_SCHED_OWNER_GROVE);
    CHECK(hs_sched_note_progress(&shard, 420U));
    CHECK(hs_sched_note_loss(&shard, "old"));

    CHECK(hs_sched_lease_local(&shard, &assignment));
    CHECK(assignment.start == 420U);
    CHECK(assignment.end == 1000U);
    CHECK(assignment.owner == HS_SCHED_OWNER_LOCAL);
    CHECK(shard.state == HS_SCHED_LOCAL);
    CHECK(shard.owner == HS_SCHED_OWNER_LOCAL);
    CHECK(!hs_sched_lease_local(&shard, &assignment));
}

int main(void)
{
    test_loss_preserves_monotonic_safe_offset();
    test_same_generation_can_reattach_before_migration();
    test_migration_leases_only_the_unfinished_suffix();
    test_offsets_are_clamped_and_completion_requires_end();
    test_incomplete_completion_queues_suffix();
    test_checked_accounting_is_per_generation();
    test_empty_suffix_cannot_be_leased();
    test_recovery_policy_is_status_authoritative();
    test_retired_job_cannot_reclaim_migrated_suffix();
    test_local_drain_claims_only_the_pending_suffix();

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("hs_crack_scheduler_test: PASS");
    return 0;
}
