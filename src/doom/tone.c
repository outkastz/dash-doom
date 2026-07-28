// tone.c -- OSS /dev/dsp probe for the Dash. Opens /dev/dsp, sets S16_LE/mono,
// tries several sample rates, writes a ~1s 440Hz sine at each. Reports every step.
// Static musl build (no libs). Proves whether the em8xxx OSS audio path makes sound.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <sys/ioctl.h>

// OSS ioctls (from linux soundcard.h) -- hardcoded to avoid header hunts
#define OSS_SETFMT   0xC0045005
#define OSS_CHANNELS 0xC0045006
#define OSS_SPEED    0xC0045002
#define AFMT_S16_LE  0x00000010

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    int fd = open("/dev/dsp", O_WRONLY);
    if (fd < 0) { printf("open /dev/dsp FAILED errno=%d (%s)\n", errno, strerror(errno)); return 1; }
    printf("open /dev/dsp OK fd=%d\n", fd);

    int fmt = AFMT_S16_LE;
    printf("SETFMT S16_LE rc=%d fmt=0x%x\n", ioctl(fd, OSS_SETFMT, &fmt), fmt);
    int ch = 1;
    printf("CHANNELS 1 rc=%d ch=%d\n", ioctl(fd, OSS_CHANNELS, &ch), ch);

    int rates[3] = {44100, 22050, 11025};
    for (int r = 0; r < 3; r++) {
        int rate = rates[r];
        int rc = ioctl(fd, OSS_SPEED, &rate);
        printf("SPEED %d rc=%d -> got %d ; writing 1s sine...\n", rates[r], rc, rate);
        int n = rate;                 // 1 second
        short *buf = malloc(n * 2);
        for (int i = 0; i < n; i++)
            buf[i] = (short)(9000.0 * sin(2.0 * 3.14159265 * 440.0 * i / rate));
        int w = write(fd, buf, n * 2);
        printf("  wrote %d bytes\n", w);
        free(buf);
    }
    // let the DAC drain
    sleep(1);
    close(fd);
    printf("DONE\n");
    return 0;
}
