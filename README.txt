OLD RECORDER - vintage cassette/handheld recorder effect (VST3)

BUILD (no tools needed on your PC):
1. Create a free GitHub account and a new repository.
2. Upload everything in this folder (keep the .github folder and Source folder).
3. Open the repo's "Actions" tab -> wait for "Build VST3" to finish (~5-10 min).
4. Open the finished run, download the "OldRecorder-VST3" artifact, unzip.
5. Copy "Old Recorder.vst3" to  C:\Program Files\Common Files\VST3\
6. FL Studio: Options > Manage plugins > Find plugins (Verify plugins) > then add
   "Old Recorder" from the Plugin database > Installed > Effects.

CONTROLS
Amount   - master strength (0 = clean bypass, 1 = full vintage). Scales everything below.
Tone     - low-pass cutoff at full Amount (lower = darker/lofi). Try 5-9 kHz.
Low Cut  - high-pass cutoff at full Amount (thin small-speaker bass loss).
Wow      - pitch wobble (slow wow + fast flutter).
Hiss     - tape noise.
Saturate - soft tape-style saturation.
Output   - makeup gain.
