// fbprobe -- find the REAL writable extent of the em8xxxfb mmap.
// mmaps smem_len, then pokes one u16 every 4KB writing green, catching
// SIGSEGV/SIGBUS to report the exact faulting offset instead of dying.
// Output is unbuffered so every line survives a crash.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static sigjmp_buf jb;
static void faulted(int sig) { siglongjmp(jb, sig); }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { printf("open fb0: %s\n", strerror(errno)); return 1; }

    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    ioctl(fd, FBIOGET_VSCREENINFO, &v);
    ioctl(fd, FBIOGET_FSCREENINFO, &f);
    printf("var: %ux%u bpp=%u  fix: smem_len=%u line_len=%u\n",
           v.xres, v.yres, v.bits_per_pixel, f.smem_len, f.line_length);

    size_t len = f.smem_len ? f.smem_len : 768000;
    uint8_t *m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("mmap(%u): %s\n", (unsigned)len, strerror(errno)); return 1; }
    printf("mmap ok: %p len=%u\n", (void *)m, (unsigned)len);

    signal(SIGSEGV, faulted);
    signal(SIGBUS,  faulted);

    // pass 1: READ pokes
    size_t off = 0; int sig;
    if ((sig = sigsetjmp(jb, 1)) == 0) {
        volatile uint8_t x;
        for (off = 0; off < len; off += 4096) x = m[off];
        (void)x;
        printf("READ ok to end (%u)\n", (unsigned)len);
    } else {
        printf("READ fault sig=%d at offset=%u\n", sig, (unsigned)off);
    }

    // pass 2: WRITE pokes (green 0x07E0), fill as we go so panel shows extent
    if ((sig = sigsetjmp(jb, 1)) == 0) {
        for (off = 0; off < len; off += 4096) {
            uint16_t *p = (uint16_t *)(m + off);
            size_t n = (len - off >= 4096) ? 2048 : (len - off) / 2;
            for (size_t i = 0; i < n; i++) p[i] = 0x07E0;
            if ((off & 0xFFFF) == 0) printf("w %u ok\n", (unsigned)off);
        }
        printf("WRITE ok to end (%u) -- whole fb is green?\n", (unsigned)len);
    } else {
        printf("WRITE fault sig=%d at offset=%u (rows=%u of 480)\n",
               sig, (unsigned)off, (unsigned)(off / 1600));
    }

    msync(m, len, MS_SYNC);
    printf("done; sleeping 20 so webcam can see the panel\n");
    sleep(20);
    return 0;
}
