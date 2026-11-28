# SSL9000 Console (independent emulation project)

Two stereo VST3 insert processors for Studio One and other compatible DAWs:
- **SSL9000 Channel**: per-track envelope-driven saturation and stereo width.
- **SSL9000 Master**: stereo-bus processing with the same controls and a lower detector threshold.

Both plugins use the DAW's normal audio routing and summing. There is no inter-plugin audio transport, shared-memory session, Channel ID, Bus ID, Bus Receive or Bus Send dependency. Insert Channel on individual tracks and Master on the stereo output or any bus you want to process.

## Double-precision processing

Both processors advertise VST3 double-precision support to the host. The DSP state and intermediate calculations (envelope follower, detector, saturation curve and stereo matrix) use C++ `double`. Float processing is also implemented for hosts that choose the 32-bit sample path; the calculations inside the DSP still use double precision in that mode.

## DSP

The current prototype uses a linked-stereo envelope follower (8 ms attack / 120 ms release). As the detected signal level rises above the processor's threshold, the nonlinear transfer curve progressively increases, generating more harmonics on louder passages. Drive sets the maximum intensity; it is not a static gain boost. Stereo Width controls the mid/side balance; there is no left/right crosstalk or channel-mixing control. This is an independent work-in-progress and is not an exact or measured emulation of any hardware console, nor is it affiliated with or endorsed by Solid State Logic.

## Build

The CMake project fetches JUCE 8.0.6 at configure time. On Windows, use Visual Studio 2022 with the C++ desktop workload:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The GitHub Actions workflow builds the Channel and Master VST3 targets and uploads them as an artifact.

## Licensing

JUCE is fetched from its upstream repository under its applicable licensing terms. Review JUCE's current licensing requirements before distributing binaries commercially.
