/* SPDX-License-Identifier: GPL-3.0-only */
/* ZP12 on the VIBE – sampling. The source (IN 1 as LINE / MIC / HI-Z, IN 1+2, or the computer at the USB cable: the
 * host's inputs "USB L" / "USB R", SDK 26) is taken mono, through GAIN, and resampled as it comes to 26.04 or
 * 27.5 kHz – a Hann-windowed sinc low-pass at 0.45 of the lower rate (a sampler's input filter, the one zp12's
 * editor and kit use) – straight into the pool, after the own waves. With a THRESHOLD the take waits for the sound
 * and keeps 20 ms from before it. It ends with REC (or STOP) again, or when the time is used up; then it is brought
 * to full level (a few pieces a frame), faded at its end and put on the selected pad – the pad's own sample before it
 * goes, its room is free again. A WAV from the card or a stick (LOAD) runs the same way, from the file. */
enum { RS_OFF, RS_ARMED, RS_REC, RS_NORM, RS_LOAD };
static int rs, rec_pad, rec_load;                /* rec_load: the take is a file (no normalising) */
static uint32_t rec_n, rec_cap, rec_peak, rec_t0;
static int rec_full;
static int rec_rate_i;                           /* 0: 26.04 kHz, 1: 27.5 kHz */
static int rec_16;                               /* the next take at 16 bits */
static const uint16_t REC_RATE[2] = { 26040, 27500 };
static int smp_count;                            /* names SMP01 .. */
static char rec_name[9];

enum { SRC_LINE, SRC_MIC, SRC_HIZ, SRC_IN12, SRC_USB, SRC_N };
static const char *const SRC_NAME[SRC_N] = { "LINE", "MIC", "HI-Z", "IN 1+2", "USB" };
static int in_src = SRC_LINE, in_gain = 0, in_thr = 0, in_mon = 0;   /* gain dB, THRESH 0 = off */
static const int16_t THR_DB[7] = { 0, -48, -42, -36, -30, -24, -18 };
static float in_peak;                            /* the meter (the audio writes, the draw reads and lets it fall) */
static int input_asked;
static int usb_input(void)                       /* the host's "USB L" (its "USB R" next to it), -1 = this host has none */
{
    int n = vibe_input_count();
    for (int i = 2; i + 1 < n; i++) { const char *a = vibe_input_name(i); if (a[0] == 'U' && a[1] == 'S' && a[2] == 'B' && a[3] == ' ' && a[4] == 'L') return i; }
    return -1;
}
static void input_on(void)
{
    if (!input_asked) { vibe_request(VIBE_REQ_AUDIO_INPUT); input_asked = 1; }
    vibe_input_link(in_src == SRC_IN12);
    vibe_input_mode(0, in_src <= SRC_HIZ ? in_src : VIBE_IN_LINE);
}

/* ---- the resampler: a windowed sinc, 16 taps a side, the table 64 times finer than a sample */
#define RH 16
#define ROS 64
static float rtab[2 * RH * ROS + 2];
static float rring[128];
static uint32_t rnin;
static double rph, rstep;
static void rs_setup(float src, float dst)
{
    float fc = 0.45f * (src < dst ? src : dst) / src;      /* cycles per input sample */
    for (int j = 0; j <= 2 * RH * ROS + 1; j++) {
        float t = (float)j / ROS - RH, h = t == 0.0f ? 2.0f * fc : vd_sin(fc * t) / (VD_PI * t);
        float w = t <= -RH || t >= RH ? 0.0f : 0.5f + 0.5f * vd_sin(t / (2.0f * RH) + 0.25f);
        rtab[j] = h * w;
    }
    for (int i = 0; i < 128; i++) rring[i] = 0;
    rnin = 0; rph = 0; rstep = (double)src / dst;
}

#define PRE_N 512                                /* ~20 ms at 26 kHz kept from before the threshold */
static int16_t pre[PRE_N];
static uint32_t pre_w;
static void rec_put(float y)                     /* one resampled sample */
{
    int32_t v = (int32_t)(y * 32767.0f);
    v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    if (rs == RS_ARMED) { pre[pre_w++ % PRE_N] = (int16_t)v; return; }
    if (rec_n >= rec_cap) { rec_full = 1; return; }
    pool[pool_top + rec_n++] = (int16_t)v;
    uint32_t a = (uint32_t)(v < 0 ? -v : v);
    if (a > rec_peak) rec_peak = a;
}
static void rs_feed(float x)
{
    rring[rnin & 127u] = x;
    rnin++;
    while ((uint32_t)rph + RH < rnin) {
        int i0 = (int)rph;
        float acc = 0;
        for (int k = i0 - RH + 1; k <= i0 + RH; k++) {
            if (k < 0) continue;
            float pos = (float)((rph - k) + RH) * ROS;
            int pi = (int)pos;
            float f = pos - (float)pi;
            acc += rring[k & 127] * (rtab[pi] + (rtab[pi + 1] - rtab[pi]) * f);
        }
        rec_put(acc);
        rph += rstep;
    }
}

/* the time a take may use (nibbles: the budget left, and the pad's own sample, which it replaces) */
static int rec_replaces(int pad)
{
    int w = sp_sound[pad].wave;
    return wave_is_own(w) && !wave_users(w, pad) ? w : -1;
}
static uint32_t avail_nib(int pad)
{
    uint32_t b = budget_nib(), u = used_nib();
    int w = rec_replaces(pad);
    if (w >= 0) u -= wave_nib(w);
    return u >= b ? 0u : b - u;
}
static int rec_begin(int pad, int from_file, uint32_t src_rate)
{
    if (rs != RS_OFF) return 0;
    if (wave_slot() < 0) { say("ALL 100 SAMPLE PLACES USED – DELETE ONE"); return 0; }
    uint32_t cap = avail_nib(pad) / (rec_16 ? 4u : 3u);
    if (cap > POOL_MAX - pool_top) cap = POOL_MAX - pool_top;
    if (cap < REC_RATE[rec_rate_i] / 10u) { say("NO SAMPLE TIME LEFT – DELETE A SAMPLE OR SETUP > TIME"); return 0; }
    rec_pad = pad; rec_cap = cap; rec_n = 0; rec_peak = 0; rec_full = 0; rec_load = from_file; pre_w = 0;
    for (int i = 0; i < PRE_N; i++) pre[i] = 0;
    rs_setup((float)src_rate, (float)REC_RATE[rec_rate_i]);
    rec_t0 = now_ms();
    rs = from_file ? RS_LOAD : (in_thr ? RS_ARMED : RS_REC);
    return 1;
}
static void rec_go(void)                         /* ARMED: the sound came – the 20 ms before it first */
{
    uint32_t n = pre_w < PRE_N ? pre_w : PRE_N;
    rs = RS_REC;
    for (uint32_t i = 0; i < n && rec_n < rec_cap; i++) {
        int16_t v = pre[(pre_w - n + i) % PRE_N];
        pool[pool_top + rec_n++] = v;
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        if (a > rec_peak) rec_peak = a;
    }
    rec_t0 = now_ms();
}
static uint32_t norm_i, norm_g;                  /* RS_NORM: where, the gain Q12 */
static void rec_end(void)                        /* REC / STOP again, or full: on to RS_NORM (the draw finishes it) */
{
    if (rs == RS_ARMED) { rs = RS_OFF; say("NOTHING RECORDED"); return; }
    if (rs != RS_REC && rs != RS_LOAD) return;
    if (rec_n < 256u) { rs = RS_OFF; say("TOO SHORT – NOTHING KEPT"); return; }
    norm_i = 0;
    norm_g = 4096u;
    if (!rec_load && rec_peak > 64u) {           /* to full level (-0.2 dB), up to +24 dB */
        uint32_t g = (32112u << 12) / rec_peak;
        norm_g = g > 65536u ? 65536u : g < 4096u ? 4096u : g;
    }
    rs = RS_NORM;
}
static void rec_cancel(void) { rs = RS_OFF; say("CANCELLED"); }
/* the draw's share: a few pieces of the gain, then the take on its pad */
static void rec_finish_step(void)
{
    if (rs != RS_NORM) return;
    int16_t *p = pool + pool_top;
    uint32_t e = norm_i + 600000u < rec_n ? norm_i + 600000u : rec_n;
    if (norm_g != 4096u)
        for (uint32_t i = norm_i; i < e; i++) {
            int32_t v = (p[i] * (int32_t)norm_g) >> 12;
            p[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    norm_i = e;
    if (norm_i < rec_n) return;
    uint32_t fade = REC_RATE[rec_rate_i] / 500u;  /* 2 ms out: no click at the end */
    for (uint32_t i = 0; i < fade && i < rec_n; i++) { uint32_t j = rec_n - 1u - i; p[j] = (int16_t)(p[j] * (int32_t)i / (int32_t)fade); }
    int w = wave_slot(), old = rec_replaces(rec_pad);
    own[w].used = 1; own[w].off = pool_top;
    if (rec_load && rec_name[0]) scopy(own[w].name, rec_name, 9);
    else { char nm[9] = "SMP"; icat2(nm, ++smp_count % 100, 9); scopy(own[w].name, nm, 9); }
    sp_wave[w].d = (const uint8_t *)(const void *)p; sp_wave[w].n = rec_n;
    sp_wave[w].rate = REC_RATE[rec_rate_i]; sp_wave[w].fmt = rec_16 ? SPW_16 : SPW_16AT12;
    pool_top += rec_n;
    sp_sound_t *s = &sp_sound[rec_pad];
    s->wave = (uint8_t)w; s->tune = 0; s->fine = 0; s->decay = 127; s->start = 0; s->end = (uint16_t)SP_TR;
    s->flags = (uint8_t)(s->flags & ~(SPF_REVERSE | SPF_33));
    if (old >= 0) wave_free(old);                 /* (after: the new take moves down into the room) */
    store_wave_later(w);
    touched();
    rs = RS_OFF;
    char m[48] = "";
    scat(m, own[w].name, 48); scat(m, " ON PAD ", 48);
    { char b[4] = { (char)('A' + rec_pad / 8), (char)('1' + rec_pad % 8), 0 }; scat(m, b, 48); }
    scat(m, " – ", 48);
    { char b[16]; scat(m, vg_ftoa((float)rec_n / REC_RATE[rec_rate_i], 1, b), 48); } scat(m, " S", 48);
    say(m);
}

/* ---- the audio's share: the source into the meter, the take, the monitor */
static float mon_buf[VIBE_MAX_BLOCK];
static void rec_audio(const float *in, int n)
{
    const float *a = 0, *b = 0;
    if (in_src == SRC_USB) { int u = usb_input(); if (u >= 0) { a = vibe_input(u); b = vibe_input(u + 1); } }
    else { a = in; if (in_src == SRC_IN12) b = vibe_input(1); }
    float g = vd_exp2((float)in_gain / 6.0206f), thr = 0;
    if (in_thr) thr = vd_exp2((float)THR_DB[in_thr] / 6.0206f);
    float pk = in_peak;
    for (int i = 0; i < n; i++) {
        float x = a ? a[i] : 0.0f;
        if (b) x = 0.5f * (x + b[i]);
        x *= g;
        float ax = x < 0 ? -x : x;
        if (ax > pk) pk = ax;
        mon_buf[i] = x;
        if (rs == RS_ARMED || rs == RS_REC) {
            if (rs == RS_ARMED && ax >= thr) rec_go();
            rs_feed(x);
            if (rec_full && rs == RS_REC) rec_end();
        }
    }
    in_peak = pk;
}

/* ---- LOAD: a WAV from the card or a stick, read a piece a frame (the media never waits: PENDING = next frame) */
static struct {
    char path[VIBE_MEDIA_PATH];
    uint64_t off, end;
    int ch, bits, fl, hdr;                        /* fl: 32-bit float */
    uint32_t rate;
} LD;
static uint8_t ld_buf[65536 + 8];
static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void load_start(int pad, const char *path, const char *file)
{
    if (rs != RS_OFF) return;
    rec_pad = pad;
    scopy(LD.path, path, sizeof LD.path);
    LD.hdr = 0; LD.off = 0;
    int j = 0;                                    /* the name: up to 8 letters / digits of the file's */
    for (int i = 0; file[i] && file[i] != '.' && j < 8; i++) {
        char c = file[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) rec_name[j++] = c;
    }
    rec_name[j] = 0;
    rs = RS_LOAD;                                 /* (rec_begin comes when the header says the rate) */
    rec_n = 0;
}
static void load_step(void)
{
    if (rs != RS_LOAD) return;
    if (!LD.hdr) {
        int n = vibe_media_read(LD.path, 0, ld_buf, 4096);
        if (n == VIBE_MEDIA_PENDING) return;
        if (n < 44 || memcmp(ld_buf, "RIFF", 4) || memcmp(ld_buf + 8, "WAVE", 4)) { rs = RS_OFF; say("NOT A WAV FILE"); return; }
        int i = 12, fmt = 0;
        while (i + 8 <= n) {
            uint32_t sz = rd32(ld_buf + i + 4);
            if (!memcmp(ld_buf + i, "fmt ", 4) && i + 24 <= n) {
                fmt = rd16(ld_buf + i + 8); LD.ch = rd16(ld_buf + i + 10); LD.rate = rd32(ld_buf + i + 12); LD.bits = rd16(ld_buf + i + 22);
                if (fmt == 0xFFFE && i + 34 <= n) fmt = rd16(ld_buf + i + 32);
            } else if (!memcmp(ld_buf + i, "data", 4)) { LD.off = (uint64_t)i + 8u; LD.end = LD.off + sz; break; }
            i += 8 + (int)((sz + 1u) & ~1u);
        }
        LD.fl = fmt == 3 && LD.bits == 32;
        if (!LD.off || !(fmt == 1 || LD.fl) || LD.ch < 1 || LD.ch > 8 || (fmt == 1 && LD.bits != 8 && LD.bits != 16 && LD.bits != 24 && LD.bits != 32) || LD.rate < 4000) {
            rs = RS_OFF; say(LD.off ? "WAV: ONLY PCM 8/16/24/32 BIT OR FLOAT" : "WAV: NO DATA FOUND"); return;
        }
        rs = RS_OFF;
        if (!rec_begin(rec_pad, 1, LD.rate)) return;
        LD.hdr = 1;
        return;
    }
    int fb = LD.ch * (LD.bits / 8);
    int want = (int)(65536 / fb) * fb;
    if (LD.end - LD.off < (uint64_t)want) want = (int)(LD.end - LD.off);
    int n = want > 0 ? vibe_media_read(LD.path, LD.off, ld_buf, want) : 0;
    if (n == VIBE_MEDIA_PENDING) return;
    if (n < 0) { rec_cancel(); say("THE FILE IS GONE"); return; }
    n -= n % fb;
    for (int f = 0; f < n / fb && !rec_full; f++) {
        const uint8_t *q = ld_buf + f * fb;
        float x = 0;
        for (int c = 0; c < LD.ch && c < 2; c++, q += LD.bits / 8) {
            float v;
            if (LD.fl) { uint32_t u = rd32(q); memcpy(&v, &u, 4); }
            else if (LD.bits == 8) v = (q[0] - 128) / 128.0f;
            else if (LD.bits == 16) v = (int16_t)rd16(q) / 32768.0f;
            else if (LD.bits == 24) v = (float)(((int32_t)((uint32_t)q[0] << 8 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 24)) >> 8) / 8388608.0f;
            else v = (float)(int32_t)rd32(q) / 2147483648.0f;
            x += v;
        }
        rs_feed(LD.ch > 1 ? 0.5f * x : x);
    }
    LD.off += (uint64_t)n;
    if (rec_full || n == 0 || LD.off >= LD.end) { if (rec_full) say("THE TIME RAN OUT – THE START KEPT"); rec_end(); }
}
