import re
s=open("ctl.sh").read()
oneshot='''  # one-shot: recon the flash/boot layout for modifying normal boot
  if [ ! -f /tmp/recon.done ]; then
    touch /tmp/recon.done
    {
      echo "=== /proc/mtd (flash partitions) ==="; cat /proc/mtd 2>&1
      echo "=== mtd/flash tools ==="; ls -la /sbin/flashcp /sbin/nandwrite /usr/sbin/nandwrite /sbin/mtd_debug /usr/bin/flash_* /sbin/flash_* 2>&1 | grep -v "No such"
      for t in flashcp nandwrite nanddump mtd_debug flash_eraseall flash_erase mkfs.cramfs cramfsck nandwrite.static; do which $t 2>/dev/null; done
      echo "=== mount (which partitions mounted where) ==="; mount 2>&1
      echo "=== bootloader env / boot select (nvram/xenv) ==="; ls -la /dev/mtd* 2>&1 | head
      echo "=== how recovery got here: cmdline + boot args ==="; cat /proc/cmdline 2>&1
      echo "=== kernel version ==="; uname -a 2>&1
      echo "=== can we see rootfs partitions? ==="; cat /proc/partitions 2>&1
    } >/tmp/recon.log 2>&1
    uplog /tmp/recon.log
    rep "RECON done ($(wc -l </tmp/recon.log) lines)"
    exit 0
  fi
'''
s=s.replace('  rep "DOOM ALIVE [$(echo $LIVE|tr \'\n\' \' \')] steady ($VER)"\n  exit 0',
            oneshot+'  rep "DOOM ALIVE [$(echo $LIVE|tr \'\n\' \' \')] steady ($VER)"\n  exit 0',1)
open("ctl.sh","w").write(s)
print("added recon one-shot" if "recon.done" in s else "FAILED")
