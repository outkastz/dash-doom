#!/bin/sh
# DOOM mode for the Dash recovery controller.
# Served live by the bridge; the USB bootstrap fetches this as /tmp/ctl.sh and runs it.
# When ready to play DOOM, copy this file over dashctl/ctl.sh (the bridge serves it live).
B="172.19.204.99:9009"
say(){ fbwrite --pos=2,28 --color=0,255,0 "$1" 2>/dev/null; wget -q -O /dev/null "http://$B/log?m=$1" 2>/dev/null; }

# 1. free the display: kill the recovery flashplayer so it stops repainting fb0
i=0; while [ $i -lt 8 ]; do
  killall chumbyflashplayer.x 2>/dev/null; killall chumbyflashplayer 2>/dev/null; i=$((i+1))
done

# 2. stage the binary in tmpfs (RAM = executable; USB is FAT and may be noexec)
if [ ! -x /tmp/doom_dash ]; then
  if [ -f /mnt/usb/doom_dash ]; then cp /mnt/usb/doom_dash /tmp/doom_dash
  else say "DOOM: fetching binary"; wget -q -O /tmp/doom_dash "http://$B/doom_dash"; fi
  chmod +x /tmp/doom_dash
fi

# 3. locate the WAD (prefer USB so it never touches RAM; bridge is the fallback)
WAD=/mnt/usb/doom1.wad
if [ ! -f "$WAD" ]; then
  WAD=/tmp/doom1.wad
  [ -f "$WAD" ] || { say "DOOM: fetching WAD"; wget -q -O "$WAD" "http://$B/doom1.wad"; }
fi

# 4. keep the flashplayer down in the background while DOOM owns fb0
( while :; do killall chumbyflashplayer.x 2>/dev/null; killall chumbyflashplayer 2>/dev/null; sleep 2; done ) &
KILLER=$!

say "DOOM: launching"
cd /tmp                      # writable dir for .default.cfg + savegames
# -gfxmode rgb565 is REQUIRED: the fb backend blits 16bpp; the i_video default is 32bpp.
/tmp/doom_dash -iwad "$WAD" -gfxmode rgb565 >>/tmp/doom.log 2>&1

kill $KILLER 2>/dev/null
say "DOOM exited (rc=$?) - see /tmp/doom.log"
sleep 3
