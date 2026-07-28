// colorbars.c -- definitive OSD pixel-format test. Fills the DCC OSD with 5 stacked
// horizontal bars of KNOWN RGB565 values and holds. If the panel shows them in the
// labeled order/colors, the OSD is plain RGB565 red-high. If scrambled/green, it's a
// different format (byte-swapped or YUV) and tells us the real mapping.
//   rows   0.. 95 : RED    0xF800
//   rows  96..191 : GREEN  0x07E0
//   rows 192..287 : BLUE   0x001F
//   rows 288..383 : YELLOW 0xFFE0  (R+G)
//   rows 384..479 : WHITE  0xFFFF
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "rua.h"
#include "dcc.h"
#define CHIP 0
#define ROK 6
#define W 800
#define H 480

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    { extern int verbose_stderr; verbose_stderr = 0; }
    struct RUA *pRUA = 0; struct DCC *pDCC = 0; struct DCCVideoSource *pVS = 0;
    struct DCCOSDProfile pf; RMuint32 sc = 0, luma[2] = {0,0}, ls[2] = {0,0}; RMstatus rv;

    rv = RUACreateInstance(&pRUA, CHIP); printf("RUACreate=%d\n", rv); if (rv != ROK) return 1;
    rv = DCCOpen(pRUA, &pDCC);           printf("DCCOpen=%d\n", rv);   if (rv != ROK) return 1;
    rv = DCCInitMicroCodeEx(pDCC, DCCInitMode_InitDisplay); printf("Init=%d\n", rv); if (rv != ROK) return 2;

    memset(&pf, 0, sizeof pf);
    pf.SamplingMode = EMhwlibSamplingMode_444;
    pf.ColorMode    = EMhwlibColorMode_TrueColor;
    pf.ColorFormat  = EMhwlibColorFormat_16BPP_565;
    pf.Width = W; pf.Height = H;
    pf.ColorSpace = EMhwlibColorSpace_RGB_0_255;
    pf.PixelAspectRatio.X = 1; pf.PixelAspectRatio.Y = 1;

    rv = DCCOpenMultiplePictureOSDVideoSource(pDCC, &pf, 2, &pVS, 0); printf("OpenOSD=%d\n", rv); if (rv != ROK) return 3;
    DCCGetOSDPictureInfo(pVS, 0, 0, &luma[0], &ls[0], 0, 0);
    DCCGetOSDPictureInfo(pVS, 1, 0, &luma[1], &ls[1], 0, 0);

    const uint16_t bar[5] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xFFFF };
    for (int b = 0; b < 2; b++) {
        RUALock(pRUA, luma[b], ls[b]);
        uint16_t *p = (uint16_t *)RUAMap(pRUA, luma[b], ls[b]);
        if (p) {
            for (int y = 0; y < H; y++) {
                uint16_t v = bar[(y * 5) / H];
                for (int x = 0; x < W; x++) p[y * W + x] = v;
            }
        }
        RUAUnLock(pRUA, luma[b], ls[b]);
    }

    DCCGetScalerModuleID(pDCC, DCCRoute_Main, DCCSurface_OSD, 0, &sc); printf("scaler=0x%x\n", sc);
    rv = DCCInsertPictureInMultiplePictureOSDVideoSource(pVS, 0, 0); printf("insert=%d\n", rv);
    rv = DCCSetSurfaceSource(pDCC, sc, pVS); printf("setsurf=%d\n", rv);
    rv = DCCEnableVideoSource(pVS, TRUE);    printf("enable=%d\n", rv);
    printf("BARS: red/green/blue/yellow/white top->bottom. holding\n");
    while (1) sleep(1);
    return 0;
}
