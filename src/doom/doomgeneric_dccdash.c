// doomgeneric_dccdash.c
// Sigma RM/DCC OSD backend for the Sony Dash HID-C10 (SMP8654, mipsel).
//
// This is the WORKING display path (session 7 breakthrough). The em8xxxfb / fb0
// framebuffer path is dead (a passive memory window; the OSD only re-composites
// under the RM/DCC userspace stack). Here we drive the compositor directly:
//
//   RUACreateInstance -> DCCOpen -> DCCInitMicroCodeEx(InitDisplay)
//     -> DCCOpenMultiplePictureOSDVideoSource(2 buffers, 800x480 16bpp_565)
//     -> DCCGetScalerModuleID(Main,OSD) -> DCCSetSurfaceSource -> DCCEnableVideoSource
//   per frame: RUALock+RUAMap the back OSD buffer, scale DOOM's 320x200 RGB565
//     frame up to 800x480 into it, RUAUnLock, DCCInsertPicture(idx) to flip.
//
// InitDisplay (mode 0) is the key: it makes DCC OWN the display chain so the
// surface-apply event fires (LeaveDisplay leaves the recovery's non-DCC OSD in
// place and DCCSetSurfaceSource blocks forever). Confirmed on-panel: 0xF800 -> RED.
// DCC 16BPP_565 is NATIVE little-endian (0xF800 = red) -- NO byte swap (unlike fb0).
//
// Input (all optional / non-fatal): any /dev/input/event* (evdev keyboard/gamepad/
// touchscreen) + UDP :5001 (2-byte [pressed, doomKeyCode] packets from the PC bridge).

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"
#include "i_system.h"

// doomkeys.h vs linux/input.h name collisions -- same workaround as linuxvt/fbdash.
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
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/input.h>
#include <signal.h>
#include <execinfo.h>
#include <ucontext.h>

#include "rua.h"
#include "dcc.h"

#define CHIP        0
#define ROK         6          // RM_OK (NOT 0)
#define OSD_W       800
#define OSD_H       480

#define KEYQUEUE_SIZE   16
#define MAX_INPUT_DEVS  16

// timing
static struct timeval startTime;

// ---- DCC OSD output ----
static struct RUA          *pRUA = NULL;
static struct DCC          *pDCC = NULL;
static struct DCCVideoSource *pVS = NULL;
static RMuint32             luma[2] = {0, 0};   // phys addr of each OSD picture buffer
static RMuint32             lsz[2]  = {0, 0};   // bytes of each buffer
static uint16_t            *osdmap[2] = {0, 0}; // CPU mapping of each buffer (mapped ONCE)
static RMuint32             scaler  = 0;
static int                  curIdx  = 0;        // back buffer we render into next

// full-screen scaler: DOOM-native 320x200 -> 800x480 (fills the panel, no borders)
static int                 *xMap = NULL;        // [OSD_W]  -> source column
static int                 *yMap = NULL;        // [OSD_H]  -> source row

// The em8xxx OSD composites in YUV even though we request RGB565 (confirmed via the
// SDK smptest sample + color-bar test on-panel). The 16BPP_565 fields are actually
// [V:5 | Y:6 | U:5]. DOOM renders RGB565, so we convert each pixel through this LUT.
static uint16_t             rgb2yuv[65536];
static void build_rgb2yuv(void) {
    for (int p = 0; p < 65536; p++) {
        int r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
        int r = (r5 * 255) / 31, g = (g6 * 255) / 63, b = (b5 * 255) / 31;
        int y = (299 * r + 587 * g + 114 * b) / 1000;
        int u = ((b - y) * 565) / 1000 + 128;
        int v = ((r - y) * 713) / 1000 + 128;
        if (y < 0) y = 0; else if (y > 255) y = 255;
        if (u < 0) u = 0; else if (u > 255) u = 255;
        if (v < 0) v = 0; else if (v > 255) v = 255;
        rgb2yuv[p] = (uint16_t)(((v >> 3) << 11) | ((y >> 2) << 5) | (u >> 3));
    }
}

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

    if (numInputFds > 0 && poll(pollfds, numInputFds, 0) > 0) {
        for (int i = 0; i < numInputFds; i++) {
            if (!(pollfds[i].revents & POLLIN)) continue;
            while (read(inputFds[i], &ev, sizeof ev) == (int)sizeof ev) {
                if (ev.type == EV_KEY)
                    addEvdevKey(ev.value, ev.code);
            }
        }
    }

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
        printf("dccdash: input <- %s\n", path);
    }
}

static void openUdp(void) {
    udpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udpFd < 0) { udpFd = -1; return; }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(5001);
    if (bind(udpFd, (struct sockaddr *)&a, sizeof a) < 0) {
        close(udpFd); udpFd = -1; return;
    }
    printf("dccdash: input <- UDP :5001\n");
}

// ---- DCC OSD setup (the proven session-7 recipe) ----
static void dcc_init_display(void) {
    { extern int verbose_stderr; verbose_stderr = 0; }   // silence RM debug spew
    RMstatus rv;
    struct DCCOSDProfile pf;

    rv = RUACreateInstance(&pRUA, CHIP);
    if (rv != ROK) I_Error("dccdash: RUACreateInstance=%d", rv);
    rv = DCCOpen(pRUA, &pDCC);
    if (rv != ROK) I_Error("dccdash: DCCOpen=%d", rv);
    rv = DCCInitMicroCodeEx(pDCC, DCCInitMode_InitDisplay);   // mode 0 -- DCC owns display
    if (rv != ROK) I_Error("dccdash: DCCInitMicroCodeEx(InitDisplay)=%d", rv);

    memset(&pf, 0, sizeof pf);
    pf.SamplingMode = EMhwlibSamplingMode_444;
    pf.ColorMode    = EMhwlibColorMode_TrueColor;
    pf.ColorFormat  = EMhwlibColorFormat_16BPP_565;   // native RGB565, no conversion
    pf.Width  = OSD_W;
    pf.Height = OSD_H;
    pf.ColorSpace = EMhwlibColorSpace_RGB_0_255;
    pf.PixelAspectRatio.X = 1;
    pf.PixelAspectRatio.Y = 1;

    rv = DCCOpenMultiplePictureOSDVideoSource(pDCC, &pf, 2, &pVS, 0);
    if (rv != ROK) I_Error("dccdash: DCCOpenMultiplePictureOSDVideoSource=%d", rv);
    DCCGetOSDPictureInfo(pVS, 0, 0, &luma[0], &lsz[0], 0, 0);
    DCCGetOSDPictureInfo(pVS, 1, 0, &luma[1], &lsz[1], 0, 0);
    printf("dccdash: luma0=0x%x luma1=0x%x sz=%u\n", luma[0], luma[1], lsz[0]);

    // Map both OSD buffers ONCE and hold the mappings for the program lifetime.
    // (Mapping per-frame leaks gbus regions -> RUAMap eventually fails -> the write
    //  wraps around a -1 pointer -> SIGSEGV. osdtest proved a held mapping persists.)
    for (int b = 0; b < 2; b++) {
        RUALock(pRUA, luma[b], lsz[b]);
        osdmap[b] = (uint16_t *)RUAMap(pRUA, luma[b], lsz[b]);
        if (osdmap[b] == NULL || osdmap[b] == (uint16_t *)-1)
            I_Error("dccdash: RUAMap buf%d failed (%p)", b, (void *)osdmap[b]);
        memset(osdmap[b], 0, (size_t)OSD_W * OSD_H * 2);
    }

    DCCGetScalerModuleID(pDCC, DCCRoute_Main, DCCSurface_OSD, 0, &scaler);
    rv = DCCInsertPictureInMultiplePictureOSDVideoSource(pVS, 0, 0);
    if (rv != ROK) I_Error("dccdash: InsertPicture=%d", rv);
    rv = DCCSetSurfaceSource(pDCC, scaler, pVS);
    if (rv != ROK) I_Error("dccdash: DCCSetSurfaceSource=%d", rv);
    rv = DCCEnableVideoSource(pVS, TRUE);
    if (rv != ROK) I_Error("dccdash: DCCEnableVideoSource=%d", rv);
    printf("dccdash: OSD display up (scaler=0x%x)\n", scaler);
}

void DG_Init(void) {
    gettimeofday(&startTime, NULL);

    dcc_init_display();
    build_rgb2yuv();

    // build nearest-neighbor scale maps: 320x200 -> 800x480 (fills the whole panel)
    xMap = malloc(sizeof(int) * OSD_W);
    yMap = malloc(sizeof(int) * OSD_H);
    if (!xMap || !yMap) I_Error("dccdash: OOM scaler maps");
    for (int x = 0; x < OSD_W; x++)
        xMap[x] = (int)((long)x * DOOMGENERIC_RESX / OSD_W);
    for (int y = 0; y < OSD_H; y++)
        yMap[y] = (int)((long)y * DOOMGENERIC_RESY / OSD_H);

    memset(DG_ScreenBuffer, 0,
           (size_t)DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t));

    checkInputDevs();
    openUdp();
}

void DG_DrawFrame(void) {
    checkKeys();

    const uint16_t *src = (const uint16_t *)DG_ScreenBuffer;   // 320x200 RGB565
    int i = curIdx;
    uint16_t *dst = osdmap[i];                                 // mapped once at init

    for (int dy = 0; dy < OSD_H; dy++) {
        const uint16_t *srow = src + (unsigned)yMap[dy] * DOOMGENERIC_RESX;
        uint16_t *drow = dst + (size_t)dy * OSD_W;
        const int *xm = xMap;
        for (int dx = 0; dx < OSD_W; dx++)
            drow[dx] = rgb2yuv[srow[xm[dx]]];   // RGB565 -> YUV565 (OSD is YUV)
    }

    DCCInsertPictureInMultiplePictureOSDVideoSource(pVS, i, 0);   // flip: show buffer i
    curIdx ^= 1;

    // FPS report every ~5s (to /tmp/doom.log)
    {
        static uint32_t t0 = 0, n = 0;
        uint32_t now = DG_GetTicksMs();
        if (t0 == 0) t0 = now;
        if (++n >= 1 && now - t0 >= 5000) {
            printf("FPS: %u frames / %u ms = %u.%u fps\n", n, now - t0,
                   (n * 1000) / (now - t0), ((n * 10000) / (now - t0)) % 10);
            n = 0; t0 = now;
        }
    }
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

static void crash_handler(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = (ucontext_t *)uc_;
    unsigned long pc = (unsigned long)uc->uc_mcontext.pc;
    unsigned long ra = (unsigned long)uc->uc_mcontext.gregs[31];
    fprintf(stderr, "\n*** SIGNAL %d fault_addr=%p PC=0x%lx RA=0x%lx ***\n",
            sig, si->si_addr, pc, ra);
    fflush(stderr);
    _exit(139);
}

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    setvbuf(stderr, 0, _IONBF, 0);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGABRT, &sa, 0);
    sigaction(SIGBUS,  &sa, 0);
    doomgeneric_Create(argc, argv);
    for (;;)
        doomgeneric_Tick();
    return 0;
}
