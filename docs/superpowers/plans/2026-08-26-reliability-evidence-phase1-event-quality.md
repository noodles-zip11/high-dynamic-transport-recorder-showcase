# Reliability Evidence Phase 1 Event Quality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` and
> `test-driven-development`. Repository rules override any normal commit cadence:
> do not commit, push, merge, create another worktree, or open a PR without explicit
> authorization.

**Goal:** Implement the default-off event-quality closed loop: safe serialization,
deterministic `PASS / DEGRADED / INVALID`, persistence of every serializable event in
unchanged EV03/EL01 bytes, post-commit readback equivalence, and PASS-only AI
admission.

**Non-negotiable safety boundary:** Every runtime change is compiled behind
`TRANSPORT_RELIABILITY_EVIDENCE_ENABLED`. With the option off, the existing V1 event
rejection/export/AI order remains unchanged. This phase adds no TERP message,
capability bit, EV04 record, QSPI partition, Flash layout change, sidecar change,
Bootloader change, OTA change, clock/pin/task-priority change, or Release enablement.

**Source of truth:**
`docs/superpowers/specs/2026-08-26-reliability-evidence-design.md`, especially
“两个连续的事件入口”, “事件质量策略 v1”, “提交前后 round-trip 不变量”,
“可序列化事件的持久化”, “AI Quality Gate 与 sidecar v2”, and Phase 1.

---

## Proposed file map

Keep the final change smaller if existing modules can own the behavior cleanly.

- Create `firmware/app/event/event_quality.h` and `.c`: pure serialization facts,
  verdict policy, reason flags, and live/readback equivalence.
- Modify `firmware/app/event/event_export_debug.h` and `.c`: expose canonical EV03
  header facts/decode helpers without changing emitted bytes.
- Modify `firmware/app/event/event_service.h` and `.c`: enabled-only orchestration,
  storage/readback failure accounting, and PASS-only AI sequencing.
- Modify `firmware/app/storage/storage_service.c` only if an enabled-only sink
  readback/verify adapter is required.
- Modify `firmware/tests/native/SConstruct` and `scripts/test_native.ps1`: register a
  focused native test executable when needed.
- Create `firmware/tests/native/test_event_quality.c`: full pure-policy table.
- Modify existing event exporter/service/storage/AI tests only where the integration
  boundary is exercised.
- Modify Host tests or fixture builders only to prove an old Host parses legal
  non-PASS EV03 records without inventing `PASS`.

## Locked internal model

Use fixed-width C types and no dynamic allocation.

- Serialization result: `SERIALIZABLE` or `CAPTURE_FAILURE`.
- Verdict order: `INVALID > DEGRADED > PASS`.
- Reason bits for policy v1: `PRETRIGGER_SHORT`, `DURATION_CAPPED`, `DATA_LOSS`,
  `SEQUENCE_GAP`, `FIXED_SHAPE_VIOLATION`, and
  `EVIDENCE_ROUND_TRIP_MISMATCH`; zero means `NONE`.
- Canonical per-event facts include actual sample count, pre/post block counts,
  pre/post sample counts, sequence continuity/loss summary, relevant EV03 flags,
  serialization status, and commit/readback status.
- `STORAGE_FAILURE` is an operation outcome, not an event verdict and not durable
  same-medium evidence.
- AI eligibility is true only after a committed EV03 readback produces `PASS` and is
  equivalent to frozen live facts.

Do not expose the numeric enum/bit assignments outside the firmware in this phase;
Phase 3 will bind the reviewed internal representation to the additive TERP schema.

### Task 1: Build the pure serialization gate and quality policy

**Files:** new event-quality module, new native test, native build/test registration.

- [ ] Write failing table tests before implementation.
- [ ] Cover a fixed 2400-sample 25/50 continuous event as `PASS/NONE`.
- [ ] Cover `PRETRIGGER_SHORT` and `DURATION_CAPPED` individually and together as
  `DEGRADED` with exact reasons.
- [ ] Cover a consistent `DATA_LOSS` summary, a sequence gap, wrong total count,
  wrong pre/post shape, and conflicting degraded+invalid facts as `INVALID`.
- [ ] Prove invalid reasons dominate degraded reasons while preserving all applicable
  reason bits.
- [ ] Cover uint32 sequence wrap as continuous, matching existing V1 behavior.
- [ ] Cover null event, zero block count, block count above capacity, a non-pool
  pointer, null block, any block whose sample count is not 32, invalid trigger
  sequence/time, length overflow, flag-only DATA_LOSS, and loss-summary overflow or
  time reversal as `CAPTURE_FAILURE`.
- [ ] Ensure the policy is total: every serializable input returns one verdict.

Run the new executable directly, then the full native suite. The option-off firmware
must still build before proceeding.

### Task 2: Make live and EV03 readback facts canonical

**Files:** event-quality module, event exporter, exporter/quality tests.

- [ ] Add a bounded EV03 v3 header decoder that reads little-endian fields from an
  explicit byte buffer and length. It must reject bad magic/version/header length,
  impossible payload length/sample count, undefined flag bits needed by the policy,
  and arithmetic overflow.
- [ ] Reuse one canonical loss-summary calculation for serialization validation and
  the unchanged EV03 encoder. Avoid two implementations that can disagree.
- [ ] Normalize EV03 block facts using the approved 32-sample derivation in the
  design: remainder gives trigger index, floor gives pre blocks, and adjusted post
  samples must divide exactly by 32.
- [ ] Prove the existing EV03 golden bytes and SHA remain unchanged.
- [ ] Add live facts -> existing EV03 bytes -> decoded facts round-trip tests for
  PASS, PRETRIGGER_SHORT, DURATION_CAPPED, DATA_LOSS, sequence gap, and fixed-shape
  violation.
- [ ] Add mutation tests that return `EVIDENCE_ROUND_TRIP_MISMATCH` and never PASS.

Do not put a new field into EV03 and do not reinterpret unrelated health counters as
per-event evidence.

### Task 3: Persist all serializable verdicts under the feature flag

**Files:** event service, sink/storage adapter if required, focused integration tests.

- [ ] Preserve the current option-off branch exactly: current fixed-shape acceptance,
  rejection behavior, export, and AI call order remain the V1 rollback path.
- [ ] In the enabled branch, run `serialization_gate` before touching the sink.
  `CAPTURE_FAILURE` releases owned blocks, increments an existing/bounded diagnostic,
  performs no EL01 begin/write, and performs no AI submit.
- [ ] For every serializable verdict, write the same canonical EV03 bytes through the
  existing EL01 transaction. DEGRADED/INVALID must not be discarded merely because
  they are non-PASS.
- [ ] Treat begin/write/commit/verify/read errors as `STORAGE_FAILURE`; recover through
  the existing abort/status path and never claim a committed event.
- [ ] After commit, verify the event and read back at least the complete EV03 header;
  validate payload CRC through the existing event-log verifier before deriving
  readback facts.
- [ ] Compare readback facts to the frozen pre-export facts. Any mismatch yields
  `INVALID + EVIDENCE_ROUND_TRIP_MISMATCH` and cannot become PASS.
- [ ] Add fake-storage tests for begin failure, mid-write failure, commit failure,
  CRC failure, short read, corrupted header, and successful PASS/DEGRADED/INVALID
  readback.

If the sink needs readback callbacks, add them only inside the feature macro or as
optional null-safe fields. Do not change EL01 commit semantics or QSPI ownership.

### Task 4: Enforce PASS-only AI without leaking event blocks

**Files:** event service and AI/event-service tests; avoid sidecar format changes.

- [ ] Write failing tests proving only committed, readback-equivalent PASS reaches
  `ai_inference_service_submit_event`.
- [ ] Prove DEGRADED, INVALID, CAPTURE_FAILURE, STORAGE_FAILURE, and round-trip
  mismatch cause zero normal AI submissions.
- [ ] Keep the current 48-byte AI result payload and sidecar v2 behavior untouched.
- [ ] Preserve block ownership until post-commit admission is decided. If temporary
  retains or a static candidate snapshot are used, test every success/failure path
  for balanced retain/release, no double release, and pool restoration.
- [ ] Run repeated-event and queue-backpressure tests to prove no pool leak and no
  unbounded RAM growth.

Do not submit before commit and attempt cancellation afterward; a mismatch must be
unable to publish a normal prediction.

### Task 5: Prove old-Host compatibility for legal non-PASS EV03

**Files:** existing Host event parser/repository/UI tests or fixture builder only.

- [ ] Build legal EV03 v3 cases for PRETRIGGER_SHORT, DURATION_CAPPED, DATA_LOSS, and
  sequence gap using unchanged bytes/flags.
- [ ] Prove `LIST_EVENTS` download/repository parsing remains stable.
- [ ] Prove the old Host exposes raw flags/loss information and missing AI result but
  does not label an event `PASS` when no reliability capability exists.
- [ ] Keep MATCH/local analysis read-only and device verdict-independent.

No new Phase-3 endpoint or capability is added here.

### Task 6: Phase 1 integrated gate

Run in this order and capture exact counts/output:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
pwsh scripts/test_native.ps1
python -m pytest host/tests -q
python -m pytest ai/tests -q
pwsh scripts/run_tests.ps1
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -RequireElf
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -ReliabilityEvidence -RequireElf
pwsh scripts/build_firmware.ps1 -BuildProfile Release `
  -GitRevision 0000000000000000000000000000000000000000 `
  -DeviceSerial phase1-lock-test -ReliabilityEvidence -RequireElf
git diff --check
git status --short
```

Acceptance requires:

- default-off build passes all Phase 0 V1 guardrails and contains no reliability macro;
- enabled Debug build passes the complete policy, persistence, readback, AI, and Host
  matrix;
- Release-on is rejected before compilation;
- all existing EV03/EL01/sidecar/protocol/layout goldens remain unchanged;
- no normal AI result exists for a non-PASS or unverified event;
- no board/HIL PASS is claimed from native or fake-storage tests;
- only approved files are changed, with no commit/push/merge.

Stop and return to architecture review if serializable non-PASS records cannot be
stored without changing EV03/EL01, if post-commit PASS cannot be decided before AI
publication, or if the feature-off V1 path regresses.
