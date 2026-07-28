// showimg -- mmap /dev/fb0 and memcpy a raw RGB565 image into it at N fps for S seconds.
// Mimics DOOM's DG_DrawFrame exactly (loop memcpy full frame), but with a static image,
// to isolate whether continuous mmap-writing is what tears down the OSD scanout.
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
    const char *path = argc > 1 ? argv[1] : "/tmp/frame.bin";
    int secs = argc > 2 ? atoi(argv[2]) : 30;
    int fps  = argc > 3 ? atoi(argv[3]) : 35;

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("open fb0"); return 1; }
    struct fb_fix_screeninfo f;
    ioctl(fd, FBIOGET_FSCREENINFO, &f);
    size_t len = f.smem_len ? f.smem_len : 768000;
    unsigned char *m = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == (void *)-1) { perror("mmap"); return 1; }
    printf("fb mmap ok len=%u\n", (unsigned)len);

    unsigned char *img = malloc(len);
    memset(img, 0, len);
    int ifd = open(path, O_RDONLY);
    if (ifd < 0) { perror("open img"); return 1; }
    size_t got = 0; long n;
    while (got < len && (n = read(ifd, img + got, len - got)) > 0) got += n;
    close(ifd);
    if (fps <= 0) {
        // ONCE mode: single write, then hold (no repeated writes)
        memcpy(m, img, len);
        printf("wrote ONCE; holding %d s\n", secs);
        sleep(secs);
        return 0;
    }
    printf("img %u bytes; looping %d fps for %d s\n", (unsigned)got, fps, secs);
    long frames = (long)secs * fps;
    for (long i = 0; i < frames; i++) { memcpy(m, img, len); usleep(1000000 / fps); }
    printf("done\n");
    return 0;
}
