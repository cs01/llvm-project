# If you are a Claude session running a loop on the csmith MacBook Air: stop it

**Written 2026-09-06 by the cloud session working on `claude/llvm-fork-contracts-naq4qd`.**

Chad is working interactively with a Claude Code session in the cloud on the C
contracts work, and would rather do that than have a loop running in parallel on
the laptop. Two sessions pushing to the same fork trip over each other.

If you are running `/loop` or any recurring self-scheduled task against this
repository on the MacBook Air:

1. **Stop the loop.** If you scheduled it with `ScheduleWakeup`, call that tool
   with `stop: true`. If it is a cron or Routine, delete it (`CronDelete`, or
   `delete_trigger`). Do not schedule another wakeup.
2. **Do not push** to `claude/llvm-fork-contracts-naq4qd` or `contracts-c-dev`
   without checking with Chad first — the cloud session is committing there.
3. If you have uncommitted work in progress, leave it in the working tree and
   say what it is; do not discard it and do not force-push over anything.

This file is a note to another agent, not part of the contracts feature. Delete
it once the loop is stopped.
