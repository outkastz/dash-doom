#!/bin/sh
# DOOM OVER LIVE FLASHPLAYER: do NOT kill/freeze/rebuild anything. The display
# pipeline stays exactly as the flashplayer set it up (menu live, compositor running,
# fb0 = the displayed buffer). DOOM mmap-writes fb0 at ~35fps and paints over the menu.
B="172.19.204.99:9009"
rep(){ curl -s -m 6 -d "$1" "http://$B/log" >/dev/null 2>&1; }
sq(){ tr '\n' '|' | tr -d '\r'; }

# already running? don't double-launch (the remote hook re-runs this every few sec)
if pidof doom_dash >/dev/null 2>&1; then exit 0; fi

wget -q -O /tmp/doom_dash "http://$B/doom_dash" && chmod +x /tmp/doom_dash
cd /tmp; rm -f /tmp/doom.log
rep "launching DOOM over LIVE flashplayer (nothing killed)"
/tmp/doom_dash -iwad /mnt/usb/doom1.wad -gfxmode rgb565 -mb 8 >/tmp/doom.log 2>&1 &
DP=$!
sleep 5
rep "doom-alive: $(kill -0 $DP 2>/dev/null && echo YES || echo NO) flash: [$(ps 2>/dev/null | grep -iE 'chumbyflashplay' | grep -v grep | awk '{print $1}' | tr '\n' ',')]"
# let it run; the bootstrap loop waits on this
wait $DP
rep "doom EXITED rc=$?"
