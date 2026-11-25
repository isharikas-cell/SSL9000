# SSL9000 Console Link: implementation design

Status: transport primitives are under implementation; the end-to-end inter-plugin audio path is **not implemented**. This document records the design and validation gates before enabling inter-plugin audio.

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
- A bounded lock-free single-producer/single-consumer ring per channel source. Each published block carries `busId` and `sourceId`; a channel selects exactly one destination bus at a time. Only the matching Bus Receive consumes that channel ring. A bus mixer reads up to 32 assigned stereo streams. A separate ring carries each completed bus stream to Master.
- The fixed session layout deliberately avoids a 32 × 32 matrix of large audio rings. Channel routing is block metadata, while each channel ring retains one producer and one designated consumer. Protocol version 2 adds the explicit routing identity to `BlockHeader`; version-1 mappings are rejected.
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

The Channel and Master plugins remain standalone stereo processors. A first isolated `BusSummingStage` DSP primitive exists and is covered by linear-sum, driven-peak, and 32-source-limit tests. A real-time-safe `ChannelAudioPublisher` endpoint now binds a channel ring to a source ID and destination bus ID and publishes timestamped stereo blocks without allocation or waiting; unit tests cover valid/invalid identities and routing metadata. A bounded `BusAudioReceiver` now consumes the 32 channel rings for one bus, aligns packets against the requested absolute sample range, sums matching audio, retains future and partially consumed packets across variable host block sizes, and drops stale or misrouted packets. Its callback path uses fixed storage and bounded ring reads only; tests cover timestamp alignment, source summing, future-packet retention, partial-block consumption, stale data, and cross-bus rejection. A Windows `WindowsConsoleLinkSession` helper now derives a deterministic `Local\\` mapping name and session nonce from an explicit UUID-like project/session key. A named Windows mutex serializes mapping creation/initialization and opener validation, so no process reads the mapped C++ atomics before the creator has started and completed their object lifetimes. The mutex wait is bounded and occurs outside the audio callback. A timeout or abandoned mutex is rejected without inspecting or overwriting the mapping; automatic recovery of an abandoned/partially initialized mapping remains future work. Tests cover two independent session objects resolving to the same mapping identity, invalid keys, sample-rate mismatch, and simultaneous attach attempts where exactly one instance creates the session. The Channel publisher endpoint is not yet wired into the Channel VST3 processor. The protocol now also defines a fixed `SharedSessionMemory` layout with 32 channel publisher SPSC rings and 32 bus-to-master SPSC rings, plus initialization and compatibility checks. Protocol version 2 adds `busId` and `sourceId` to each block so a channel ring can be routed to one selected bus without a 32 × 32 ring matrix; tests exercise routing metadata across independent bus destinations, both ring directions, and session metadata. A Windows-only `WindowsSharedMemoryRegion` wrapper now creates or opens a caller-named `Local\\` file mapping, owns the mapped view with RAII, and validates mapped session metadata. A newly-created mapping starts C++ object lifetimes before session initialization; only the creator initializes it. Unit tests verify that a second mapping handle sees the same initialized session. A Windows-only child-process test now launches the same test executable in consumer mode; the parent creates and initializes the mapping, publishes a stereo block into channel ring 5, and the child independently opens the named mapping and validates the block payload and project sample position. This is still transport infrastructure only: automatic acquisition of a stable project UUID from Studio One, cross-process ownership/ACL policy, creator/open race recovery, producer/consumer integration, Bus Receive, Bus Send, latency reporting, and Studio One integration remain unimplemented. Until the complete path is validated, use Studio One's native bus routing for reliable summing.
