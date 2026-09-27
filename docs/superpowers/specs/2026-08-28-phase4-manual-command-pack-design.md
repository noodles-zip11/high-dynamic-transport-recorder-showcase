# Phase 4 Manual Command Pack Design

## Goal

Provide one independent, copy-paste-safe command for each remaining operator
activity that is useful without a logic analyzer or oscilloscope. Commands must
run from any working directory, preserve first-failure evidence, and never make
the operator repeat a completed gate because of an avoidable harness error.

This command pack does not weaken the Phase 4 hardware gates and does not turn
partial or diagnostic evidence into a release PASS.

## Frozen scope

The command pack contains exactly three commands, in this order:

1. A read-only SWD preflight.
2. A ten-round RC-013 physical cold-boot matrix.
3. An exact verified-v1 restore and read-only runtime acceptance command.

UART3 physical reconnect is not repeated. The existing 11 successful rounds,
the retained round-12 failure, the non-physical diagnostic retry, and the later
9/9 physical continuation remain separate evidence. H4 remains PARTIAL unless a
subsequent main-agent audit can close every required sub-gate without deleting,
overwriting, or reclassifying the failure.

The pack does not run random write-window power cuts, processor-fault timing,
watchdog tests, physical FIFO/DMA loss, QSPI/U2 erase or program operations, OTA
or model writes, or SPI timing measurements. Those items remain blocked or
partial when the required stimulus, policy, or measurement equipment is absent.

## Command 1: read-only SWD preflight

The command uses an explicit STM32CubeProgrammer path and the frozen ST-Link
serial number. It must read, without reset or programming:

- ST-Link identity and firmware;
- target voltage;
- STM32H743 device identity;
- MCU UID words;
- the first application words at `0x08020000`.

Any identity mismatch or SWD connection failure stops the command pack. The
preflight never writes, resets, erases, unlocks, or auto-selects a probe.

## Command 2: RC-013 cold-boot matrix

The command takes explicit `COM13`, sealed RC-013 identity, event ID 100, ten
rounds, and an external evidence directory. Before asking for an operator action
it performs a no-write TERP preflight and requires:

- capability flags 491 and device serial `recorder-001`;
- healthy storage and zero storage/export errors;
- event 100 listed and downloadable;
- 38,560 downloaded bytes with device CRC and SHA-256 matching the baseline.

For every round the runner:

1. asks the operator to remove only board main power;
2. records explicit confirmation that the power indicator is off and that power
   remained off for at least five seconds;
3. asks the operator to restore board main power;
4. waits for TERP readiness with a bounded timeout;
5. repeats the complete read-only identity, health, list, download, CRC, length,
   and SHA checks;
6. writes one immutable round result before continuing.

The runner stops at the first failed round. A later invocation resumes only from
the first missing round and never overwrites an existing PASS or FAIL record.
Diagnostic mode can test imports, paths, identity, and readback without claiming
a physical cycle.

## Command 3: verified-v1 restore and acceptance

The command first reruns the read-only SWD preflight. It then requires the exact
219,120-byte verified-v1 application image whose SHA-256 is
`83584fd8b3bba44619d315d75e3140aebc706ba8b406f3e5e04cdfd1a17f9112`.
It programs only the application region beginning at `0x08020000`, requests
STM32CubeProgrammer verification, reads back exactly 219,120 bytes, and requires
the same SHA-256.

After reset, the command performs read-only TERP acceptance on explicit COM13
and requires the released-v1 identity, capability flags 235, healthy storage,
and readable existing events. It stores programming, readback, hash, raw TERP,
frame, and parsed-result evidence externally. Any mismatch stops immediately and
must not be hidden by another programming attempt.

## Paths and evidence

All user-facing commands use absolute paths and set
`TRANSPORT_VENV_ROOT` explicitly. All generated evidence is stored below the
external RC-013 session root, never in the source worktree. Each runner records
the command line, UTC timestamps, source revision, device/probe/port identity,
exit codes, first error, and relevant hashes.

The command pack does not update `manifest.json`, `SHA256SUMS.txt`, or hardware
gate status while it is running. The main agent reviews raw results first and
then regenerates package metadata and checksums as a separate audited step.

## Verification and review

Before delivery to the operator:

- every command is exercised from `LOCAL_USER_HOME\Desktop`;
- read-only or diagnostic modes prove path and import independence;
- failure fixtures prove fail-fast and no-overwrite behavior;
- PowerShell parsing and `git diff --check` pass;
- a specification reviewer checks gate preservation;
- a quality reviewer checks error handling and evidence integrity;
- the main agent owns final approval.
