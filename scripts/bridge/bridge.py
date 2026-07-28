#!/usr/bin/env python3
import http.server, sys, time, os
W,H = 800,480
HERE=os.path.dirname(os.path.abspath(__file__))
def rgb565(r,g,b): return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
_n=[0]
_dseq=[0]
def test_frame():
    buf=bytearray(W*H*2); cols,rows=4,2
    pal=[(200,40,40),(40,160,40),(40,40,200),(200,160,40),(160,40,200),(40,200,200),(200,80,140),(120,120,120)]
    t=int(time.time())%2
    for y in range(H):
        r0=y*rows//H
        for x in range(W):
            r,g,b=pal[r0*cols+(x*cols//W)]
            if x%(W//cols)<4 or y%(H//rows)<4: r=g=b=(255 if t else 0)
            v=rgb565(r,g,b); o=(y*W+x)*2; buf[o]=v&0xff; buf[o+1]=(v>>8)&0xff
    return bytes(buf)
class Hd(http.server.BaseHTTPRequestHandler):
    def log_message(self,*a): pass
    def do_GET(self):
        if self.path.startswith('/frame'):
            _n[0]+=1
            if _n[0]==1 or _n[0]%15==0: print("[FRAME] #%d <- %s"%(_n[0],self.client_address[0]),flush=True)
            d=test_frame(); self._send(200,d,'application/octet-stream')
        elif self.path.startswith('/ctl.sh'):
            try: d=open(os.path.join(HERE,'ctl.sh'),'rb').read()
            except: d=b'#!/bin/sh\nsleep 3\n'
            print("[CTL] served ctl.sh (%dB) -> %s"%(len(d),self.client_address[0]),flush=True)
            self._send(200,d,'text/plain')
        elif self.path.startswith('/nctl.sh'):
            try: d=open(os.path.join(HERE,'..','dashctl_usb','nctl.sh'),'rb').read()
            except: d=b'#!/bin/sh\nsleep 3\n'
            print("[NCTL] served nctl.sh (%dB) -> %s"%(len(d),self.client_address[0]),flush=True)
            self._send(200,d,'text/plain')
        elif self.path.startswith('/go'):
            if os.path.exists(os.path.join(HERE,'go.flag')): self._send(200,b'go','text/plain')
            else: self._send(404,b'wait','text/plain')
        elif self.path.startswith('/log'):
            import urllib.parse as _up
            q=_up.parse_qs(_up.urlparse(self.path).query)
            print("[LOG] %s (%s)"%(q.get('m',[''])[0],self.client_address[0]),flush=True)
            self._send(200,b'ok','text/plain')
        elif any(self.path.startswith(p) for p in ('/doom_dash_snd','/em8xxxoss.ko','/em8xxxalsa.ko','/em8xxx.ko','/em8xxx_i2c.ko','/tone440.raw','/tone440.wav','/ossplay','/rfs1_sshd.img','/rfs1_ssh.img','/rfs1_stock.img','/rfs1_new.img','/userhook0','/audtone_dash','/audtest_dash','/colorbars_dash','/doom_dash_dcc','/doom_dash','/doom1.wad','/fbprobe','/showimg2','/fliptest','/dbtest','/osdtest_dash','/osdtest','/showimg','/titledump','/tone','/red.bin','/green.bin','/blue.bin','/half.bin')):
            fn={'/doom_dash_snd':'doom_dash_snd','/em8xxxoss.ko':'em8xxxoss.ko','/em8xxxalsa.ko':'em8xxxalsa.ko','/em8xxx.ko':'em8xxx.ko','/em8xxx_i2c.ko':'em8xxx_i2c.ko','/tone440.raw':'tone440.raw','/tone440.wav':'tone440.wav','/ossplay':'ossplay','/rfs1_sshd.img':'rfs1_sshd.img','/rfs1_ssh.img':'rfs1_ssh.img','/rfs1_stock.img':'rfs1_stock.img','/rfs1_new.img':'rfs1_new.img','/userhook0':'userhook0','/audtone_dash':'audtone_dash','/audtest_dash':'audtest_dash','/colorbars_dash':'colorbars_dash','/doom_dash_dcc':'doom_dash_dcc','/doom_dash':'doom_dash','/doom1.wad':'doom1.wad','/fbprobe':'fbprobe','/showimg':'showimg','/showimg2':'showimg2','/fliptest':'fliptest','/dbtest':'dbtest','/osdtest_dash':'osdtest_dash','/osdtest':'osdtest','/titledump':'frame.bin','/tone':'tone','/red.bin':'red.bin','/green.bin':'green.bin','/blue.bin':'blue.bin','/half.bin':'half.bin'}[next(p for p in ('/doom_dash_snd','/em8xxxoss.ko','/em8xxxalsa.ko','/em8xxx.ko','/em8xxx_i2c.ko','/tone440.raw','/tone440.wav','/ossplay','/rfs1_sshd.img','/rfs1_ssh.img','/rfs1_stock.img','/rfs1_new.img','/userhook0','/audtone_dash','/audtest_dash','/colorbars_dash','/doom_dash_dcc','/doom_dash','/doom1.wad','/fbprobe','/showimg2','/fliptest','/dbtest','/osdtest_dash','/osdtest','/showimg','/titledump','/tone','/red.bin','/green.bin','/blue.bin','/half.bin') if self.path.startswith(p))]
            try:
                d=open(os.path.join(HERE,'..','dashctl_usb',fn),'rb').read()
                print("[FILE] %s (%dB) -> %s"%(fn,len(d),self.client_address[0]),flush=True)
                self._send(200,d,'application/octet-stream')
            except Exception as e:
                print("[FILE] %s MISSING (%s)"%(fn,e),flush=True); self._send(404,b'nf','text/plain')
        else: self._send(200,b'dash bridge\n','text/plain')
    def do_POST(self):
        n=int(self.headers.get('Content-Length',0)); raw=self.rfile.read(n)
        if self.path.startswith('/framedump'):
            fn=os.path.join(HERE,'frame_dump.bin'); open(fn,'wb').write(raw)
            _dseq[0]+=1
            fn2=os.path.join(HERE,'frame_dump_%d.bin'%_dseq[0]); open(fn2,'wb').write(raw)
            print("[DUMP] frame_dump.bin + frame_dump_%d.bin %dB <- %s"%(_dseq[0],len(raw),self.client_address[0]),flush=True)
            self._send(200,b'ok','text/plain'); return
        body=raw.decode('latin1').strip()
        print("[%s] %s (%s)"%('TOUCH' if '/touch' in self.path else 'LOG',body,self.client_address[0]),flush=True)
        self._send(200,b'ok','text/plain')
    def _send(self,code,data,ct):
        self.send_response(code); self.send_header('Content-Type',ct); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
if __name__=='__main__':
    p=int(sys.argv[1]) if len(sys.argv)>1 else 9009
    print("bridge on :%d  (frame /frame.bin, controller /ctl.sh, POST /touch /log)"%p,flush=True)
    http.server.ThreadingHTTPServer(('0.0.0.0',p),Hd).serve_forever()
