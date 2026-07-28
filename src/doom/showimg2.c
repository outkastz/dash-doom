// showimg2 -- like showimg, but performs DOOM's EXTRA setup steps (selected by a
// letters arg) BEFORE loop-writing a solid color, to find which one corrupts the OSD.
//   v = FBIOGET_VSCREENINFO ioctl (DOOM does this; showimg/fbwrite do not)
//   i = open /dev/input/event0 (evdev)
//   u = open+bind UDP socket :5001
//   m = malloc+touch 8MB (DOOM zone) then 4MB (WAD-ish)
// Usage: showimg2 <colorfile> <secs> <steps>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/fb.h>

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    const char *colorfile = argv[1];
    int secs = atoi(argv[2]);
    const char *steps = argc > 3 ? argv[3] : "";

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("open fb0"); return 1; }
    struct fb_fix_screeninfo f;
    ioctl(fd, FBIOGET_FSCREENINFO, &f);
    size_t len = f.smem_len ? f.smem_len : 768000;

    if (strchr(steps, 'v')) {
        struct fb_var_screeninfo v;
        ioctl(fd, FBIOGET_VSCREENINFO, &v);
        printf("step v: VSCREENINFO %ux%u bpp=%u\n", v.xres, v.yres, v.bits_per_pixel);
    }
    if (strchr(steps, 'i')) {
        int e = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
        printf("step i: open event0 = %d\n", e);
    }
    if (strchr(steps, 'u')) {
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof a);
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(5001);
        bind(s, (struct sockaddr *)&a, sizeof a);
        printf("step u: udp socket = %d\n", s);
    }
    if (strchr(steps, 'm')) {
        void *z = malloc(8 * 1024 * 1024); if (z) memset(z, 1, 8 * 1024 * 1024);
        void *w = malloc(4 * 1024 * 1024); if (w) memset(w, 2, 4 * 1024 * 1024);
        printf("step m: 8MB=%p 4MB=%p\n", z, w);
    }

    unsigned char *m = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == (void *)-1) { perror("mmap"); return 1; }
    unsigned char *img = malloc(len); memset(img, 0, len);
    int ifd = open(colorfile, O_RDONLY);
    size_t got = 0; long n;
    while (got < len && (n = read(ifd, img + got, len - got)) > 0) got += n;
    close(ifd);
    printf("looping %s for %d s, steps=[%s]\n", colorfile, secs, steps);
    for (long i = 0; i < (long)secs * 35; i++) { memcpy(m, img, len); usleep(28000); }
    return 0;
}
