#!/bin/sh
# more_startup.sh - launched by the chumby control panel on boot (production OR dev). GOOD version:
# does NOT killall rcS, does NOT block on wait_for_opening. Holds watchdog, marks /psp, starts sshd,
# backgrounds a fast poll for nctl.sh (which will run DOOM + audio), then RETURNS so the CP continues.
( [ -e /dev/watchdog ] && exec 9>/dev/watchdog && while : ; do echo 1 >&9 2>/dev/null; sleep 4; done ) &
mount -o remount,rw /psp 2>/dev/null
echo "MSU_RAN up=$(cat /proc/uptime 2>/dev/null | cut -d' ' -f1) cmd=$(cat /proc/cmdline 2>/dev/null) build=[$(cat /etc/firmware_build_type 2>/dev/null)]" > /psp/msu_ran 2>/dev/null
sync
# start sshd (empty root pw) so we have a fallback shell
[ -x /usr/chumby/scripts/start_sshd.sh ] && /usr/chumby/scripts/start_sshd.sh >/dev/null 2>&1 &
# background: network + fast poll for nctl.sh
(
  B="172.19.204.99:9009"
  /usr/chumby/scripts/start_network wlan0 >/dev/null 2>&1
  n=0
  while [ $n -lt 600 ]; do
    n=$((n+1))
    IP=$(ifconfig wlan0 2>/dev/null | grep -a 'inet addr' | head -1)
    curl -s -m 5 -d "MSU_NORMAL poll=$n up=$(cat /proc/uptime 2>/dev/null|cut -d' ' -f1) ip=[$IP]" "http://$B/log" >/dev/null 2>&1
    wget -q -O /tmp/nctl.sh "http://$B/nctl.sh" 2>/dev/null && sh /tmp/nctl.sh
    sleep 3
  done
) &
# RETURN immediately - do NOT interrupt the control panel / dash boot
