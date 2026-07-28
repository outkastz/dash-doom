// doomgeneric_fbdash.c
// Framebuffer + input backend for the Sony Dash HID-C10 (Sigma SMP8654, mipsel).
//
// Output: /dev/fb0, 800x480 RGB565.  Built with -gfxmode rgb565 so i_video fills
//   DG_ScreenBuffer as a ready-to-blit 800x480 RGB565 frame (game auto-scaled 2x =
//   640x400, centered/letterboxed).  We push it with write() (lseek+write) because
//   that is the exact path our controller proved works on em8xxxfb ("cat > /dev/fb0");
//   mmap can miss on this double-buffered display.
//
// Input (all optional / non-fatal, so it runs even before a keyboard is attached):
//   - any /dev/input/event* : USB keyboard/gamepad OR the touchscreen, via evdev.
//   - UDP :5001             : 2-byte packets [pressed, doomKeyCode] from the PC bridge.
//
// Based on doomgeneric_linuxvt.c (Techflash), adapted for the Dash.

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"
#include "i_system.h"

// doomkeys.h vs linux/input.h name collisions -- same workaround as linuxvt.
#undef KEY_TAB
#undef KEY_ENTER
#undef KEY_BACKSPACE
#undef KEY_MINUS
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#define DOOM_KEY_TAB       9
#define DOOM_KEY_ENTER     13
#define DOOM_KEY_MINUS     0x2d
#define DOOM_KEY_BACKSPACE 0x7f
#define DOOM_KEY_F1  (0x80+0x3b)
#define DOOM_KEY_F2  (0x80+0x3c)
#define DOOM_KEY_F3  (0x80+0x3d)
#define DOOM_KEY_F4  (0x80+0x3e)
#define DOOM_KEY_F5  (0x80+0x3f)
#define DOOM_KEY_F6  (0x80+0x40)
#define DOOM_KEY_F7  (0x80+0x41)
#define DOOM_KEY_F8  (0x80+0x42)
#define DOOM_KEY_F9  (0x80+0x43)
#define DOOM_KEY_F10 (0x80+0x44)
#define DOOM_KEY_F11 (0x80+0x57)
#define DOOM_KEY_F12 (0x80+0x58)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/input.h>
#include <linux/fb.h>

#define KEYQUEUE_SIZE   16
#define MAX_INPUT_DEVS  16

// timing
static struct timeval startTime;

// framebuffer
static int            fbFd = -1;
static unsigned int   fbWidth, fbHeight, fbStride, fbBpp;
static unsigned char *fbmem = NULL;     // mmap'd OSD video memory (1 or 2 buffers)
static size_t         fbmapsz = 0;
static struct fb_var_screeninfo fbvar;  // for FBIOPAN_DISPLAY (the OSD re-composite trigger)
static size_t         fbBufBytes = 0;   // bytes in ONE buffer (stride*yres)
static int            fbDouble = 0;     // 1 if double-buffered (smem_len>=2 buffers & yres_virtual>=2*yres)
static int            fbBack = 1;       // index of the buffer we render into next

// full-screen scaler: DOOM-native DOOMGENERIC_RESX x RESY (320x200) -> fbWidth x fbHeight
static uint16_t    *scaleBuf = NULL;    // fbWidth * fbHeight RGB565 scratch
static int         *xMap = NULL;        // [fbWidth]  -> source column
static int         *yMap = NULL;        // [fbHeight] -> source row

// input
static int             numInputFds = 0;
static int             inputFds[MAX_INPUT_DEVS];
static struct pollfd   pollfds[MAX_INPUT_DEVS];
static int             udpFd = -1;
static bool            shiftPressed = false;

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int   s_KeyQueueWriteIndex = 0;
static unsigned int   s_KeyQueueReadIndex  = 0;

static unsigned char convertToDoomKey(unsigned int key) {
    switch (key) {
        case KEY_ENTER:     return DOOM_KEY_ENTER;
        case KEY_ESC:       return KEY_ESCAPE;
        case KEY_LEFT:      return KEY_LEFTARROW;
        case KEY_RIGHT:     return KEY_RIGHTARROW;
        case KEY_UP:        return KEY_UPARROW;
        case KEY_DOWN:      return KEY_DOWNARROW;
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL: return KEY_FIRE;
        case KEY_SPACE:     return KEY_USE;
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:return KEY_RSHIFT;
        case KEY_LEFTALT:
        case KEY_RIGHTALT:  return KEY_LALT;
        case KEY_TAB:       return DOOM_KEY_TAB;
        case KEY_F1:        return DOOM_KEY_F1;
        case KEY_F2:        return DOOM_KEY_F2;
        case KEY_F3:        return DOOM_KEY_F3;
        case KEY_F4:        return DOOM_KEY_F4;
        case KEY_F5:        return DOOM_KEY_F5;
        case KEY_F6:        return DOOM_KEY_F6;
        case KEY_F7:        return DOOM_KEY_F7;
        case KEY_F8:        return DOOM_KEY_F8;
        case KEY_F9:        return DOOM_KEY_F9;
        case KEY_F10:       return DOOM_KEY_F10;
        case KEY_F11:       return DOOM_KEY_F11;
        case KEY_EQUAL:     return KEY_EQUALS;
        case KEY_MINUS:     return DOOM_KEY_MINUS;
        case KEY_BACKSPACE: return DOOM_KEY_BACKSPACE;
        // a handful of letters DOOM uses in menus / cheats
        case KEY_Y:         return 'y';
        case KEY_N:         return 'n';
        default:            return 0;
    }
}

static void pushDoomKey(int pressed, unsigned char doomKey) {
    if (doomKey == 0) return;
    if (pressed != 0 && pressed != 1) return;  // ignore autorepeat(2)/bogus
    unsigned short keyData = (unsigned short)((pressed << 8) | doomKey);
    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

static void addEvdevKey(int pressed, unsigned int code) {
    if ((code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) &&
        (pressed == 0 || pressed == 1))
        shiftPressed = pressed;
    pushDoomKey(pressed, convertToDoomKey(code));
}

static void checkKeys(void) {
    struct input_event ev;

    // local evdev devices (keyboard / gamepad / touchscreen)
    if (numInputFds > 0 && poll(pollfds, numInputFds, 0) > 0) {
        for (int i = 0; i < numInputFds; i++) {
            if (!(pollfds[i].revents & POLLIN)) continue;
            while (read(inputFds[i], &ev, sizeof ev) == (int)sizeof ev) {
                if (ev.type == EV_KEY)
                    addEvdevKey(ev.value, ev.code);
            }
        }
    }

    // UDP remote input: 2-byte packets [pressed, doomKeyCode] (already DOOM codes)
    if (udpFd >= 0) {
        unsigned char pkt[16];
        int n;
        while ((n = recv(udpFd, pkt, sizeof pkt, 0)) >= 2) {
            pushDoomKey(pkt[0] & 1, pkt[1]);
        }
    }
}

static void checkInputDevs(void) {
    char path[32];
    for (int i = 0; i < 32 && numInputFds < MAX_INPUT_DEVS; i++) {
        sprintf(path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        pollfds[numInputFds].fd = fd;
        pollfds[numInputFds].events = POLLIN;
        inputFds[numInputFds++] = fd;
        printf("fbdash: input <- %s\n", path);
    }
}

void DG_Init(void) {
    struct fb_fix_screeninfo finfo;

    fbFd = open("/dev/fb0", O_RDWR);
    if (fbFd < 0)
        I_Error("fbdash: open /dev/fb0: %s", strerror(errno));

    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &fbvar) == 0) {
        fbWidth  = fbvar.xres;
        fbHeight = fbvar.yres;
        fbBpp    = fbvar.bits_per_pixel;
    } else {
        fbWidth = DOOMGENERIC_RESX; fbHeight = DOOMGENERIC_RESY; fbBpp = 16;
    }
    if (ioctl(fbFd, FBIOGET_FSCREENINFO, &finfo) == 0)
        fbStride = finfo.line_length;
    else
        fbStride = fbWidth * (fbBpp / 8);

    // mmap the OSD video memory. em8xxxfb re-composites ONLY on FBIOPAN_DISPLAY (which
    // writes the OSD scan-address+trigger over gbus -- confirmed by disassembling the .ko).
    // If the OSD is set up double-buffered (smem_len >= 2 frames AND yres_virtual >= 2*yres),
    // we render into the OFF-screen buffer and pan to it each frame -> tear-free animation.
    // Single-buffer falls back to same-address pan (a no-op re-latch; static only).
    fbBufBytes = (size_t)fbStride * fbHeight;
    // if the OSD has memory for 2 buffers but yres_virtual wasn't doubled, enable it
    // (check_var permits yres_virtual > yres -- confirmed by disassembling em8xxxfb.ko)
    {
        struct fb_fix_screeninfo f3;
        if (ioctl(fbFd, FBIOGET_FSCREENINFO, &f3) == 0 &&
            f3.smem_len >= 2 * fbBufBytes && fbvar.yres_virtual < 2 * fbvar.yres) {
            fbvar.yres_virtual = 2 * fbvar.yres; fbvar.xres_virtual = fbvar.xres;
            fbvar.xoffset = 0; fbvar.yoffset = 0; fbvar.activate = FB_ACTIVATE_NOW;
            ioctl(fbFd, FBIOPUT_VSCREENINFO, &fbvar);
            ioctl(fbFd, FBIOGET_VSCREENINFO, &fbvar);
        }
    }
    {
        struct fb_fix_screeninfo f2;
        size_t want = fbBufBytes;
        fbmapsz = (ioctl(fbFd, FBIOGET_FSCREENINFO, &f2) == 0 && f2.smem_len > 0)
                    ? f2.smem_len : want;
        if (fbmapsz > 2 * want) fbmapsz = 2 * want;   // sanity cap at 2 buffers
        if (fbmapsz < want)     fbmapsz = want;
        fbmem = mmap(NULL, fbmapsz, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0);
        if (fbmem == MAP_FAILED) { fbmem = NULL; }
    }
    fbDouble = (fbmem && fbmapsz >= 2 * fbBufBytes && fbvar.yres_virtual >= 2 * fbvar.yres);
    fbBack   = fbDouble ? 1 : 0;

    printf("fbdash: /dev/fb0 %ux%u %ubpp stride=%u yvirt=%u mmap=%p mapsz=%u double=%d (doom %dx%d rgb565)\n",
           fbWidth, fbHeight, fbBpp, fbStride, fbvar.yres_virtual, (void *)fbmem,
           (unsigned)fbmapsz, fbDouble, DOOMGENERIC_RESX, DOOMGENERIC_RESY);

    // build nearest-neighbor scale maps: full 320x200 -> fbWidth x fbHeight (fill panel)
    xMap     = malloc(sizeof(int) * fbWidth);
    yMap     = malloc(sizeof(int) * fbHeight);
    scaleBuf = malloc((size_t)fbWidth * fbHeight * 2);
    if (!xMap || !yMap || !scaleBuf)
        I_Error("fbdash: OOM allocating scaler for %ux%u", fbWidth, fbHeight);
    for (unsigned x = 0; x < fbWidth; x++)
        xMap[x] = (int)((unsigned long)x * DOOMGENERIC_RESX / fbWidth);
    for (unsigned y = 0; y < fbHeight; y++)
        yMap[y] = (int)((unsigned long)y * DOOMGENERIC_RESY / fbHeight);

    memset(DG_ScreenBuffer, 0,
           (size_t)DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t));

    // local input (optional)
    checkInputDevs();

    // UDP remote input (optional)
    udpFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpFd >= 0) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port        = htons(5001);
        if (bind(udpFd, (struct sockaddr *)&a, sizeof a) < 0) {
            close(udpFd); udpFd = -1;
        } else {
            fcntl(udpFd, F_SETFL, O_NONBLOCK);
        }
    }
    printf("fbdash: %d local input dev(s), udp fd=%d (:5001)\n", numInputFds, udpFd);

    gettimeofday(&startTime, NULL);
}

void DG_DrawFrame(void) {
    const uint16_t *src = (const uint16_t *)DG_ScreenBuffer;   // 320x200 RGB565

    // nearest-neighbor upscale to fill the whole panel.
    // Byte-swap each pixel: the em8xxx OSD scans big-endian RGB565 (proven on-device:
    // little-endian green 0x07E0 displayed as pink 0xE007).
    for (unsigned dy = 0; dy < fbHeight; dy++) {
        const uint16_t *srow = src + (unsigned)yMap[dy] * DOOMGENERIC_RESX;
        uint16_t *drow = scaleBuf + (size_t)dy * fbWidth;
        for (unsigned dx = 0; dx < fbWidth; dx++) {
            uint16_t v = srow[xMap[dx]];
            drow[dx] = (uint16_t)((v >> 8) | (v << 8));
        }
    }

    const unsigned rowbytes = fbWidth * 2;   // RGB565
    if (fbmem) {
        // render into the back buffer (offset by one frame when double-buffered)
        unsigned char *dst = fbmem + (size_t)fbBack * fbBufBytes;
        if (fbStride == rowbytes) {
            memcpy(dst, scaleBuf, fbBufBytes);
        } else {
            for (unsigned dy = 0; dy < fbHeight; dy++)
                memcpy(dst + (size_t)dy * fbStride, scaleBuf + (size_t)dy * fbWidth, rowbytes);
        }
        // FLIP: point the OSD at the just-drawn buffer + trigger a re-composite.
        // em8xxxfb re-scans ONLY on FBIOPAN_DISPLAY (gbus addr+commit writes -- confirmed
        // by disassembling em8xxxfb.ko). Double-buffered: yoffset alternates 0<->yres so
        // the scan ADDRESS changes each frame (single-buffer same-address pan is a no-op).
        fbvar.yoffset  = (unsigned)fbBack * fbHeight;
        fbvar.xoffset  = 0;
        fbvar.activate = FB_ACTIVATE_NOW;
        ioctl(fbFd, FBIOPAN_DISPLAY, &fbvar);
        if (fbDouble) fbBack ^= 1;   // swap; single-buffer keeps writing buffer 0
    } else {
        // fallback: write() path (does not display on em8xxxfb, but keeps parity)
        lseek(fbFd, 0, SEEK_SET);
        if (fbStride == rowbytes) {
            unsigned total = rowbytes * fbHeight, off = 0;
            unsigned char *p = (unsigned char *)scaleBuf;
            while (off < total) { int w = write(fbFd, p + off, total - off); if (w <= 0) break; off += (unsigned)w; }
        }
    }

    checkKeys();
}

void DG_SleepMs(uint32_t ms) {
    usleep(ms * 1000);
}

uint32_t DG_GetTicksMs(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint32_t)((now.tv_sec - startTime.tv_sec) * 1000
                    + (now.tv_usec - startTime.tv_usec) / 1000);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    checkKeys();
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
        return 0;
    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;
    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;
    return 1;
}

void DG_SetWindowTitle(const char *title) {
    (void)title;
}

int main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    for (;;)
        doomgeneric_Tick();
    return 0;
}
