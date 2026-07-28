// osdtest -- clean single run. InitDisplay, open OSD (800x480 16bpp565), write RED to
// BOTH buffers BEFORE setsurf, report every step. Hold.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "rua.h"
#include "dcc.h"
#define CHIP 0
#define ROK 6

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    struct RUA *pRUA = 0; struct DCC *pDCC = 0; struct DCCVideoSource *pVS = 0;
    struct DCCOSDProfile pf; RMuint32 sc = 0, luma[2] = {0,0}, ls[2] = {0,0}; RMstatus rv;

    rv = RUACreateInstance(&pRUA, CHIP); printf("RUACreate=%d\n", rv); if (rv != ROK) return 1;
    rv = DCCOpen(pRUA, &pDCC);           printf("DCCOpen=%d\n", rv);   if (rv != ROK) return 1;
    rv = DCCInitMicroCodeEx(pDCC, DCCInitMode_InitDisplay); printf("Init(InitDisplay)=%d\n", rv); if (rv != ROK) return 2;

    memset(&pf, 0, sizeof pf);
    pf.SamplingMode = EMhwlibSamplingMode_444;
    pf.ColorMode    = EMhwlibColorMode_TrueColor;
    pf.ColorFormat  = EMhwlibColorFormat_16BPP_565;
    pf.Width = 800; pf.Height = 480;
    pf.ColorSpace = EMhwlibColorSpace_RGB_0_255;
    pf.PixelAspectRatio.X = 1; pf.PixelAspectRatio.Y = 1;

    rv = DCCOpenMultiplePictureOSDVideoSource(pDCC, &pf, 2, &pVS, 0); printf("OpenOSD=%d\n", rv); if (rv != ROK) return 3;
    DCCGetOSDPictureInfo(pVS, 0, 0, &luma[0], &ls[0], 0, 0);
    DCCGetOSDPictureInfo(pVS, 1, 0, &luma[1], &ls[1], 0, 0);
    printf("luma0=0x%x luma1=0x%x\n", luma[0], luma[1]);

    for (int b = 0; b < 2; b++) {
        RUALock(pRUA, luma[b], ls[b]);
        uint16_t *p = (uint16_t *)RUAMap(pRUA, luma[b], ls[b]);
        if (p) { for (unsigned i = 0; i < 800u*480u; i++) p[i] = 0xF800; }  // RED
        printf("buf%d map=%p\n", b, (void*)p);
        RUAUnLock(pRUA, luma[b], ls[b]);
    }

    DCCGetScalerModuleID(pDCC, DCCRoute_Main, DCCSurface_OSD, 0, &sc); printf("scaler=0x%x\n", sc);
    rv = DCCInsertPictureInMultiplePictureOSDVideoSource(pVS, 0, 0); printf("insert=%d\n", rv);
    rv = DCCSetSurfaceSource(pDCC, sc, pVS); printf("setsurf=%d\n", rv);
    rv = DCCEnableVideoSource(pVS, TRUE);    printf("enable=%d\n", rv);
    printf("DONE holding\n");
    while (1) sleep(1);
    return 0;
}
