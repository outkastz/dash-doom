// audtone.c -- play a 440Hz tone through the Sigma MRUA audio engine (raw PCM).
// Opens audio alongside DOOM (noinit), sets a PCM format, opens a SEND pool on the
// audio decoder, plays, and feeds 44100Hz/16-bit/stereo sine. Reports every status.
// Iterate the format (CDA fixed 44100/16/stereo vs Pcmx with params) until sound.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include "rua.h"
#include "dcc.h"
#define CHIP 0
#define ROK 6
#define PEND 7

// Dash libdcc exports (not in jur headers)
extern RMstatus DCCSetAudioPcmCdaFormat(struct DCCAudioSource *, void *);
extern RMstatus DCCSetAudioPcmxFormat(struct DCCAudioSource *, void *);

// libaudiooutports: the firmware's own audio-OUTPUT config (what set_outports/em8xxxalsa use)
extern void OutportsAudioInitOptions(void *opts);
extern int  OutportsAudioApplyOptions(void *ctx, void *opts);
extern RMstatus RUAExchangeProperty(struct RUA *, RMuint32, RMuint32, void *, RMuint32, void *, RMuint32);
// wrappers matching ApplyOptions' expected signatures; return RM_OK so it doesn't spin on PENDING
static RMstatus ao_set(struct RUA *r, RMuint32 m, RMuint32 p, void *v, RMuint32 s) {
    RMstatus rv, t = 0; do { rv = RUASetProperty(r, m, p, v, s, 0); } while (rv == 7 && ++t < 8);
    return 6;
}
static RMstatus ao_exch(struct RUA *r, RMuint32 m, RMuint32 p, void *iv, RMuint32 is, void *ov, RMuint32 os) {
    RMstatus rv, t = 0; do { rv = RUAExchangeProperty(r, m, p, iv, is, ov, os); } while (rv == 7 && ++t < 8);
    return 6;
}
struct ao_ctx { void *rua; void *set; void *exch; };  // [0]=RUA [4]=setprop [8]=exchangeprop
#include <pthread.h>
static struct RUA *g_rua;
static void *apply_thread(void *a) {   // runs ApplyOptions; blocks at prop 4168 but activates output
    (void)a;
    static char opts[512]; struct ao_ctx ctx;
    OutportsAudioInitOptions(opts);
    ((RMuint32 *)opts)[0] = 0;                     // audio engine index 0
    ctx.rua = g_rua; ctx.set = (void *)ao_set; ctx.exch = (void *)ao_exch;
    OutportsAudioApplyOptions(&ctx, opts);         // (never returns if it blocks)
    return 0;
}

#define SR      44100
#define BUFSZ   (1 << 16)          // 64KB DMA buffers (matches plaympeg)

#include <signal.h>
static void crashh(int s){ printf("*** audtone SIGNAL %d ***\n", s); fflush(stdout); _exit(139); }
int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    signal(SIGSEGV, crashh); signal(SIGBUS, crashh);
    { extern int verbose_stderr; verbose_stderr = 0; }
    int mode = (argc > 1) ? atoi(argv[1]) : 0;   // 0=CDA, 1=Pcmx
    struct RUA *pRUA = 0; struct DCC *pDCC = 0;
    struct DCCSTCSource *pSTC = 0; struct DCCAudioSource *pAud = 0;
    struct DCCStcProfile stc; struct DCCAudioProfile ap;
    struct RUABufferPool *pool = 0;
    RMuint32 adec = 0, aeng = 0, atmr = 0;
    RMstatus rv;

    int do_init = (argc > 2 && strcmp(argv[2], "init") == 0);
    rv = RUACreateInstance(&pRUA, CHIP); printf("RUACreate=%d\n", rv); if (rv != ROK) return 1;
    rv = DCCOpen(pRUA, &pDCC);           printf("DCCOpen=%d\n", rv);   if (rv != ROK) return 1;
    if (do_init) {   // standalone (no DOOM): bring up the media pipeline ourselves
        rv = DCCInitMicroCodeEx(pDCC, DCCInitMode_InitDisplay); printf("InitDisplay=%d\n", rv);
    }

    memset(&stc, 0, sizeof stc);
    stc.STCID = 0; stc.master = Master_STC;
    stc.stc_timer_id = 0; stc.stc_time_resolution = 90000;
    stc.video_timer_id = 1; stc.video_time_resolution = 90000;
    stc.audio_timer_id = 2; stc.audio_time_resolution = 90000;
    rv = DCCSTCOpen(pDCC, &stc, &pSTC); printf("STCOpen=%d\n", rv);

    memset(&ap, 0, sizeof ap);
    ap.BitstreamFIFOSize = 512 * 1024;
    ap.STCID = 0;
    rv = DCCOpenAudioDecoderSource(pDCC, &ap, &pAud); printf("OpenAudioDecoder=%d\n", rv);
    if (rv != ROK) return 3;
    rv = DCCGetAudioDecoderSourceInfo(pAud, &adec, &aeng, &atmr);
    printf("AudioInfo rv=%d decoder=0x%x engine=0x%x\n", rv, adec, aeng);

    // ---- MASTER VOLUME / UNMUTE ----
    // em8xxxalsa's snd_em8xxx_put_hw_volume writes property 4162 on the audio engine with an
    // 8-byte value {0, vol}. Default (recovery) is 0 = muted. Set it to unmute. Sweep vol via argv[3].
    {
        RMuint32 mv = (argc > 3) ? (RMuint32)strtoul(argv[3], 0, 0) : 0x00800000u;
        RMuint32 mvbuf[2]; mvbuf[0] = 0; mvbuf[1] = mv;
        rv = RUASetProperty(pRUA, aeng, 4162, mvbuf, sizeof mvbuf, 0);
        printf("MasterVol(prop4162)=%d vol=0x%x\n", rv, mv);
    }

    // ---- set PCM format ----
    RMuint32 params[16];
    memset(params, 0, sizeof params);
    if (mode == 0) {
        rv = DCCSetAudioPcmCdaFormat(pAud, params);   // CD-DA: fixed 44100/16/stereo
        printf("SetPcmCdaFormat=%d\n", rv);
    } else {
        params[0] = SR;      // guess: SampleRate
        params[1] = 2;       // guess: Channels
        params[2] = 16;      // guess: BitsPerSample
        rv = DCCSetAudioPcmxFormat(pAud, params);
        printf("SetPcmxFormat=%d\n", rv);
    }

    RMuint32 decvol = (argc > 4) ? (RMuint32)strtoul(argv[4], 0, 0) : 0x00010000u; /* Q16 unity */
    rv = DCCSetAudioSourceVolume(pAud, decvol); printf("SetVolume=%d decvol=0x%x\n", rv, decvol);

    // start the playback clock (STC) -- without this the engine decodes but never outputs
    rv = DCCSTCSetTimeResolution(pSTC, DCC_Audio, 90000); printf("STCTimeRes=%d\n", rv);
    rv = DCCSTCSetAudioOffset(pSTC, 0, 600);              printf("STCAudioOff=%d\n", rv);
    rv = DCCSTCSetTime(pSTC, 0ULL, 45000);                printf("STCSetTime=%d\n", rv);
    rv = DCCSTCSetSpeed(pSTC, 1, 1);                      printf("STCSetSpeed=%d\n", rv);

    rv = RUAOpenPool(pRUA, adec, 96, 16, RUA_POOL_DIRECTION_SEND, &pool);
    printf("OpenPool=%d\n", rv); if (rv != ROK) return 4;

    rv = DCCPlayAudioSource(pAud); printf("Play=%d\n", rv);
    rv = DCCSTCPlay(pSTC);         printf("STCPlay=%d\n", rv);

    // spawn the output-config in a background thread: it blocks at prop 4168, but that
    // write activates the I2S/DAC output -- meanwhile THIS thread keeps feeding PCM.
    g_rua = pRUA;
    { pthread_t apth; pthread_create(&apth, 0, apply_thread, 0); printf("apply_thread spawned\n"); }
    usleep(500000);   // let the output activate before feeding

    // ---- feed ~18 s of alternating 440/880Hz sine, 16-bit signed LE, stereo ----
    double phase = 0.0;
    int total_frames = SR * 18;        // 18 seconds (paced to realtime by FIFO backpressure)
    int fed = 0, sends = 0, okends = 0;
    while (fed < total_frames) {
        RMuint8 *buf = 0;
        int tries = 0;
        do { rv = RUAGetBuffer(pool, &buf, 100000); } while (rv == PEND && ++tries < 50);
        if (rv != ROK || !buf) { printf("GetBuffer=%d (stop)\n", rv); break; }
        int frames = BUFSZ / 4;                 // stereo 16-bit frames per buffer
        if (frames > total_frames - fed) frames = total_frames - fed;
        int16_t *s = (int16_t *)buf;
        for (int i = 0; i < frames; i++) {
            // beep pattern: 440Hz for 0.5s, 880Hz for 0.5s, alternating (obvious test tone)
            int half = ((fed + i) / (SR / 2)) & 1;
            double freq = half ? 880.0 : 440.0;
            phase += 2.0 * 3.14159265358979 * freq / SR;
            int16_t v = (int16_t)(16000.0 * sin(phase));
            s[i * 2] = v; s[i * 2 + 1] = v;
        }
        struct emhwlib_info info; memset(&info, 0, sizeof info);
        int bytes = frames * 4;
        tries = 0;
        do { rv = RUASendData(pRUA, adec, pool, buf, bytes, &info, sizeof info); } while (rv == PEND && ++tries < 50);
        sends++; if (rv == ROK) okends++;
        if (sends <= 2) printf("send#%d rv=%d bytes=%d\n", sends, rv, bytes);
        do { rv = RUAReleaseBuffer(pool, buf); } while (rv == PEND);
        fed += frames;
    }
    printf("fed=%d frames, sends=%d ok=%d\n", fed, sends, okends);
    // diagnostics: is the STC clock advancing + what state is the decoder in?
    {
        RMuint64 t1 = 0, t2 = 0; RMuint32 dstate = 0, count = 0;
        DCCSTCGetTime(pSTC, &t1, 90000); sleep(1); DCCSTCGetTime(pSTC, &t2, 90000);
        printf("STC %llu -> %llu adv=%d\n", (unsigned long long)t1, (unsigned long long)t2, (t2 > t1));
        RUAGetProperty(pRUA, adec, 4194, &dstate, sizeof dstate);   // RMAudioDecoderPropertyID_State
        printf("decoderState=%u\n", dstate);
        RUAGetProperty(pRUA, aeng, 4177, &count, sizeof count);     // RMAudioEnginePropertyID_ConnectedTaskCount
        printf("engineConnectedTasks=%u\n", count);
    }
    printf("draining...\n"); sleep(2);
    // clean up so repeated runs don't leak decoder/engine state
    DCCStopAudioSource(pAud);
    RUAClosePool(pool);
    DCCCloseAudioSource(pAud);
    DCCSTCClose(pSTC);
    printf("DONE (mode=%d)\n", mode);
    return 0;
}
