# Phase 4 SWD Boundary Amendment

## Decision

The manual command pack no longer treats HOTPLUG target-memory reads as a
formal prerequisite for the RC-013 cold-boot matrix. The existing probe-list
result remains valid only as evidence that the expected ST-Link serial was
enumerated. The target-memory results collected on 2026-08-29 are invalid and
must not be cited as a PASS.

The ten-round TERP cold-boot matrix remains independent and may proceed after
its own frozen RC-013 read-only preflight. SWD reset, halt, erase, programming,
verification, and readback remain prohibited during this matrix.

The verified-v1 restore command is the sole reset and Flash-mutation boundary.
It will use the historically proven NORMAL STM32CubeProgrammer connection mode
instead of HOTPLUG. The operator acknowledgement remains mandatory before the
first reset or write. The image length, image SHA-256, explicit probe serial,
and current RC-013 identity on explicit COM13 must still be validated through
read-only checks before that boundary.

## Evidence behind the amendment

STM32CubeProgrammer 2.22 returned exit code zero for two HOTPLUG reads while
producing inconsistent target identity and data:

- the UID connection reported device ID `0x450` but returned three repeated
  `0x00000800` words instead of the sealed UID;
- the application connection reported device ID `0x800`, emitted database and
  Flash-loader errors, and returned four repeated `0x411FC271` words;
- the original checker validated target identity only for the UID command and
  accepted any four parseable application words, so it incorrectly returned
  `pass=true`.

Historical sealed evidence collected with the reliable NORMAL connection
contains device ID `0x450`, UID words `003E0026 3433510C 34393738`, RC-013
application words `24001600 08024819 0802486D 0802041D`, and exact v1
application words `24001600 080240ED 08024141 080203C5`.

## Required implementation changes

1. Harden the SWD checker so every target connection must report the expected
   device identity and must contain no CubeProgrammer error line. Validate the
   exact sealed UID and the expected RC-013 application words. The two existing
   2026-08-29 sessions remain immutable failure/inconclusive evidence and are
   never overwritten or reclassified.
2. Do not ask the operator to repeat the HOTPLUG memory preflight. Command 1 is
   recorded as probe enumeration success with target-memory evidence invalid.
3. Keep the cold-boot command unchanged and independently gated by its TERP
   identity, health, event length, CRC, and SHA-256 checks.
4. Before the restore boundary, enumerate the exact probe and perform a
   read-only TERP check of the current RC-013 identity on COM13. Do not perform
   any SWD target-memory read in this pre-boundary phase.
5. Change the v1 restore programmer and readback connections to explicit NORMAL
   mode. Keep exactly one programming attempt, exact application address
   `0x08020000`, verify, exact-length readback, SHA-256 equality, and COM13 v1
   acceptance.
6. The restore command must announce that operator confirmation crosses the
   reset/mutation boundary. No reset or write occurs before the exact
   `RESTORE_VERIFIED_V1` acknowledgement.

## Acceptance

- Synthetic fixtures reproducing the 2026-08-29 `0x800`/`Error:` output fail.
- Wrong or repeated UID/application words fail even when CubeProgrammer exits
  zero.
- The cold-boot matrix remains ten rounds, resumable only from complete formal
  PASS records, and diagnostic runs remain non-formal.
- The restore command uses the pinned verified-v1 image, explicit probe serial,
  NORMAL connection only inside the acknowledged restore boundary, one program
  attempt, exact readback hash, and released-v1 runtime acceptance.
- No firmware, protocol, vector table, Flash address, or v1 binary changes.
