# Phase 05 evidence checklist

## Automated evidence

- H743 build: `scons -C firmware -j4`.
- Native C tests: pool lifecycle/order, trigger boundaries, pretrigger ring,
  event state machine and EV01 golden vector.
- Host Python tests: valid EV01 parse plus bad magic, bad length and CRC
  corruption rejection.

## Hardware evidence still required

Create and retain these files after board testing:

- `static-01.ev01` through `static-10.ev01`;
- `knock-01.ev01` through `knock-10.ev01`;
- `drop-01.ev01` through `drop-10.ev01`;
- matching JSON/CSV plots and a threshold-selection note;
- one 100-cycle `event trigger_test` run showing the free-block count returns
  to its baseline after every completed export or clear.

Do not treat simulation or native tests as a substitute for these board-level
results.
