// fliptest -- alternate RED/GREEN every 600ms using different fb commit strategies,
// to find what makes the em8xxx OSD actually UPDATE (not just show the first write).
//   mode 0: open+mmap+write+munmap+close every frame (like fbwrite exiting)
//   mode 1: keep fd, mmap+write+munmap every frame
//   mode 2: keep one mmap, just memcpy (control -- expected to stick on frame 0)
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#define LEN 768000
static unsigned char rbuf[LEN], gbuf[LEN];

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    int iters = argc > 1 ? atoi(argv[1]) : 30;
    int mode  = argc > 2 ? atoi(argv[2]) : 0;
    // rbuf = WHITE, gbuf = BLACK -- unambiguous blink even under camera overexposure
    for (size_t i = 0; i < LEN; i += 2) { rbuf[i]=0xFF; rbuf[i+1]=0xFF; gbuf[i]=0x00; gbuf[i+1]=0x00; }

    int fd = -1; unsigned char *m = 0;
    struct fb_var_screeninfo v;
    if (mode == 2 || mode == 3) { fd = open("/dev/fb0", O_RDWR); m = mmap(0, LEN, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0); ioctl(fd, FBIOGET_VSCREENINFO, &v); }
    if (mode == 1) fd = open("/dev/fb0", O_RDWR);

    for (int i = 0; i < iters; i++) {
        unsigned char *buf = (i & 1) ? gbuf : rbuf;
        if (mode == 0) {
            int f = open("/dev/fb0", O_RDWR);
            unsigned char *mm = mmap(0, LEN, PROT_READ|PROT_WRITE, MAP_SHARED, f, 0);
            if (mm != (void*)-1) { memcpy(mm, buf, LEN); munmap(mm, LEN); }
            close(f);
        } else if (mode == 1) {
            unsigned char *mm = mmap(0, LEN, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
            if (mm != (void*)-1) { memcpy(mm, buf, LEN); munmap(mm, LEN); }
        } else if (mode == 3) {
            // keep mmap, write, then FBIOPAN_DISPLAY (the OSD re-composite trigger)
            memcpy(m, buf, LEN);
            v.yoffset = 0; v.activate = FB_ACTIVATE_NOW;
            int r = ioctl(fd, FBIOPAN_DISPLAY, &v);
            if (i == 0) printf("PAN ret=%d errno=%d (%s) yres_virtual=%u\n", r, errno, strerror(errno), v.yres_virtual);
        } else {
            memcpy(m, buf, LEN);
        }
        printf("frame %d %s\n", i, (i & 1) ? "GREEN" : "RED");
        usleep(600000);
    }
    return 0;
}
