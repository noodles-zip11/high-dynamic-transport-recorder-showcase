# Phase 09 Desktop Application and Event Replay Design

## Status

Approved for implementation on 2026-08-01. This document fixes the software
scope for Phase 09; it does not claim USB or board acceptance.

## Goal

Provide a PySide6 desktop application that turns an event saved by the
recorder into locally stored, inspectable and exportable evidence. It reuses
the existing Python TERP protocol library instead of reimplementing frames or
event downloads in the user interface.

## In Scope

- Discover serial ports, create a cancellable device session, and display the
  identity and health data returned by the existing TERP requests.
- Browse events through paged requests and download an event with byte
  progress, cancellation and resumable temporary files.
- Store validated event files immutably under a local data directory and keep
  their index, user labels and notes in SQLite.
- Open valid local event fixtures, show event metadata and render acceleration
  and angular-rate channels with trigger and warning annotations.
- Export CSV, JSON, PNG and a byte-identical raw-file copy with SHA-256.
- Test device interaction using the existing simulated TERP transport and test
  replay/export using valid, CRC-invalid, truncated and unknown-version files.

## Explicitly Out of Scope

- Real-time preview and any change to the recorder's acquisition path. A
  preview protocol needs a separate design for record ownership, rate limiting
  and backpressure before it can be connected to the 1.6 kHz pipeline.
- Changes to TERP framing, event-log formats, pin mapping, firmware timing or
  board-side recording behaviour.
- Device-event deletion. EL01 has no approved safe deletion or garbage
  collection semantics; the UI presents it as unsupported rather than issuing
  a destructive request.
- Hardware claims: USB enumeration, cable unplug/reconnect, physical download
  reliability and four-hour stability require a later bench acceptance run.

## Architecture

The host package keeps five directed layers:

1. `protocol` is the existing TERP codec/client and imports neither Qt nor
   serial UI concepts.
2. `transport` discovers ports and provides byte transport only; it does not
   parse event payloads.
3. `device` owns the cancellable session state machine and converts protocol
   results into immutable application view data. Blocking serial work runs in
   a worker thread.
4. `repository` owns raw-file placement, SHA-256/CRC state and versioned
   SQLite migrations. It never modifies an event file.
5. `analysis` decodes local event files and prepares bounded replay data.
   `ui` renders view models and sends user intent to the device/repository
   layer; it never hand-builds TERP frames.

Qt's main thread is limited to interface updates. A worker thread reports
results, recoverable errors and final cancellation state through signals; it
must not mutate widgets directly.

## Data Lifecycle and Integrity

For a downloaded event, the required order is:

`TERP chunks -> .part and manifest -> full CRC validation -> atomic final file
placement -> SQLite transaction -> valid local index`.

The three completion states remain distinct. A file may be fully downloaded
but fail CRC; it may pass CRC but fail the SQLite transaction; only the final
state is a valid local repository record. A missing raw file marks the index
as `missing`; no background action silently deletes the user's record.

The repository stores user labels and notes separately from device metadata.
Raw event identity is based on verified content rather than its filename.

## Replay and Export

Replay shows three-axis acceleration, acceleration magnitude, three-axis
angular rate, trigger data and available environmental or health snapshots.
Metadata supplies units and ranges. Warnings distinguish corrupt data,
truncation, unknown format versions, time invalidity, saturation and data
gaps.

Large signals use min/max envelope decimation or a visible-window query, so a
compressed display cannot hide an impact peak. Cursor values use the common
event time axis.

CSV carries fields, units and event/version metadata. JSON carries the event
summary, validation state and user annotations. PNG exports the current
rendered view. Raw export copies bytes unchanged and writes the calculated
SHA-256 beside it. Export never overwrites an existing target without a user
decision.

## Failure Handling

Errors are classified as user-correctable input/connection errors, device
rejections, data corruption and internal failures. UI controls that could
start device operations are disabled after disconnect, while locally stored
events remain available. Closing the application requests cancellation and
waits for a bounded worker shutdown. Operation identifiers and error codes go
to logs; raw event payloads do not.

## Software Acceptance

Automated checks must cover protocol-fixture import, repository rollback,
cancel/resume behaviour, CRC refusal before indexing, session-state changes,
worker-error propagation and bounded replay preparation. Manual no-device
acceptance includes launching the application, opening each fixture and
checking CSV/JSON/PNG exports by reading them back.

Passing these checks means the software slice is validated. It does not mean
the USB device path or board has been accepted.
