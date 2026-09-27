# ICM-45686 30-minute long-run hardware evidence

- Worktree: `feature/hardware-bringup` (Bootloader/OTA/U3 excluded).
- Image: `icm-30min.bin`; SHA-256 is recorded in the parent evidence directory.
- Build: `board_icm_long_run_diagnostic=1` with no 10-second or 10-minute override, so the firmware target is 1800 seconds.
- Programming: CubeProgrammer exit code 0; the raw console and CubeProgrammer output are `ascii-program-console.log` and `ascii-program-output.log` in this directory.
- Runtime window: `monitor-start.txt` to `monitor-finish.txt`, 1802 seconds observed, with no monitor error.
- Result read: SWD `-halt -r32` at `0x30004940`, 136 bytes, exit code 0. The unmodified raw output is `result-swd-console.log` and the CubeProgrammer log is `result-swd.log`.

Acceptance result: **PASS**. The result block reports `completed=1`, ICM configuration verification register `0x21` expected/actual `0x07`, 287853 INT1 IRQs and services, 2878530 samples, maximum FIFO depth 160, and zero FIFO/DMA error counters.

The first programming attempts in the parent directory were CubeProgrammer path/tooling failures and did not write the board. The successful programming used an ASCII temporary path because CubeProgrammer crashed when given the Chinese worktree path; the successful raw logs are retained.

After the SWD read, the board was restored with `ordinary-restore-baseline.bin` using the same ST-Link. The restore command exited 0 and verified successfully; `ordinary-restore-console.log`, `ordinary-restore-program.log`, and `ordinary-restore-exit.txt` are the raw restore evidence. The restored image SHA-256 is `C9614FED88D0E34188033759F21416394D8EA3D4BABC40EC0E9A1953371B1ABE`.
