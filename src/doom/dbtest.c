// dbtest -- verify double-buffered pan on em8xxxfb.
// Fills buffer 0 = WHITE, buffer 1 = BLACK, then alternates FBIOPAN_DISPLAY between
// yoffset 0 and yres every 600ms. If the panel BLINKS white/black, double-buffer pan
// re-composites the OSD (the mechanism DOOM needs). Prints geometry so we see if the
// OSD was actually set up double-buffered.
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    int iters = argc > 1 ? atoi(argv[1]) : 40;

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("open fb0"); return 1; }
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    ioctl(fd, FBIOGET_VSCREENINFO, &v);
    ioctl(fd, FBIOGET_FSCREENINFO, &f);
    size_t bufBytes = (size_t)f.line_length * v.yres;
    printf("dbtest: %ux%u yvirt=%u stride=%u smem_len=%u bufBytes=%u double=%d\n",
           v.xres, v.yres, v.yres_virtual, f.line_length, f.smem_len,
           (unsigned)bufBytes, (v.yres_virtual >= 2*v.yres && f.smem_len >= 2*bufBytes));

    // if there's memory for 2 buffers but yres_virtual wasn't doubled, set it ourselves
    // (check_var permits yres_virtual > yres, confirmed by disassembly).
    if (f.smem_len >= 2*bufBytes && v.yres_virtual < 2*v.yres) {
        v.yres_virtual = 2*v.yres; v.xres_virtual = v.xres; v.xoffset = 0; v.yoffset = 0;
        v.activate = FB_ACTIVATE_NOW;
        int pr = ioctl(fd, FBIOPUT_VSCREENINFO, &v);
        ioctl(fd, FBIOGET_VSCREENINFO, &v);
        printf("dbtest: forced yres_virtual -> %u (put ret=%d)\n", v.yres_virtual, pr);
    }

    size_t maplen = f.smem_len;
    unsigned char *m = mmap(0, maplen, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == (void*)-1) { perror("mmap"); return 1; }

    if (v.yres_virtual < 2*v.yres || f.smem_len < 2*bufBytes) {
        printf("dbtest: NOT double-buffered -> pan is a no-op; setup failed\n");
        return 2;
    }
    memset(m, 0xFF, bufBytes);              // buffer 0 = white
    memset(m + bufBytes, 0x00, bufBytes);   // buffer 1 = black

    for (int i = 0; i < iters; i++) {
        v.yoffset = (i & 1) ? v.yres : 0;
        v.xoffset = 0; v.activate = FB_ACTIVATE_NOW;
        int r = ioctl(fd, FBIOPAN_DISPLAY, &v);
        if (i < 2) printf("pan yoffset=%u ret=%d\n", v.yoffset, r);
        usleep(1500000);   // 1.5s per state -- easy to catch on camera
    }
    return 0;
}
