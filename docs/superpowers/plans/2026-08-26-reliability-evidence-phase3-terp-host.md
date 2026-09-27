# Reliability Evidence Phase 3 TERP/Host Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` and
> `test-driven-development`. Do not commit, push, merge, open a PR, create another
> worktree, or clean the current worktree without explicit authorization.

**Goal:** Expose committed event-quality evidence and the Phase 2 CrashRecord
through additive TERP v1 operations, one bounded MSH facade, and a compatible Host
client without changing any existing TERP payload, message ID, event download, AI
result, OTA/model, or default-off behavior.

**Architecture:** Add a small feature-only `reliability_evidence` facade that
combines existing EL01 verification/read, the EV03 header decoder and quality
policy, the existing AI sidecar lookup, and the Phase 2 validated CrashRecord cache.
The generic TERP device layer only owns fixed wire encoding and delegates through
bounded callbacks. The runtime binds those callbacks and advertises one additive
capability only when the reliability feature is enabled and the Crash backend is
ready. The Host gates all three new calls on that capability. Existing clients and
devices continue to use their unchanged TERP v1 paths.

**Source of truth:**
`docs/superpowers/specs/2026-08-26-reliability-evidence-design.md`, especially
“Reliability Evidence Service”, “TERP 与 MSH 兼容旁路”, and “Phase 3：TERP / Host”.

**Non-negotiable boundary:** Do not change existing TERP IDs or payload bytes,
HELLO 13B, HEALTH 14B, GET_EVENT_INFO 12B, GET_AI_RESULT 48B,
READ_EVENT_CHUNK `20+N`, the 4000-byte chunk ceiling, EV03/EL01, AI sidecar v2,
storage ownership, Bootloader/OTA/model paths, pins, clocks, task priorities, or
the Release lock. No clear/delete/erase CrashRecord operation is allowed.

---

## Frozen Phase 3 wire contract

- Capability: `TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1 = 1 << 8` (`0x00000100`).
  This is a single discovery bit for all three operations. It is advertised only
  when `TRANSPORT_RELIABILITY_EVIDENCE_ENABLED=1` and the Phase 2 Crash backend is
  ready. If the backend is unavailable, the bit is clear and all three callbacks
  remain unbound; event evidence is intentionally hidden with the same capability.
- Message IDs:
  - `GET_EVENT_EVIDENCE = 0x0300`
  - `GET_CRASH_RECORD = 0x0301`
  - `ACK_CRASH_RECORD = 0x0302`
- All fields are little-endian.
- `GET_EVENT_EVIDENCE` request is 4B: `event_id_u32`.
- `GET_EVENT_EVIDENCE` response is exactly 20B:
  `event_id_u32`, `evidence_version_u16`, `verdict_u8`, `ai_decision_u8`,
  `reason_flags_u32`, `storage_state_u8`, `ai_result_status_u8`,
  `ai_failure_reason_u16`, `ai_result_sequence_u32`.
- `evidence_version=1`; verdict numeric values remain
  `PASS=1`, `DEGRADED=2`, `INVALID=3`.
- AI decision values are `NOT_RUN_QUALITY=0`, `ELIGIBLE_NO_RESULT=1`, and
  `RESULT_PRESENT=2`. `storage_state=EL01_VERIFIED=1` is the only successful v1
  response state. With no sidecar record, AI status/failure/sequence are all zero.
- `GET_CRASH_RECORD` request is 12B:
  `sequence_u32`, `offset_u32`, `requested_length_u32`. `sequence=0` selects the
  current visible latest record; nonzero sequence must match it exactly.
- `GET_CRASH_RECORD` response is `20+N` bytes:
  `sequence_u32`, `actual_offset_u32`, `total_length_u32`,
  `actual_length_u32`, `chunk_crc32_u32`, `data_bytes`. `N` is nonzero and at most
  4000. V1 exposes exactly the validated 128 record bytes; the independent ACK word
  and reserved slot tail are never returned as record bytes.
- `ACK_CRASH_RECORD` request is 4B `sequence_u32` and rejects zero. Its successful
  response is the same 4B sequence. Duplicate ACK is the same success response;
  stale or absent sequence is `NOT_FOUND` and changes no bytes.
- Error mapping:
  - malformed length, zero requested length, range overflow, or request above the
    negotiated/4000-byte bound -> `MALFORMED`;
  - feature/callback unavailable or committed EV02/unknown EV header ->
    `UNSUPPORTED`;
  - absent event, absent CrashRecord, or nonzero Crash sequence mismatch ->
    `NOT_FOUND`;
  - EL01 verify/read/decode failure, AI sidecar storage error, or ambiguous Crash
    A/B state -> `INTERNAL`;
  - pre-HELLO requests continue to return `HANDSHAKE_REQUIRED`.

## Proposed file map

- Modify `protocol/terp_messages.yaml` and generated firmware/Host registries.
- Add only new reliability golden vectors; existing golden files remain byte-for-byte
  unchanged.
- Modify `firmware/components/protocol/terp_device.{h,c}` for bounded structs,
  callbacks, encoders, and dispatch cases.
- Create `firmware/app/reliability/reliability_evidence.{h,c}` for the facade and
  one feature-only `evidence` MSH command.
- Modify `firmware/app/transport/terp_service.{h,c}` to bind facade callbacks and
  conditionally advertise capability bit 8.
- Modify `firmware/app/SConscript` only for feature-on facade compilation. Phase 2
  already owns early Crash initialization in `app_runtime.c`; do not reorder later
  V1 services.
- Modify Host protocol client/messages/simulator modules and add focused tests.

### Task 1: Freeze additive registry and golden bytes

- [ ] Add failing registry tests for capability bit 8 and message IDs
  `0x0300..0x0302`, including uniqueness and exact request/response field lists.
- [ ] Add the YAML entries and regenerate both registries with the existing tool.
- [ ] Freeze one request and one success response golden for each operation plus
  representative `UNSUPPORTED`, `NOT_FOUND`, `MALFORMED`, and handshake errors.
- [ ] Prove all pre-existing message definitions, generated numeric IDs, and golden
  files are unchanged; only additive entries/files are allowed.
- [ ] Run only registry/golden/V1-contract tests for this task.

### Task 2: Add bounded TERP device operations

- [ ] Add fixed C DTOs and callbacks for event evidence, Crash chunk read, and
  sequence-exact ACK. Do not expose storage, AI, or Backup SRAM types in the generic
  protocol layer.
- [ ] Implement exact length validation and the frozen response encodings. Reuse the
  existing CRC32 and 4000-byte chunk bound; no heap allocation or second chunk size.
- [ ] Callback errors must be mapped only through the frozen whitelist above. An
  unbound callback returns `UNSUPPORTED`.
- [ ] Add focused native tables for valid, malformed, unsupported, not-found,
  internal, sequence=0/latest, exact sequence, range overflow, duplicate ACK, and
  pre-HELLO behavior.
- [ ] Re-run the locked existing HELLO/HEALTH/event/AI/OTA device vectors once, not
  the whole repository suite.

### Task 3: Implement the reliability facade and MSH boundary

- [ ] Event query requires a committed `event_log_event_info_t`, then calls existing
  EL01 verification, reads exactly the 160-byte EV03 header, requires an exact read,
  decodes it with `event_export_debug_decode_header`, and evaluates it with the same
  `event_quality_policy_v1`. It must not allocate or read the full event payload.
- [ ] An existing EV02 or unknown EV header returns `UNSUPPORTED`; missing event is
  `NOT_FOUND`; verify/read/decode failure is `INTERNAL`. Never manufacture PASS.
- [ ] Derive `ai_decision` only from the computed verdict and an exact same-event AI
  lookup. Non-PASS never queries/publishes an AI record. PASS plus no result is
  `ELIGIBLE_NO_RESULT`; PASS plus an existing sidecar entry is `RESULT_PRESENT`.
  AI-disabled builds behave as no result; sidecar storage failure is `INTERNAL`.
- [ ] Crash reads use the Phase 2 validated RAM cache, never re-parse volatile Backup
  SRAM in the TERP thread. Return only the 128 record bytes. ACK delegates to the
  Phase 2 sequence-exact API and updates only the independent ACK state.
- [ ] Add one feature-only MSH command:
  `evidence event <event_id>`, `evidence crash [sequence] [offset] [length]`, and
  `evidence crash_ack <sequence>`. No clear/delete/erase and no implicit ACK.
- [ ] Test the facade with fake storage/AI/Crash seams: PASS/DEGRADED/INVALID,
  readback corruption, EV02, missing result, matching result, sidecar error, no
  Crash, chunk bounds, stale ACK, and duplicate ACK.

### Task 4: Bind readiness without disturbing V1 startup

- [ ] Bind all three callbacks only in feature-on builds and only when the Phase 2
  Crash backend is ready. Set capability bit 8 at the same decision point before
  TERP device info is exposed.
- [ ] Crash init/recovery remains before `power_runtime_start()` as established by
  Phase 2. Do not move storage, TERP, AI, acquisition, event, reporter, or power
  startup relative to the default-off path.
- [ ] Prove feature-off has no facade/MSH source or capability bit, and new requests
  receive structured unsupported without changing any old response.
- [ ] Prove feature-on with backend unavailable clears the bit and leaves all later
  V1 startup paths running.

### Task 5: Add the Host compatibility layer

- [ ] Add immutable Host DTOs/enums for the frozen 20B event evidence and 20+N Crash
  chunk. Preserve raw reason flags and raw Crash bytes even if symbolization fails.
- [ ] Add capability-gated client methods for event evidence, complete 128B Crash
  download with identity/offset/length/chunk-CRC checks, and exact ACK.
- [ ] `sequence=0` may resolve latest only on the first Crash response; subsequent
  chunks must request and verify the returned nonzero sequence.
- [ ] Extend the simulator only with additive feature-on behavior. Old-device mode
  must keep bit 8 clear and return unsupported.
- [ ] Add focused Host tests for old device, enabled device, raw download, CRC and
  identity mismatch, stale ACK, duplicate ACK, disconnect/resume, and legal
  DEGRADED/INVALID EV03 display without AI prediction.

### Task 6: Phase 3 gate

Run narrow checks during development. At completion run one integrated gate only:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
pwsh scripts/test_native.ps1
python -m pytest host/tests scripts/tests -q
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -RequireElf `
  -SaveReliabilityOffBaseline
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -ReliabilityEvidence -RequireElf
pwsh scripts/check_crash_fault_context.ps1
pwsh scripts/build_firmware.ps1 -BuildProfile Release `
  -GitRevision 0000000000000000000000000000000000000000 `
  -DeviceSerial phase3-lock-test -ReliabilityEvidence -RequireElf
git diff --check
git status --short
```

Acceptance requires:

- all three new wire formats and IDs match the frozen contract;
- all original V1 messages, golden bytes, event download/resume, 48B AI response,
  OTA/model operations, and default-off capability flags remain unchanged;
- event evidence comes only from verified committed EV03 plus the shared quality
  policy and exact-event AI lookup;
- Crash read/ACK is bounded, raw, sequence-exact, and never clears a record;
- backend failure hides bit 8 and does not block startup;
- Release-on remains rejected before compilation;
- no hardware/UART/reset-retention claim is made from native or loopback evidence;
- no commit, push, merge, PR, or worktree cleanup occurs.

Stop and return to architecture review if event evidence requires loading an entire
event, if an old wire payload/golden changes, if capability readiness cannot be fixed
before HELLO, if Crash raw bytes cannot remain 128-byte bounded, or if default-off
links any feature-only facade/MSH implementation.
