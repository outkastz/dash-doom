# DOOM on the Sony Dash

Sony Dash HID-C10, a.k.a. a Chumby with a Sigma Designs set-top-box chip inside. This is the log — and the code — from getting id Software's *DOOM* running on it, written up as a blueprint for whoever's next.

Nothing here required opening the case or touching a chip with a probe. Every result came from software running on the device itself, over the network, through the official recovery-flash mechanism.

## Status

| Piece | Status | Notes |
|---|---|---|
| Native display control (bypassing Flash) | **Done** | Full RM/DCC path, double-buffered, correct color |
| DOOM, playable, full screen | **Done** | ~40 fps, touchscreen input, boots straight to it |
| Digital audio pipeline (decode → DSP) | **Done** | Verified reproducibly via webcam-mic FFT detection |
| Audio reaching the physical speaker | **Blocked** | Missing kernel sound core in the recovery environment |
| Root shell on the normal (sound-capable) boot | **Open** | Should work per the firmware's own logic; doesn't yet on real hardware |

See [Open problems](#open-problems) below — this is where community help would matter most.

## Quick start: run DOOM from a USB stick

[`usb-kit/`](usb-kit/) is a self-contained kit — no PC, network, or wifi required.

1. Copy everything in `usb-kit/` to the **root** of a FAT32-formatted USB stick.
2. Plug it into the Dash and boot into the **Special Options** recovery menu (button-combo + power).
3. DOOM takes over the screen in about 10–15 seconds.
4. Power-cycle without the stick (or without the button combo) to return to the normal Sony/Chumby control panel — nothing on internal flash is touched.

Video only for now — see [The audio investigation](#the-audio-investigation) for why.

## Repo layout

```
src/doom/        DOOM (doomgeneric) backend + display/audio test tools, C source
scripts/device/  Scripts that run ON the Dash (launch/control DOOM, boot hooks)
scripts/bridge/  PC-side dev server used while iterating (serves files, logs status)
scripts/tools/   Webcam-mic FFT tone detector used to verify audio without listening
usb-kit/         Ready-to-copy, offline, self-contained USB kit (binary + WAD included)
```

## The platform

The Dash boots one of two root filesystems depending on a kernel command-line flag (`rfsp=`), read out of U-Boot environment variables that are editable at runtime via `setxenv`:

| Flag | Rootfs | Normally reached by | What runs there |
|---|---|---|---|
| `rfsp=9` | rfs1 (64 MB) | ordinary power-on | The real Sony/Chumby control panel. Sound hardware is fully initialized. |
| `rfsp=8` | rfs2 (30 MB) | button-combo + power → recovery menu | A minimal Flash-based recovery/unbrick UI, meant only for reflashing. |

The two kernels in flash turned out to be **byte-identical** — there's no separate "sound kernel." The audio difference between boot modes is entirely userspace. Both rootfs images are cramfs with a 16 KB block size (the MIPS kernel here runs 16 K pages, not the usual 4 K).

## Escaping Flash — getting our own pixels on screen

The recovery UI is a Flash movie that preloads a second SWF off the USB stick used to boot it — that's our code-execution foothold.

**Dead end: the Linux framebuffer.** `/dev/fb0` looked normal, but writing to it only ever displayed one frame. The Sigma OSD compositor doesn't continuously scan the framebuffer — it only re-composites when specific hardware registers get poked, and even that turned out to be a no-op with only one buffer allocated. `/dev/fb0` is a passive memory window; it does not drive the panel.

**What actually drives it:** Sigma's userspace "Rich Media" API (`librua`/`libdcc`) — the same stack the Flash player uses. No headers ship on the device; a community SDK for a sibling chip ([jur/smp86xxsdk](https://github.com/jur/smp86xxsdk)) was ABI-compatible enough to bootstrap from, once linked against the Dash's own `.so` files.

Key finding: the SDK defaults to a display-init mode that leaves the recovery UI's existing display setup in place, which makes the "route this to the screen" call block forever. Switching to `DCCInitMode_InitDisplay` — making DCC take ownership of the whole display chain — fixed it immediately.

Second surprise: colors were wrong (grays rendered green, gold rendered blue). The OSD composites pixels as YUV even when the profile claims RGB565. Fix: a 65,536-entry RGB565→YUV565 lookup table, applied per pixel.

## Porting DOOM

Built on [ozkl's doomgeneric](https://github.com/ozkl/doomgeneric) (Techflash's linuxvt fork) with a new backend (`src/doom/doomgeneric_dccdash.c`) that does the DCC setup once at startup and, each frame, upscales DOOM's native 320×200 buffer into whichever OSD buffer isn't on screen, then flips.

- Toolchain: Bootlin `mips32el--glibc--stable-2020.02-2` (glibc 2.31 build with a pre-2.34 C runtime, producing binaries that only need GLIBC_2.7 — matching the Dash's ancient glibc 2.8)
- Linked against the Dash's *own* `librua`/`libdcc`/`libllad`, not the SDK's stand-ins
- 320×200 native, nearest-neighbor scaled to fill 800×480
- Input: touchscreen (evdev) + UDP fallback for PC testing
- ~40 fps measured

One bug worth flagging: the frame-drawing hook mapped a buffer fresh every frame but never unmapped it. The kernel's pool of mappable regions is finite — the game ran perfectly for ~11 seconds then segfaulted the instant the pool was exhausted. Fixed by mapping both OSD buffers once at startup and holding those pointers for the process's lifetime.

## The audio investigation

This is where most of the effort went, and the one piece still unsolved. Every layer of the digital pipeline works; nothing reaches the speaker.

```
DOOM PCM data
  |  RUASendData()  ->  confirmed: 49/49 sends accepted, decoder state = playing
  v
MRUA audio decoder (Sigma DSP)
  |  DCCSetAudioPcmxFormat()  ->  confirmed OK, 44.1kHz/16-bit/stereo
  v
Audio engine -> I2S output routing
  |  X never configured -- this is the wall
  v
External I2C audio codec (init/unmute confirmed working over I2C)
  v
Speaker
```

**Root cause:** the recovery rootfs's kernel has no Linux sound core at all — `CONFIG_SOUND` is unset, and there's no `soundcore.ko` anywhere in the firmware. The ALSA/OSS drivers that would normally exist both fail to load with `Unknown symbol register_sound_dsp`/`snd_card_new`.

The driver that *does* perform the engine-to-I2S routing (on the normal, production boot) sets roughly twenty undocumented properties on the audio engine module. We can call the same underlying API it uses (`libaudiooutports.so`'s `OutportsAudioApplyOptions`) — but the very first property write in that sequence blocks forever inside the kernel, waiting on engine state that only the missing driver's own init would normally establish first.

Confirmed working along the way: the DSP microcode loads correctly, the decode pipeline genuinely runs (verified via webcam-mic FFT/Goertzel tone detection — [`scripts/tools/detect_tone.py`](scripts/tools/detect_tone.py)), and the analog codec/amp/speaker chain is alive (toggling its mute produces an audible click).

## What we learned about the firmware's security model

- Firmware images are integrity-checked (plain MD5), **not cryptographically authenticated**.
- The one real signature on this device (a DSA key) only gates downloadable Flash content — never the firmware.
- U-Boot doesn't verify the rootfs at all.
- A single string, `/etc/firmware_build_type`, decides "production" vs. "developer" behavior across every startup script. Only `production/release`/`production/candidate` take the locked path.

**Open discrepancy:** on paper, changing that one file's value should unlock a root shell on the sound-capable normal boot. In practice, every attempt on real hardware (renaming the file, and separately, rebuilding the rootfs image with the value changed) loops the boot before that check is ever reached — even though a rebuild that changes nothing boots fine. Something earlier in boot is sensitive to the modification in a way we haven't isolated.

## The blueprint — reusing this on other Sigma-based boxes

The SMP865x/86xx family shipped in a lot of early-2010s IPTV set-top boxes and smart-display products.

1. Find an unsigned, writable code-execution foothold first (recovery menus, diagnostic modes, community firmware).
2. Don't trust the Linux framebuffer device if there's a real hardware compositor sitting behind it — check for a vendor userspace media API before burning days on fb ioctls.
3. Community SDKs for a *sibling* chip are worth trying even off-target — ABI overlap within a chip family is often good enough to bootstrap from.
4. Trace security boundaries empirically, not just by reading scripts — a check that looks trivially bypassable on paper may not behave that way on real hardware.
5. Instrument your verification, especially for anything analog — a microphone + FFT tone detector meant every audio experiment could be checked automatically.

## Open problems

Two concrete, well-bounded problems remain:

- **Audio output routing without a sound core.** Need either the missing kernel sound-core symbols made available in the recovery environment, or the specific sequence of RM engine properties the ALSA driver sets before its first (blocking) output-routing write, so it can be replicated from userspace in order.
- **Why a value-only rootfs modification loops the normal boot.** Static analysis says it should be harmless; live testing (twice, two different methods) says otherwise.

Issues and PRs welcome.

## License

The DOOM source in `src/doom/` is derived from `doomgeneric`/the original *DOOM* source release and is under the **GNU GPL v2** (see [`LICENSE`](LICENSE)); the rest of this repository is offered under the same terms for consistency. Runtime linkage against the Dash's own Sigma/Chumby system libraries is dynamic — no proprietary firmware is included in this repository.
