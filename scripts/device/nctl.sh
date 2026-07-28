#!/bin/sh
# nctl.sh - NORMAL boot control (fetched by userhook0's background poll). DIAGNOSTIC first: confirm the
# normal boot completed + report audio state. Does NOT run DOOM yet.
B="172.19.204.99:9009"
rep(){ curl -s -m 6 -d "$1" "http://$B/log" >/dev/null 2>&1; }
if [ ! -f /tmp/nctl_diag.done ]; then
  touch /tmp/nctl_diag.done
  rep "NCTL-DIAG cmdline=[$(cat /proc/cmdline 2>/dev/null)] up=[$(cat /proc/uptime 2>/dev/null|cut -d' ' -f1)] build=[$(cat /etc/firmware_build_type 2>&1)]"
  rep "NCTL-DIAG procs:[$(ps 2>/dev/null|grep -iE 'flash|control|player|panel|doom|btplay|sshd'|grep -v grep|head -10|awk '{print $NF}'|tr '\n' ' ')]"
  rep "NCTL-DIAG audio: btplay-fifo=[$(ls /tmp/.btplay-cmdin 2>/dev/null)] audioCodec=[$(ls /usr/bin/audioCodec 2>/dev/null)] i2c3=[$(ls /dev/i2c-3 2>/dev/null)]"
  rep "NCTL-DIAG normal-boot COMPLETED past userhook0 (poll reached bridge)"
fi
rep "NCTL heartbeat up=[$(cat /proc/uptime 2>/dev/null|cut -d' ' -f1)] doom=[$(ps 2>/dev/null|grep -c '[d]oom_dash')]"
