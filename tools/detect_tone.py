#!/usr/bin/env python3
# detect_tone.py <wav> -- FFT analysis to confirm the DOOM/audtone test tone (440/880 Hz alternating).
# Prints dominant frequency, its dB level, and energy specifically at 440 & 880 Hz vs the noise floor.
import sys, wave, numpy as np

def read_wav(p):
    w = wave.open(p, 'rb')
    n, sw, sr, nf = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(nf); w.close()
    dt = {1: np.int8, 2: np.int16, 4: np.int32}[sw]
    a = np.frombuffer(raw, dtype=dt).astype(np.float64)
    if n > 1: a = a.reshape(-1, n).mean(axis=1)
    if a.size: a /= (np.iinfo(dt).max or 1)
    return a, sr

def goertzel_mag(x, sr, f):
    # normalized magnitude of frequency f in signal x
    N = len(x); k = int(0.5 + N * f / sr); w = 2*np.pi*k/N
    cw = np.cos(w); c = 2*cw; s1 = s2 = 0.0
    for samp in x:
        s0 = samp + c*s1 - s2; s2 = s1; s1 = s0
    p = s1*s1 + s2*s2 - c*s1*s2
    return np.sqrt(max(p, 0)) * 2 / N

def main():
    x, sr = read_wav(sys.argv[1])
    if x.size == 0:
        print("EMPTY"); return
    x = x - x.mean()
    win = np.hanning(len(x)); xw = x * win
    sp = np.abs(np.fft.rfft(xw)); fr = np.fft.rfftfreq(len(xw), 1/sr)
    # ignore < 80 Hz (mains hum / DC)
    m = fr >= 80
    peak_i = np.argmax(sp[m]); peak_f = fr[m][peak_i]
    ref = sp[m].max()
    def db(v): return 20*np.log10(v/ref + 1e-12) if ref > 0 else -120
    rms = np.sqrt(np.mean(x**2)); rms_db = 20*np.log10(rms + 1e-12)
    # tone-band (350-1000) vs total energy ratio
    band = (fr >= 350) & (fr <= 1000)
    band_ratio = (sp[band]**2).sum() / ((sp[m]**2).sum() + 1e-12)
    g440 = goertzel_mag(x, sr, 440); g880 = goertzel_mag(x, sr, 880)
    g_noise = goertzel_mag(x, sr, 233)  # off-tone reference
    print(f"samples={x.size} sr={sr} rms_dbFS={rms_db:.1f}")
    print(f"PEAK_FREQ={peak_f:.1f}Hz")
    print(f"toneband_energy_ratio(350-1000Hz)={band_ratio*100:.1f}%")
    print(f"goertzel: 440Hz={g440:.5f} 880Hz={g880:.5f} offtone233Hz={g_noise:.5f}")
    tone_ratio = max(g440, g880) / (g_noise + 1e-9)
    is_tone = (band_ratio > 0.35) and (tone_ratio > 4) and (rms_db > -40)
    print(f"TONE_DETECTED={'YES' if is_tone else 'NO'} (tone/offtone={tone_ratio:.1f}x)")

if __name__ == "__main__":
    main()
