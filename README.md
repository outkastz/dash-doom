# DOOM on the Sony Dash

Native *DOOM*, running on a Sony Dash HID-C10 (the Sigma-chip smart display Chumby made for Sony), booted straight off a USB stick. No PC, no network, no soldering.

![status](https://img.shields.io/badge/video-working-brightgreen) ![status](https://img.shields.io/badge/audio-not%20yet-red)

## Run it

1. Download this repo (or just the [`usb-kit/`](usb-kit/) folder) and copy everything inside `usb-kit/` to the **root** of a FAT32-formatted USB stick — files loose at the top level, not in a subfolder.
2. Plug the stick into the Dash.
3. Power it on holding the button combo for **Special Options** (recovery mode).
4. Wait ~10–15 seconds. The recovery menu appears, then DOOM takes over the whole screen.

To go back to normal: power-cycle without the stick (or without holding the combo). Nothing on the device's internal flash is touched — this only affects the USB-boot session.

**No sound yet** — see [Why there's no audio](#why-theres-no-audio) below.

**Stuck?** See [Troubleshooting](#troubleshooting).

## What's in this repo

```
usb-kit/     Everything from step 1 above — copy this to a USB stick
src/doom/    The C source: DOOM's display/input backend + the display/audio tools used to build it
tools/       A webcam-mic FFT tone detector, used to verify audio without needing to listen
```

## How this works

The Dash's recovery menu is a Flash app that, by design, preloads a widget off the USB stick used to boot it — that's the hook this whole project runs on. From there:

- **Display:** `/dev/fb0` looked like the normal way to draw to the screen, but only ever shows one frame — the panel is actually driven by Sigma's own undocumented `librua`/`libdcc` API, not the Linux framebuffer. DOOM renders through that instead, double-buffered.
- **DOOM itself** is [doomgeneric](https://github.com/ozkl/doomgeneric) with a custom backend, cross-compiled for the Dash's MIPS CPU, running at ~40 fps.
- **Audio** decodes correctly all the way through the DSP but never reaches the speaker — the recovery environment's kernel has no sound core at all, so the final output-routing step has nothing to call into.

### Why there's no audio

Every digital step works and is verified (see `tools/detect_tone.py`): the audio microcode loads, the decoder accepts and plays PCM data in real time, and the physical codec/amp/speaker chain is confirmed alive (its mute click is audible). What's missing is the routing between the two — normally handled by an ALSA driver that can't load here because the kernel is missing sound support entirely. The one available workaround (calling the same low-level routing function the driver would) hangs indefinitely waiting on kernel state that driver's own init would normally set up first.

### Open problems

- **Audio output routing without a sound core** — need the property sequence a working ALSA driver sets up before the routing call that currently hangs.
- **A modified boot config loops instead of booting** — should work per the boot scripts' own logic, doesn't yet on real hardware, cause not yet isolated.

Issues and PRs welcome.

## Troubleshooting

- **Blank screen:** give it the full 10–15 seconds — the recovery menu loads first, then gets replaced by DOOM.
- **Nothing happens at all:** confirm the stick is FAT32 and the files are at the drive's root, not inside a folder.
- **Debugging further:** `/tmp/dashctl.log` and `/tmp/doom.log` on the device record the launch sequence and any errors, if you have another way to shell in.

## License

The DOOM source in `src/doom/` is derived from `doomgeneric`/the original *DOOM* source release and is under the **GNU GPL v2** (see [`LICENSE`](LICENSE)); the rest of this repository is offered under the same terms for consistency. Runtime linkage against the Dash's own Sigma/Chumby system libraries is dynamic — no proprietary firmware is included in this repository.
