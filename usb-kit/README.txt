DOOM on the Sony Dash - USB kit
================================

WHAT THIS DOES
--------------
Copy everything in this folder to the ROOT of a FAT32-formatted USB stick,
plug it into the Dash, and boot into the "Special Options" recovery menu
(hold the button combo while powering on). The Dash's own recovery/unbrick
UI will run this kit automatically and DOOM will take over the screen in
about 10-15 seconds. No PC, network, or wifi is needed - this is fully
self-contained.

Video only - there is no sound yet. See the write-up for why (the short
version: the recovery environment's kernel has no sound core, and the
digital audio pipeline hits a wall right before the physical speaker).

FILES
-----
  flashplayer.cfg   Tells the recovery Flash player to preload unbrick.swf
                     (this is what gives us a hook to run our own code)
  unbrick.swf        Stock unbrick widget - calls unbrick_sync then unbrick_async
  unbrick_sync       Stock sync-check script (unmodified)
  unbrick_async      OUR launcher: kills the recovery menu, stages the DOOM
                     binary + WAD into RAM, and runs DOOM. Auto-restarts DOOM
                     if it ever crashes. Fully offline - no bridge/network calls.
  doom_dash_dcc      DOOM (doomgeneric), cross-compiled for the Dash's MIPS CPU,
                     rendering through Sigma's native RM/DCC display API
                     (not the Linux framebuffer - see the write-up for why).
  doom1.wad          Shareware DOOM game data (id Software, freely distributable).

HOW TO GET BACK TO NORMAL
--------------------------
Just power-cycle the Dash without the USB stick inserted, or without holding
the recovery button combo - it boots its normal Sony/Chumby control panel
exactly as before. This kit only affects the recovery/USB-boot session; it
does not modify anything on the device's internal flash.

TROUBLESHOOTING
----------------
- Blank screen after boot: give it the full 10-15 seconds: the recovery menu
  loads first, then our script kills it and launches DOOM.
- Nothing happens at all: check the USB stick is FAT32 and these files are
  at the ROOT of the drive, not in a subfolder.
- Want to see what happened: /tmp/dashctl.log and /tmp/doom.log on the
  device (accessible if you also have a shell path in, e.g. over SSH on a
  normal boot) show the launch sequence and any errors.

Full technical write-up (platform details, the display/audio investigation,
and a blueprint for other Sigma SMP86xx-based hardware) is included
separately as doom_on_dash_writeup.html / doom_on_dash_punbb_post.txt.
