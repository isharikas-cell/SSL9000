# SSL9000 Console Link: implementation design

Status: engineering proposal; **not implemented**. This document records the transport design to validate before enabling inter-plugin audio.

## Signal topology

- Each SSL9000 Channel instance processes its own stereo track and publishes one timestamped stereo block to a selected bus.
- SSL9000 Bus Receive, first in a normal Studio One bus insert chain, reads and sums the selected channel streams.
- Ordinary Studio One VST3 inserts remain between Bus Receive and Bus Send and therefore process the actual summed bus audio.
- SSL9000 Bus Send, last in that insert chain, publishes the post-insert stereo bus block to the master stream.
- SSL9000 Master reads and sums the selected bus streams, then applies master processing.

Receive and Send are not allowed to silently fall back to a second parallel signal path. A visible, deterministic disconnected/underrun state is required.

## Proposed transport

Use a versioned, process-shared transport on Windows, with a platform abstraction so the DSP and protocol can be unit-tested independently.

- One shared session namespace per user/session and project instance; no global fixed mapping shared by unrelated projects.
- Stable project/session UUID, 1..32 bus identifiers, and stable per-channel source identifiers.
- A bounded lock-free single-producer/single-consumer ring per source-to-bus stream. A bus mixer reads up to 32 stereo streams. A separate ring carries each completed bus stream to Master.
- Each slot carries protocol version, stream ID, monotonically increasing sequence, absolute project sample position, sample count, channel count, and audio payload.
- Audio callbacks must never allocate, wait on a mutex, perform file/network I/O, or block waiting for another plugin instance.
- The consumer sums only blocks with matching project sample positions. Missing or late blocks are handled by a defined silence/underrun policy and diagnostic counters; stale blocks are discarded. No callback-order assumption is permitted.
- Shared-memory creation, discovery, cleanup, crash recovery, and ownership are handled outside the real-time audio callback. Names/ACLs must prevent cross-user collisions and unintended access.

## Timing and latency

A one-audio-block pipeline delay is the initial design target to remove same-block callback-order races. It must be reported to the host through the plugin latency API and validated with Studio One delay compensation enabled. Block size can vary: packets are sample-positioned and must not assume a permanently fixed host block size. The implementation must explicitly handle seeks, loop wrap, transport stop/start, sample-rate changes, and plugin re-prepare.

Third-party insert latency is expected to be handled by Studio One's normal plug-in delay compensation within the bus insert chain. Bus Send publishes the audio it receives after those inserts; Master aligns bus streams by absolute sample position and the declared console-link pipeline delay. Do not add a second guessed compensation for insert latency.

## Failure and safety behavior

- Missing session, incompatible protocol version, duplicate stream identity, ring overflow/underrun, host without valid project sample position, or unsupported process isolation must produce a clear status and deterministic silence/pass-through behavior documented per plugin.
- No stale audio from a previous project or transport pass may be replayed after a seek or reinitialization.
- Plugin bypass must not accidentally publish duplicate audio or leave stale ring slots.
- Session teardown must tolerate plugins being removed in any order and host/plugin crashes.
- The implementation must be stress-tested with plugins hosted in separate processes if Studio One's hosting mode permits it.

## Required validation before calling Console Link usable

1. Unit tests for ring wraparound, sequence handling, variable block sizes, sample-position alignment, overflow, underrun, reset and concurrent producer/consumer operation.
2. Integration test with 32 channel publishers feeding at least four independent buses, with no cross-bus leakage.
3. Insert third-party latency-bearing VST3 effects between Receive and Send; compare impulse/null alignment against a reference route.
4. Verify no double summing, no bypass around inserts, and no duplicated master contribution.
5. Test sample rates 44.1/48/88.2/96 kHz, multiple block sizes, transport seeks/loops, stop/start, project reload and plugin removal.
6. Build Windows x64 VST3 bundles and validate scanning, loading, automation/state restore and playback in Studio One.

## Current implementation status

The Channel and Master plugins remain standalone stereo processors. A first isolated `BusSummingStage` DSP primitive now exists: it sums up to 32 planar stereo sources and applies optional post-sum dynamic saturation without allocating in the processing call. Unit coverage has been added for transparent linear summing, driven peak reduction, and the 32-source limit. This primitive is not yet connected to a VST3 processor or Console Link transport. Shared-memory transport, Bus Receive, Bus Send, project/session identity, latency reporting for Console Link, and Studio One integration tests remain unimplemented. Until the complete path is validated, use Studio One's native bus routing for reliable summing.
