#!/bin/sh
# STAGED (not live yet): safe audio-DSP-ucode load BEFORE DOOM (memory free -> rmmalloc works),
# with hard guard against the null-address xload that hung the box. Deploy only with live monitoring.
B="172.19.204.99:9009"
rep(){ curl -s -m 8 -d "$1" "http://$B/log" >/dev/null 2>&1; }
[ -f /tmp/bpA.lock ] && exit 0
cd /usr/local/mrua 2>/dev/null; . ./run.env 2>/dev/null
XT=/usr/local/mrua/MRUA_src/llad_xtest
export PATH=$PATH:/usr/local/mrua/bin:/usr/local/mrua/MRUA_src/llad_smallapps:$XT
export HOME=/tmp; export LD_LIBRARY_PATH=/usr/local/mrua/lib:/lib:/usr/lib
AX=/usr/local/mruafw/audio_microcode_t3iptv_prod_dts54.xload

# ---- audio DSP ucode load+start, ONCE, BEFORE DOOM, with crash guard ----
if [ ! -f /tmp/bpA.done ]; then
  : > /tmp/bpA.lock; touch /tmp/bpA.done
  DA="$($XT/rmmalloc 0 2340777 2>/dev/null)"
  rep "bpA rmmalloc DA=[$DA]"
  if [ -n "$DA" ] && [ "$DA" != "0x00000000" ] && [ "$DA" != "0" ]; then
    xkc xload 0xaaaaaaaa $AX $DA 0 > /tmp/xl.log 2>&1
    $XT/rmfree 0 $DA >/dev/null 2>&1
    : > /tmp/us.log
    for core in a A @; do echo "==$core==" >>/tmp/us.log; xkc ustart 0xaaaaaaaa $core >>/tmp/us.log 2>&1; done
    rep "bpA audio-loaded xl:[$(cat /tmp/xl.log|tr '\n' '~'|cut -c1-100)] ustart:[$(cat /tmp/us.log|tr '\n' '~'|cut -c1-160)]"
    for c in i spf dacm spm; do /usr/bin/audioCodec $c >/dev/null 2>&1; done
    /usr/bin/audioCodec spvol 31 >/dev/null 2>&1
    rep "bpA codec unmuted"
  else
    rep "bpA SKIP xload (rmmalloc gave null; would hang) — audio ucode load deferred"
  fi
fi

# ---- DOOM ----
if ! ps 2>/dev/null | grep -qE '[d]oom_dash'; then
  wget -q -O /tmp/doom_dash_dcc "http://$B/doom_dash_dcc" && chmod +x /tmp/doom_dash_dcc
  [ -f /tmp/doom1.wad ] || wget -q -O /tmp/doom1.wad "http://$B/doom1.wad"
  ( cd /tmp && ./doom_dash_dcc -iwad doom1.wad -gfxmode rgb565 >/tmp/doom.log 2>&1 ) & sleep 8
  rep "bpA; DOOM relaunched"
else
  rep "bpA; DOOM alive"
fi
