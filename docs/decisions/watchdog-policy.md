# Phase 03 watchdog policy

The watchdog is intentionally disabled during BSP bring-up. It must not be enabled until clock, reset reason reporting and long-running hardware tests are evidenced.

When enabled, only a health-monitor task may feed it, and only after it receives timely health reports from critical acquisition, storage and communication tasks. Individual drivers and ISRs must not feed the watchdog.
