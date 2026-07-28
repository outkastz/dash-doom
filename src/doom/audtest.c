// audtest.c -- probe the Sigma MRUA audio engine (no /dev/dsp; direct RM/DCC path).
// Brings up the media pipeline (InitDisplay), opens an STC + audio decoder source
// (plaympeg recipe), and reports RM status at every step. Goal: confirm the audio
// engine is reachable so we can feed raw PCM next. RM_OK=6, RM_PENDING=7.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "rua.h"
#include "dcc.h"
#define CHIP 0
#define ROK 6

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    { extern int verbose_stderr; verbose_stderr = 0; }
    int skip_init = (argc > 1 && strcmp(argv[1], "noinit") == 0);
    struct RUA *pRUA = 0; struct DCC *pDCC = 0;
    struct DCCSTCSource *pSTC = 0; struct DCCAudioSource *pAud = 0;
    struct DCCStcProfile stc; struct DCCAudioProfile ap;
    RMuint32 adec = 0, aeng = 0, atmr = 0;
    RMstatus rv;

    rv = RUACreateInstance(&pRUA, CHIP); printf("RUACreate=%d\n", rv); if (rv != ROK) return 1;
    rv = DCCOpen(pRUA, &pDCC);           printf("DCCOpen=%d\n", rv);   if (rv != ROK) return 1;
    if (!skip_init) {
        rv = DCCInitMicroCodeEx(pDCC, DCCInitMode_InitDisplay); printf("Init=%d\n", rv); if (rv != ROK) return 2;
    } else {
        printf("Init=SKIPPED (using DOOM's live pipeline)\n");
    }

    memset(&stc, 0, sizeof stc);
    stc.STCID = 0;
    stc.master = Master_STC;
    stc.stc_timer_id = 0;
    stc.stc_time_resolution = 90000;
    stc.video_timer_id = 1;
    stc.video_time_resolution = 90000;
    stc.audio_timer_id = 2;
    stc.audio_time_resolution = 90000;
    rv = DCCSTCOpen(pDCC, &stc, &pSTC); printf("STCOpen=%d\n", rv);

    memset(&ap, 0, sizeof ap);
    ap.BitstreamFIFOSize = 512 * 1024;
    ap.XferFIFOCount = 0;
    ap.DemuxProgramID = 0;
    ap.AudioEngineID = 0;
    ap.AudioDecoderID = 0;
    ap.STCID = 0;
    rv = DCCOpenAudioDecoderSource(pDCC, &ap, &pAud); printf("OpenAudioDecoder=%d\n", rv);
    if (rv == ROK) {
        rv = DCCGetAudioDecoderSourceInfo(pAud, &adec, &aeng, &atmr);
        printf("AudioInfo rv=%d decoder=0x%x engine=0x%x timer=0x%x\n", rv, adec, aeng, atmr);
        rv = DCCSetAudioSourceVolume(pAud, 0x10000000); printf("SetVolume=%d\n", rv);
    }
    printf("DONE (audio engine %s)\n", (pAud ? "REACHABLE" : "not opened"));
    return 0;
}
