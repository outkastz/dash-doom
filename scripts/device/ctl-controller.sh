#!/bin/sh
B="172.19.204.99:9009"
say(){ fbwrite --pos=2,28 --color=0,255,0 "DCTL: $1                                        "; wget -q -O /dev/null --post-data="$1" "http://$B/log" 2>/dev/null; }
# start ONE touch reader if not already running
if [ ! -f /tmp/touch_on ]; then
  touch /tmp/touch_on
  cat > /tmp/hk.cfg <<CFG
0 250 0 500 1
250 500 0 500 2
500 750 0 500 3
750 1000 0 500 4
0 250 500 1000 5
250 500 500 1000 6
500 750 500 1000 7
750 1000 500 1000 8
CFG
  [ -c /dev/ts0 ] || mknod /dev/ts0 c 13 64
  ( hid_keys /dev/ts0 /tmp/hk.cfg | while read n; do wget -q -O /dev/null --post-data="btn=$n" "http://$B/touch" 2>/dev/null; done ) &
fi
# frame loop ~15s, killing the recovery flashplayer EACH iteration so the menu can't win
i=0
while [ $i -lt 15 ]; do
  killall -9 chumbyflashplayer.x 2>/dev/null; killall -9 chumbyflashplayer 2>/dev/null
  if wget -q -O /tmp/frame.bin "http://$B/frame.bin"; then
    cat /tmp/frame.bin > /dev/fb0 2>/tmp/fberr
  fi
  [ $i -eq 0 ] && say "loop: fbwrite rc, framebytes=$(wc -c < /tmp/frame.bin 2>/dev/null) fberr=[$(cat /tmp/fberr 2>/dev/null)]"
  i=$((i+1)); sleep 1
done
# exit -> bootstrap re-fetches ctl.sh (picks up edits)
