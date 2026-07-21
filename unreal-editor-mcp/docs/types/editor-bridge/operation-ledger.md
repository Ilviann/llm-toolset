# Mutation operation ledger

Every mutating command requires a caller-generated 32-character lowercase hexadecimal `operation_id`. At authenticated HTTP admission the bridge canonicalizes the command and complete argument object, binds it to the project, bridge instance, and authenticated context, and retains the resulting 40-character request digest. Reusing an ID with different normalized arguments returns `operation_conflict` without execution. Reusing it with the same terminal request replays the retained success or error.

Entries move through `queued`, `executing`, and one terminal state: `committed`, `rejected`, or `cancelled`. `operation_status` accepts the operation ID and the 32-character `bridge_instance_id` published by `capabilities` and mutation results. It can cancel queued work; executing Unreal mutations are never interrupted. A missing/expired entry or another bridge instance returns `outcome_unknown`, `retained: false`, and `retry_safe: false`, requiring inspection before any new mutation.

The ledger retains at most 128 operations for 15 minutes after their latest terminal outcome. It evicts the oldest terminal record when full and never evicts queued/executing work to admit another operation. Shutdown cancels queued entries. Committed outcomes are stored before HTTP response completion, so a disconnected caller can reconcile rather than duplicate the edit.
