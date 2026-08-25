/*
 * SpectreDAW v0.3 — Nintendo 3DS Homebrew Synthesizer + Piano Roll
 * Build: devkitARM + libctru
 *
 * Architecture:
 *  • 3 drum tracks (kick/snare/hat): static one-shot linearAlloc buffers, ch0-2
 *  • 3 synth voices (ch3-5): streaming double-buffer, real-time per-sample DSP
 *  • Synth engine: osc1+osc2 (5 types) → SVF low-pass filter → ADSR envelope
 *  • LFO modulates filter cutoff
 *  • Piano roll: StepChord per step (up to 3 MIDI notes)
 *  • Patches saved to sdmc:/3ds/SpectreDAW/patches.bin
 *  • Pattern saved to sdmc:/3ds/SpectreDAW/pattern.bin
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

/* ─────────────────── Constants ─────────────────── */

#define SAMPLERATE       32728
#define NUM_DRUM_TRACKS  3
#define NUM_STEPS        16
#define NUM_VOICES       3
#define NUM_PATCHES      4
#define MAX_CHORD        3
#define BPM_DEFAULT      120
#define BPM_MIN          60
#define BPM_MAX          240
#define BPM_STEP         5
#define BOTTOM_W         320
#define BOTTOM_H         240
#define CH_SYNTH0        3
#define BLOCK_SAMPLES    1024
#define BLOCK_BYTES      (BLOCK_SAMPLES * (int)sizeof(s16))

/* Drum sound lengths */
#define KICK_SAMPLES     ((int)(0.300f * SAMPLERATE))
#define SNARE_SAMPLES    ((int)(0.200f * SAMPLERATE))
#define HIHAT_SAMPLES    ((int)(0.045f * SAMPLERATE))

/* SEQ view grid layout — 16 steps × 19 px + 15 × 1 gap + 1 margin = 320 */
#define GRID_MARGIN_X    1
#define GRID_MARGIN_Y    18
#define CELL_W           19
#define CELL_H           52
#define CELL_HGAP        1
#define CELL_VGAP        1

/* ADSR states */
#define ADSR_OFF     0
#define ADSR_ATTACK  1
#define ADSR_DECAY   2
#define ADSR_SUSTAIN 3
#define ADSR_RELEASE 4

/* Views */
#define VIEW_SEQ          0
#define VIEW_NOTE_PICKER  1
#define VIEW_SYNTH_EDIT   2

/* Piano key range: C3 (MIDI 48) .. B4 (MIDI 71) */
#define PIANO_MIDI_LO  48
#define PIANO_MIDI_HI  71
#define NUM_WHITE      14
#define KEY_W          22
#define KEY_H          130
#define KEY_Y          88
#define KEY_X0         4
#define BK_W           13
#define BK_H           80

/* ─────────────────── Data types ─────────────────── */

typedef struct {
    char name[16];
    u8   osc1_type, osc2_type;   /* 0=sin 1=saw 2=sq 3=tri 4=noise */
    s8   osc2_detune;            /* -50..+50 cents */
    u8   osc_mix;                /* 0=osc1 only, 255=osc2 only */
    u8   atk, dec, sus, rel;
    u8   cut, res;
    u8   lfo_rate, lfo_depth;
} SynthPatch;

typedef struct {
    u8 count;
    s8 notes[MAX_CHORD];         /* MIDI 0-127, -1 = unused */
} StepChord;

typedef struct {
    bool  active;
    int   midi_note;
    float phase1, phase2;
    float lfo_phase;
    int   adsr_state;
    float adsr_val;
    float release_start_val;
    float svf_low, svf_band;
    u64   release_at_ms;
} SynthVoice;

/* ─────────────────── Global state ─────────────────── */

static bool       drumGrid[NUM_DRUM_TRACKS][NUM_STEPS];
static StepChord  synthRoll[NUM_STEPS];
static SynthPatch patches[NUM_PATCHES];
static SynthVoice voices[NUM_VOICES];
static int        g_active_patch = 0;
static int        g_bpm          = BPM_DEFAULT;
static bool       g_playing      = false;
static int        g_currentStep  = 0;
static u64        g_lastStepMs   = 0;
static int        g_view         = VIEW_SEQ;
static int        g_picker_step  = 0;
static int        g_synth_tab    = 0;

static float      g_note_freq[128];

/* Drum audio */
static s16        *g_drum_buf[NUM_DRUM_TRACKS];
static u32         g_drum_bytes[NUM_DRUM_TRACKS];
static ndspWaveBuf g_drum_wb[NUM_DRUM_TRACKS];

/* Synth streaming audio */
static s16        *g_synth_pcm[NUM_VOICES][2];
static ndspWaveBuf g_synth_wb[NUM_VOICES][2];

/* ─────────────────── Default patches ─────────────────── */

static const SynthPatch DEFAULT_PATCHES[NUM_PATCHES] = {
    { "Lead Pluck",  1, 1,  7, 128,   5, 120, 180,  80, 160,  80, 40, 60 },
    { "Pad Strings", 0, 0, 12, 200,  80, 100, 220, 120, 100,  60, 50, 80 },
    { "Bass Stab",   1, 4,  0,   0,   2,  60, 100,  40,  80, 120,  0,  0 },
    { "Noise FX",    4, 4,  0,   0,   1, 200,   0, 150, 220, 180,100,180 },
};

/* ─────────────────── 3×5 Pixel font ─────────────────── */
/* Rows encoded: bit2=left col, bit1=mid col, bit0=right col */
static const u8 FONT[39][5] = {
    {6,5,5,5,6}, /* 0 */  {2,6,2,2,7}, /* 1 */  {6,1,6,4,7}, /* 2 */
    {6,1,6,1,6}, /* 3 */  {5,5,7,1,1}, /* 4 */  {7,4,6,1,6}, /* 5 */
    {6,4,6,5,6}, /* 6 */  {7,1,1,2,2}, /* 7 */  {6,5,6,5,6}, /* 8 */
    {6,5,6,1,6}, /* 9 */
    {2,5,7,5,5}, /* A */  {6,5,6,5,6}, /* B */  {7,4,4,4,7}, /* C */
    {6,5,5,5,6}, /* D */  {7,4,7,4,7}, /* E */  {7,4,7,4,4}, /* F */
    {7,4,5,5,7}, /* G */
    {2,7,2,7,2}, /* # */  {0,0,7,0,0}, /* - */  {0,0,0,0,0}, /* spc */
    /* H-Z */
    {5,5,7,5,5}, /* H */  {7,2,2,2,7}, /* I */  {1,1,1,5,6}, /* J */
    {5,6,4,6,5}, /* K */  {4,4,4,4,7}, /* L */  {5,7,5,5,5}, /* M */
    {5,7,7,5,5}, /* N */  {6,5,5,5,6}, /* O */  {6,5,6,4,4}, /* P */
    {6,5,5,3,6}, /* Q */  {6,5,6,5,5}, /* R */  {7,4,6,1,6}, /* S */
    {7,2,2,2,2}, /* T */  {5,5,5,5,6}, /* U */  {5,5,5,2,2}, /* V */
    {5,5,5,7,5}, /* W */  {5,5,2,5,5}, /* X */  {5,5,2,2,2}, /* Y */
    {7,1,2,4,7}, /* Z */
};

static int char_idx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'G') return 10 + (c - 'A');
    if (c >= 'H' && c <= 'Z') return 20 + (c - 'H');
    if (c == '#') return 17;
    if (c == '-') return 18;
    return 19;
}

/* ─────────────────── Framebuffer helpers ─────────────────── */
/* 3DS bottom screen: column-major, BGR order. */
/* pixel(x,y) → offset = (x*240 + (239-y)) * 3               */

static inline void put_pixel(u8 *fb, int x, int y, u8 r, u8 g, u8 b) {
    if ((unsigned)x >= BOTTOM_W || (unsigned)y >= BOTTOM_H) return;
    u32 off = ((u32)x * 240u + (u32)(239 - y)) * 3u;
    fb[off]   = b;
    fb[off+1] = g;
    fb[off+2] = r;
}

static void fill_rect(u8 *fb, int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    for (int cx = x; cx < x + w; cx++)
        for (int cy = y; cy < y + h; cy++)
            put_pixel(fb, cx, cy, r, g, b);
}

static void draw_glyph(u8 *fb, int x, int y, char c, u8 r, u8 g, u8 b, int scale) {
    const u8 *row = FONT[char_idx(c)];
    for (int gy = 0; gy < 5; gy++)
        for (int gx = 0; gx < 3; gx++)
            if (row[gy] & (4 >> gx))
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        put_pixel(fb, x + gx*scale + sx, y + gy*scale + sy, r, g, b);
}

static void draw_str(u8 *fb, int x, int y, const char *s, u8 r, u8 g, u8 b, int scale) {
    for (; *s; s++, x += (3 + 1) * scale)
        draw_glyph(fb, x, y, *s, r, g, b, scale);
}

/* ─────────────────── Note name helpers ─────────────────── */

static const char *NOTE_NAMES[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static void midi_to_str(int midi, char *out) { /* out: 4 bytes */
    int octave = midi / 12 - 1;
    const char *nn = NOTE_NAMES[midi % 12];
    int i = 0;
    out[i++] = nn[0];
    if (nn[1] == '#') out[i++] = '#';
    out[i++] = '0' + (char)octave;
    out[i] = '\0';
}

/* ─────────────────── Oscillator ─────────────────── */

static float osc_sample(int type, float p) {
    switch (type) {
        case 0:  return sinf(p * 2.0f * (float)M_PI);
        case 1:  return 2.0f * p - 1.0f;
        case 2:  return p < 0.5f ? 1.0f : -1.0f;
        case 3:  return 4.0f * fabsf(p - 0.5f) - 1.0f;
        default: return ((float)(rand() & 0x7FFF) / 16383.5f) - 1.0f;
    }
}

/* ─────────────────── Synth block fill ─────────────────── */

static void fill_synth_block(int v, s16 *buf, int n) {
    SynthVoice *vo = &voices[v];
    SynthPatch *pa = &patches[g_active_patch];

    /* note-off check at block boundary */
    if (vo->adsr_state == ADSR_SUSTAIN && vo->release_at_ms > 0
        && osGetTime() >= vo->release_at_ms) {
        vo->adsr_state = ADSR_RELEASE;
        vo->release_start_val = vo->adsr_val;
    }

    if (!vo->active || vo->adsr_state == ADSR_OFF) {
        memset(buf, 0, (size_t)(n * (int)sizeof(s16)));
        return;
    }

    float freq1 = g_note_freq[vo->midi_note];
    float detune_mult = powf(2.0f, pa->osc2_detune / 1200.0f);
    float freq2 = freq1 * detune_mult;
    float mix2 = pa->osc_mix / 255.0f;
    float mix1 = 1.0f - mix2;

    float atk_inc = (pa->atk  < 2) ? 1.0f : 1.0f / ((pa->atk  / 255.0f * 2.0f)  * SAMPLERATE);
    float dec_inc = (pa->dec  < 2) ? 1.0f : 1.0f / ((pa->dec  / 255.0f * 1.5f)  * SAMPLERATE);
    float sus_lvl = pa->sus  / 255.0f;
    float rel_inc = (pa->rel  < 2) ? 1.0f : 1.0f / ((pa->rel  / 255.0f * 3.0f)  * SAMPLERATE);

    float base_cut    = 200.0f + (pa->cut        / 255.0f) * 13800.0f;
    float lfo_depth_h = (pa->lfo_depth / 255.0f) * 6000.0f;
    float lfo_rate_h  = 0.1f + (pa->lfo_rate   / 255.0f) * 10.0f;
    float q = 1.0f - pa->res / 255.0f * 0.97f;

    for (int i = 0; i < n; i++) {
        /* ADSR */
        switch (vo->adsr_state) {
            case ADSR_ATTACK:
                vo->adsr_val += atk_inc;
                if (vo->adsr_val >= 1.0f) { vo->adsr_val = 1.0f; vo->adsr_state = ADSR_DECAY; }
                break;
            case ADSR_DECAY:
                vo->adsr_val -= dec_inc * (1.0f - sus_lvl);
                if (vo->adsr_val <= sus_lvl) { vo->adsr_val = sus_lvl; vo->adsr_state = ADSR_SUSTAIN; }
                break;
            case ADSR_SUSTAIN:
                vo->adsr_val = sus_lvl;
                break;
            case ADSR_RELEASE:
                vo->adsr_val -= rel_inc * vo->release_start_val;
                if (vo->adsr_val <= 0.0f) {
                    vo->adsr_val = 0.0f;
                    vo->adsr_state = ADSR_OFF;
                    vo->active = false;
                }
                break;
            default: break;
        }

        /* Oscillators */
        vo->phase1 += freq1 / SAMPLERATE;
        if (vo->phase1 >= 1.0f) vo->phase1 -= 1.0f;
        vo->phase2 += freq2 / SAMPLERATE;
        if (vo->phase2 >= 1.0f) vo->phase2 -= 1.0f;

        float s = osc_sample(pa->osc1_type, vo->phase1) * mix1
                + osc_sample(pa->osc2_type, vo->phase2) * mix2;

        /* LFO → cutoff modulation */
        vo->lfo_phase += lfo_rate_h / SAMPLERATE;
        if (vo->lfo_phase >= 1.0f) vo->lfo_phase -= 1.0f;
        float lfo = sinf(vo->lfo_phase * 2.0f * (float)M_PI);
        float cut = base_cut + lfo * lfo_depth_h;
        if (cut < 20.0f)    cut = 20.0f;
        if (cut > 14000.0f) cut = 14000.0f;

        /* State-variable filter (low-pass output) */
        float f    = 2.0f * sinf((float)M_PI * cut / SAMPLERATE);
        vo->svf_low  += f * vo->svf_band;
        float high    = s - vo->svf_low - q * vo->svf_band;
        vo->svf_band += f * high;
        s = vo->svf_low;

        buf[i] = (s16)(s * vo->adsr_val * 0.55f * 32767.0f);
    }
}

/* ─────────────────── Streaming audio update (call every frame) ─────────────────── */

static void update_synth_streaming(void) {
    for (int v = 0; v < NUM_VOICES; v++) {
        for (int b = 0; b < 2; b++) {
            if (g_synth_wb[v][b].status == NDSP_WBUF_DONE) {
                fill_synth_block(v, g_synth_pcm[v][b], BLOCK_SAMPLES);
                DSP_FlushDataCache(g_synth_pcm[v][b], (u32)BLOCK_BYTES);
                g_synth_wb[v][b].nsamples = BLOCK_SAMPLES;
                g_synth_wb[v][b].status   = NDSP_WBUF_FREE;
                ndspChnWaveBufAdd(CH_SYNTH0 + v, &g_synth_wb[v][b]);
            }
        }
    }
}

/* ─────────────────── Trigger helpers ─────────────────── */

static void trigger_voice(int v, int midi_note, u64 release_at) {
    SynthVoice *vo  = &voices[v];
    vo->active      = true;
    vo->midi_note   = midi_note;
    vo->phase1      = 0.0f;
    vo->phase2      = 0.0f;
    vo->lfo_phase   = 0.0f;
    vo->adsr_state  = ADSR_ATTACK;
    vo->adsr_val    = 0.0f;
    vo->release_start_val = 0.0f;
    vo->svf_low     = 0.0f;
    vo->svf_band    = 0.0f;
    vo->release_at_ms = release_at;
}

static void trigger_drum(int t) {
    ndspChnWaveBufClear(t);
    g_drum_wb[t].status = NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(t, &g_drum_wb[t]);
}

/* ─────────────────── Drum synthesis (v0.2 carry-over) ─────────────────── */

static void synthesize_kick(s16 *buf, int n) {
    float phase = 0.0f;
    int click_end = (int)(0.003f * SAMPLERATE);
    for (int i = 0; i < n; i++) {
        float t    = (float)i / SAMPLERATE;
        float freq = 50.0f + 100.0f * expf(-t * 20.0f);
        float amp  = expf(-t * 11.0f);
        phase += 2.0f * (float)M_PI * freq / SAMPLERATE;
        if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        float s = sinf(phase) * amp;
        if (i < click_end) {
            float noise = ((float)(rand() & 0x7FFF) / 16383.5f) - 1.0f;
            s += noise * 0.35f * expf(-t * 300.0f);
        }
        buf[i] = (s16)(s * 0.7f * 32767.0f);
    }
}

static void synthesize_snare(s16 *buf, int n) {
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t     = (float)i / SAMPLERATE;
        float amp   = expf(-t * 20.0f);
        float noise = ((float)(rand() & 0x7FFF) / 16383.5f) - 1.0f;
        phase      += 2.0f * (float)M_PI * 200.0f / SAMPLERATE;
        float body  = sinf(phase) * expf(-t * 35.0f);
        buf[i] = (s16)((noise * 0.6f + body * 0.4f) * amp * 0.7f * 32767.0f);
    }
}

static void synthesize_hihat(s16 *buf, int n) {
    float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
    for (int i = 0; i < n; i++) {
        float t   = (float)i / SAMPLERATE;
        float amp = expf(-t * 65.0f);
        p1 = fmodf(p1 + 8372.0f  / SAMPLERATE, 1.0f);
        p2 = fmodf(p2 + 10548.0f / SAMPLERATE, 1.0f);
        p3 = fmodf(p3 + 13289.0f / SAMPLERATE, 1.0f);
        float s = (p1 < 0.5f ? 0.3f : -0.3f)
                + (p2 < 0.5f ? 0.3f : -0.3f)
                + (p3 < 0.5f ? 0.4f : -0.4f);
        buf[i] = (s16)(s * amp * 0.7f * 32767.0f);
    }
}

/* ─────────────────── Audio init / cleanup ─────────────────── */

static void init_audio(void) {
    const int drum_counts[NUM_DRUM_TRACKS] = {
        KICK_SAMPLES, SNARE_SAMPLES, HIHAT_SAMPLES
    };

    for (int t = 0; t < NUM_DRUM_TRACKS; t++) {
        g_drum_bytes[t] = (u32)(drum_counts[t] * (int)sizeof(s16));
        g_drum_buf[t]   = (s16 *)linearAlloc(g_drum_bytes[t]);
        memset(g_drum_buf[t], 0, g_drum_bytes[t]);
    }
    synthesize_kick  (g_drum_buf[0], KICK_SAMPLES);
    synthesize_snare (g_drum_buf[1], SNARE_SAMPLES);
    synthesize_hihat (g_drum_buf[2], HIHAT_SAMPLES);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = 1.0f;

    for (int t = 0; t < NUM_DRUM_TRACKS; t++) {
        DSP_FlushDataCache(g_drum_buf[t], g_drum_bytes[t]);
        memset(&g_drum_wb[t], 0, sizeof(ndspWaveBuf));
        g_drum_wb[t].data_vaddr = g_drum_buf[t];
        g_drum_wb[t].nsamples   = g_drum_bytes[t] / (u32)sizeof(s16);
        g_drum_wb[t].looping    = false;
        g_drum_wb[t].status     = NDSP_WBUF_FREE;

        ndspChnReset(t);
        ndspChnSetInterp(t, NDSP_INTERP_LINEAR);
        ndspChnSetRate(t, (float)SAMPLERATE);
        ndspChnSetFormat(t, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetMix(t, mix);
    }

    for (int v = 0; v < NUM_VOICES; v++) {
        int ch = CH_SYNTH0 + v;
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
        ndspChnSetRate(ch, (float)SAMPLERATE);
        ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetMix(ch, mix);

        for (int b = 0; b < 2; b++) {
            g_synth_pcm[v][b] = (s16 *)linearAlloc((u32)BLOCK_BYTES);
            memset(g_synth_pcm[v][b], 0, (size_t)BLOCK_BYTES);
            DSP_FlushDataCache(g_synth_pcm[v][b], (u32)BLOCK_BYTES);
            memset(&g_synth_wb[v][b], 0, sizeof(ndspWaveBuf));
            g_synth_wb[v][b].data_vaddr = g_synth_pcm[v][b];
            g_synth_wb[v][b].nsamples   = BLOCK_SAMPLES;
            g_synth_wb[v][b].looping    = false;
            g_synth_wb[v][b].status     = NDSP_WBUF_FREE;
            ndspChnWaveBufAdd(ch, &g_synth_wb[v][b]);
        }
    }
}

static void cleanup_audio(void) {
    for (int t = 0; t < NUM_DRUM_TRACKS; t++) ndspChnWaveBufClear(t);
    for (int v = 0; v < NUM_VOICES; v++) ndspChnWaveBufClear(CH_SYNTH0 + v);
    ndspExit();
    for (int t = 0; t < NUM_DRUM_TRACKS; t++) {
        if (g_drum_buf[t]) { linearFree(g_drum_buf[t]); g_drum_buf[t] = NULL; }
    }
    for (int v = 0; v < NUM_VOICES; v++)
        for (int b = 0; b < 2; b++)
            if (g_synth_pcm[v][b]) { linearFree(g_synth_pcm[v][b]); g_synth_pcm[v][b] = NULL; }
}

/* ─────────────────── SD persistence ─────────────────── */

#define PATTERN_MAGIC "SDAW"

static void load_from_sd(void) {
    FILE *f;

    f = fopen("sdmc:/3ds/SpectreDAW/patches.bin", "rb");
    if (f) {
        fread(patches, sizeof(SynthPatch), NUM_PATCHES, f);
        fclose(f);
    }

    f = fopen("sdmc:/3ds/SpectreDAW/pattern.bin", "rb");
    if (f) {
        char magic[4];
        if (fread(magic, 1, 4, f) == 4 && memcmp(magic, PATTERN_MAGIC, 4) == 0) {
            fread(drumGrid,  sizeof(drumGrid),  1, f);
            fread(synthRoll, sizeof(synthRoll), 1, f);
            fread(&g_bpm,    sizeof(g_bpm),     1, f);
            fread(&g_active_patch, sizeof(g_active_patch), 1, f);
        }
        fclose(f);
    }
}

static void save_to_sd(void) {
    mkdir("sdmc:/3ds/SpectreDAW", 0777);

    FILE *f = fopen("sdmc:/3ds/SpectreDAW/patches.bin", "wb");
    if (f) {
        fwrite(patches, sizeof(SynthPatch), NUM_PATCHES, f);
        fclose(f);
    }

    f = fopen("sdmc:/3ds/SpectreDAW/pattern.bin", "wb");
    if (f) {
        fwrite(PATTERN_MAGIC,   1, 4, f);
        fwrite(drumGrid,  sizeof(drumGrid),  1, f);
        fwrite(synthRoll, sizeof(synthRoll), 1, f);
        fwrite(&g_bpm,    sizeof(g_bpm),     1, f);
        fwrite(&g_active_patch, sizeof(g_active_patch), 1, f);
        fclose(f);
    }
}

/* ─────────────────── Track colors ─────────────────── */
/* Kick=red, Snare=green, Hat=cyan */
static const u8 DRUM_COLOR[NUM_DRUM_TRACKS][3] = {
    { 220, 55, 55 },
    {  55,200, 55 },
    {  55,200,220 },
};
/* Synth row = magenta */

/* ─────────────────── Horizontal slider helper ─────────────────── */
static void draw_hslider(u8 *fb, int sx, int sy, int sw, int sh, u8 val,
                          u8 fr, u8 fg, u8 fb2) {
    fill_rect(fb, sx, sy, sw, sh, 35, 35, 35);
    int filled = (int)val * sw / 255;
    if (filled > 0) fill_rect(fb, sx, sy, filled, sh, fr, fg, fb2);
    /* knob */
    int kx = sx + filled - 2;
    if (kx < sx) kx = sx;
    fill_rect(fb, kx, sy, 4, sh, 220, 220, 220);
}

/* Vertical slider: high value = tall fill from bottom */
static void draw_vslider(u8 *fb, int sx, int sy, int sw, int sh, u8 val,
                          u8 fr, u8 fg, u8 fb2) {
    fill_rect(fb, sx, sy, sw, sh, 35, 35, 35);
    int filled = (int)val * sh / 255;
    if (filled > 0) fill_rect(fb, sx, sy + sh - filled, sw, filled, fr, fg, fb2);
    int ky = sy + sh - filled - 2;
    if (ky < sy) ky = sy;
    fill_rect(fb, sx, ky, sw, 4, 220, 220, 220);
}

/* ─────────────────── SEQ view draw ─────────────────── */

static void draw_seq(void) {
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    memset(fb, 0x12, (size_t)(BOTTOM_W * BOTTOM_H * 3));

    /* Title bar */
    fill_rect(fb, 0, 0, BOTTOM_W, GRID_MARGIN_Y - 2, 30, 30, 50);
    draw_str(fb, 2, 4, "SEQ", 160, 160, 220, 1);
    {
        char pname[32];
        snprintf(pname, sizeof(pname), "P%d %.10s",
                 g_active_patch + 1, patches[g_active_patch].name);
        draw_str(fb, 30, 4, pname, 200, 200, 200, 1);
    }

    /* Grid: 3 drum rows + 1 synth row */
    for (int row = 0; row < 4; row++) {
        int cy = GRID_MARGIN_Y + row * (CELL_H + CELL_VGAP);
        bool is_synth = (row == 3);

        for (int col = 0; col < NUM_STEPS; col++) {
            int cx = GRID_MARGIN_X + col * (CELL_W + CELL_HGAP);
            bool on_beat = (col % 4 == 0);
            u8 bg = on_beat ? 45 : 28;

            if (!is_synth) {
                /* Drum cell */
                bool active = drumGrid[row][col];
                if (active) {
                    fill_rect(fb, cx, cy, CELL_W, CELL_H,
                              DRUM_COLOR[row][0], DRUM_COLOR[row][1], DRUM_COLOR[row][2]);
                } else {
                    fill_rect(fb, cx, cy, CELL_W, CELL_H, bg, bg, bg);
                    /* dim color tint */
                    fill_rect(fb, cx, cy, 2, CELL_H,
                              DRUM_COLOR[row][0]/4, DRUM_COLOR[row][1]/4, DRUM_COLOR[row][2]/4);
                }
            } else {
                /* Synth piano-roll cell */
                StepChord *sc = &synthRoll[col];
                if (sc->count > 0) {
                    fill_rect(fb, cx, cy, CELL_W, CELL_H, 150, 80, 200);
                    /* show first note label */
                    char label[4];
                    midi_to_str(sc->notes[0], label);
                    draw_str(fb, cx + 1, cy + 2, label, 240, 240, 240, 1);
                } else {
                    fill_rect(fb, cx, cy, CELL_W, CELL_H, bg, bg, bg);
                    fill_rect(fb, cx, cy, 2, CELL_H, 80, 40, 100);
                }
            }
        }
    }

    /* Playhead border */
    u8 ph_r = g_playing ? 255 : 100;
    u8 ph_g = g_playing ? 220 :  70;
    u8 ph_b = 0;
    for (int row = 0; row < 4; row++) {
        int cx = GRID_MARGIN_X + g_currentStep * (CELL_W + CELL_HGAP);
        int cy = GRID_MARGIN_Y + row * (CELL_H + CELL_VGAP);
        for (int px = cx; px < cx + CELL_W; px++) {
            put_pixel(fb, px, cy,            ph_r, ph_g, ph_b);
            put_pixel(fb, px, cy + 1,        ph_r, ph_g, ph_b);
            put_pixel(fb, px, cy + CELL_H-1, ph_r, ph_g, ph_b);
            put_pixel(fb, px, cy + CELL_H-2, ph_r, ph_g, ph_b);
        }
        for (int py = cy; py < cy + CELL_H; py++) {
            put_pixel(fb, cx,            py, ph_r, ph_g, ph_b);
            put_pixel(fb, cx + 1,        py, ph_r, ph_g, ph_b);
            put_pixel(fb, cx + CELL_W-1, py, ph_r, ph_g, ph_b);
            put_pixel(fb, cx + CELL_W-2, py, ph_r, ph_g, ph_b);
        }
    }
}

/* ─────────────────── Note picker draw ─────────────────── */

/* White key MIDI values for C3..B4 */
static const int WHITE_MIDI[NUM_WHITE] = {
    48, 50, 52, 53, 55, 57, 59,   /* C3 D3 E3 F3 G3 A3 B3 */
    60, 62, 64, 65, 67, 69, 71    /* C4 D4 E4 F4 G4 A4 B4 */
};

/* After which white key index does a black key appear (0-indexed): */
static const int BK_AFTER[10] = { 0, 1, 3, 4, 5,  7, 8, 10, 11, 12 };
static const int BK_MIDI[10]  = {49,51,54,56,58, 61,63, 66, 68, 70 };

static bool chord_has(StepChord *sc, int midi) {
    for (int i = 0; i < sc->count; i++)
        if (sc->notes[i] == (s8)midi) return true;
    return false;
}

static void chord_toggle(StepChord *sc, int midi) {
    /* Remove if present */
    for (int i = 0; i < sc->count; i++) {
        if (sc->notes[i] == (s8)midi) {
            for (int j = i; j < sc->count - 1; j++)
                sc->notes[j] = sc->notes[j+1];
            sc->count--;
            return;
        }
    }
    /* Add */
    if (sc->count < MAX_CHORD) {
        sc->notes[sc->count++] = (s8)midi;
    } else {
        /* shift oldest out */
        sc->notes[0] = sc->notes[1];
        sc->notes[1] = sc->notes[2];
        sc->notes[2] = (s8)midi;
    }
}

static void draw_note_picker(void) {
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    memset(fb, 0x18, (size_t)(BOTTOM_W * BOTTOM_H * 3));

    /* Header */
    fill_rect(fb, 0, 0, BOTTOM_W, 36, 25, 25, 45);

    /* "STEP N:" label */
    {
        char hdr[20];
        snprintf(hdr, sizeof(hdr), "STEP %d", g_picker_step + 1);
        draw_str(fb, 4, 4, hdr, 200, 200, 220, 2);
    }

    /* Current chord notes */
    StepChord *sc = &synthRoll[g_picker_step];
    int nx = 130;
    for (int i = 0; i < sc->count; i++) {
        char lab[4];
        midi_to_str(sc->notes[i], lab);
        draw_str(fb, nx, 6, lab, 255, 200, 100, 2);
        nx += (int)strlen(lab) * 8 + 4;
    }
    if (sc->count == 0)
        draw_str(fb, 130, 6, "---", 120, 120, 120, 2);

    /* CLEAR + DONE buttons */
    fill_rect(fb, 196, 3, 58, 28, 80, 30, 30);
    draw_str(fb, 200, 10, "CLR", 220, 120, 120, 2);
    fill_rect(fb, 260, 3, 58, 28, 30, 80, 30);
    draw_str(fb, 264, 10, "DONE", 120, 220, 120, 2);

    /* White keys */
    for (int k = 0; k < NUM_WHITE; k++) {
        int kx = KEY_X0 + k * KEY_W;
        bool active = chord_has(sc, WHITE_MIDI[k]);
        u8 br = active ? 120 : 230;
        u8 bg_b = active ? 255 : 230;
        fill_rect(fb, kx, KEY_Y, KEY_W - 1, KEY_H, br, br, bg_b);
        /* note label at bottom of key */
        if (WHITE_MIDI[k] % 12 == 0) { /* C notes */
            char lab[4];
            midi_to_str(WHITE_MIDI[k], lab);
            draw_str(fb, kx + 2, KEY_Y + KEY_H - 10, lab, 60, 60, 60, 1);
        }
    }

    /* Black keys (drawn on top) */
    for (int k = 0; k < 10; k++) {
        int bx = KEY_X0 + BK_AFTER[k] * KEY_W + KEY_W - BK_W / 2;
        bool active = chord_has(sc, BK_MIDI[k]);
        u8 br = active ? 60 : 20;
        u8 bg = active ? 20 : 20;
        u8 bb = active ? 120 : 20;
        fill_rect(fb, bx, KEY_Y, BK_W, BK_H, br, bg, bb);
    }

    /* Key line separators */
    for (int k = 0; k < NUM_WHITE; k++) {
        int kx = KEY_X0 + k * KEY_W + KEY_W - 1;
        fill_rect(fb, kx, KEY_Y, 1, KEY_H, 60, 60, 60);
    }
}

/* ─────────────────── Synth edit draw ─────────────────── */

static const char *OSC_NAMES[5] = { "SIN", "SAW", "SQR", "TRI", "NOI" };

static void draw_synth_edit(void) {
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    memset(fb, 0x15, (size_t)(BOTTOM_W * BOTTOM_H * 3));

    SynthPatch *pa = &patches[g_active_patch];

    /* Tab bar */
    static const char *tabs[3] = { "OSC", "ENV", "FLT" };
    for (int t = 0; t < 3; t++) {
        int tx = t * 107;
        u8 br = (t == g_synth_tab) ? 80 : 35;
        fill_rect(fb, tx, 0, 106, 22, br, br, br + 20);
        draw_str(fb, tx + 30, 6, tabs[t], 200, 200, 240, 2);
    }
    fill_rect(fb, 0, 22, BOTTOM_W, 2, 100, 100, 180);

    int content_y = 26;

    if (g_synth_tab == 0) {
        /* ── OSC tab ── */
        draw_str(fb, 2, content_y, "OSC1", 200, 200, 200, 1);
        for (int i = 0; i < 5; i++) {
            int bx = 2 + i * 64;
            u8 sel = (pa->osc1_type == (u8)i);
            fill_rect(fb, bx, content_y + 10, 60, 30, sel?80:35, sel?80:35, sel?130:35);
            draw_str(fb, bx + 4, content_y + 18, OSC_NAMES[i], 220, 220, 220, 1);
        }

        draw_str(fb, 2, content_y + 46, "OSC2", 200, 200, 200, 1);
        for (int i = 0; i < 5; i++) {
            int bx = 2 + i * 64;
            u8 sel = (pa->osc2_type == (u8)i);
            fill_rect(fb, bx, content_y + 56, 60, 30, sel?80:35, sel?80:35, sel?130:35);
            draw_str(fb, bx + 4, content_y + 64, OSC_NAMES[i], 220, 220, 220, 1);
        }

        draw_str(fb, 2, content_y + 92, "DETUNE", 180, 180, 180, 1);
        draw_hslider(fb, 2, content_y + 102, 316, 22, (u8)(pa->osc2_detune + 50), 180, 140, 80);

        draw_str(fb, 2, content_y + 130, "MIX  OSC1", 180, 180, 180, 1);
        draw_str(fb, 240, content_y + 130, "OSC2", 180, 180, 180, 1);
        draw_hslider(fb, 2, content_y + 140, 316, 22, pa->osc_mix, 80, 140, 220);

    } else if (g_synth_tab == 1) {
        /* ── ENV tab ── */
        static const char *elabels[4] = { "A", "D", "S", "R" };
        u8 *vals[4] = { &pa->atk, &pa->dec, &pa->sus, &pa->rel };
        for (int i = 0; i < 4; i++) {
            int vx = 16 + i * 78;
            draw_str(fb, vx + 22, content_y, elabels[i], 200, 200, 200, 2);
            draw_vslider(fb, vx, content_y + 20, 60, 160, *vals[i], 100, 180, 255);
            /* value readout */
            char vstr[4];
            snprintf(vstr, sizeof(vstr), "%d", *vals[i] * 100 / 255);
            draw_str(fb, vx + 16, content_y + 184, vstr, 180, 180, 180, 1);
        }

    } else {
        /* ── FLT tab ── */
        draw_str(fb, 2, content_y,      "CUTOFF",    180, 180, 180, 1);
        draw_hslider(fb, 2, content_y + 10,  316, 28, pa->cut,       100, 200, 255);

        draw_str(fb, 2, content_y + 44, "RESONANCE", 180, 180, 180, 1);
        draw_hslider(fb, 2, content_y + 54, 316, 28, pa->res,       200, 100, 180);

        draw_str(fb, 2, content_y + 88, "LFO RATE",  180, 180, 180, 1);
        draw_hslider(fb, 2, content_y + 98, 316, 28, pa->lfo_rate,  100, 220, 140);

        draw_str(fb, 2, content_y +132, "LFO DEPTH", 180, 180, 180, 1);
        draw_hslider(fb, 2, content_y +142, 316, 28, pa->lfo_depth, 100, 220, 140);
    }
}

/* ─────────────────── Top screen ─────────────────── */

static const char *view_names[3] = { "SEQ", "NOTE PICK", "SYNTH EDIT" };
static const char *tab_names[3]  = { "OSC", "ENV", "FLT" };

static void draw_top_screen(void) {
    printf("\x1b[2;1H");
    printf("  SpectreDAW  v0.3          View: %-10s\n\n",
           view_names[g_view]);
    printf("  BPM    : %3d     Status : %-8s   Step: %2d/%d\n",
           g_bpm, g_playing ? "PLAYING" : "STOPPED", g_currentStep + 1, NUM_STEPS);
    printf("  Patch  : %d  %-14s\n\n", g_active_patch + 1, patches[g_active_patch].name);

    if (g_view == VIEW_SEQ) {
        printf("  [A] play/stop   [Y] synth edit   [L/R] patch\n");
        printf("  [D-Up/Down] BPM    [touch] toggle / note\n");
        printf("  [START] save & exit\n");
    } else if (g_view == VIEW_NOTE_PICKER) {
        printf("  Tap piano keys to build chord (max 3 notes)\n");
        printf("  [CLR] clear step   [DONE / B] back to SEQ\n");
    } else {
        printf("  Tab: %-3s  [L/R] prev/next tab   [B] back\n",
               tab_names[g_synth_tab]);
        printf("  [touch] tap buttons / drag sliders\n");
        printf("  [A] play/stop\n");
    }
}

/* ─────────────────── Sequencer tick ─────────────────── */

static void tick_sequencer(void) {
    if (!g_playing) return;
    u64 now        = osGetTime();
    u64 step_ms    = (u64)(60000.0f / (float)g_bpm / 4.0f + 0.5f); /* 16th note */
    if (now - g_lastStepMs < step_ms) return;
    g_lastStepMs  = now;
    g_currentStep = (g_currentStep + 1) % NUM_STEPS;

    /* Drums */
    for (int t = 0; t < NUM_DRUM_TRACKS; t++)
        if (drumGrid[t][g_currentStep]) trigger_drum(t);

    /* Synth voices */
    StepChord *sc = &synthRoll[g_currentStep];
    u64 note_off  = now + step_ms * 9 / 10;
    for (int v = 0; v < NUM_VOICES; v++) {
        if (v < sc->count && sc->notes[v] >= 0)
            trigger_voice(v, sc->notes[v], note_off);
    }
}

/* ─────────────────── Input handlers ─────────────────── */

static void handle_input_seq(u32 k_down) {
    if (k_down & KEY_DUP)   { g_bpm += BPM_STEP; if (g_bpm > BPM_MAX) g_bpm = BPM_MAX; }
    if (k_down & KEY_DDOWN) { g_bpm -= BPM_STEP; if (g_bpm < BPM_MIN) g_bpm = BPM_MIN; }

    if (k_down & KEY_L) { g_active_patch = (g_active_patch + NUM_PATCHES - 1) % NUM_PATCHES; }
    if (k_down & KEY_R) { g_active_patch = (g_active_patch + 1) % NUM_PATCHES; }

    if (k_down & KEY_Y) { g_view = VIEW_SYNTH_EDIT; return; }

    if (k_down & KEY_A) {
        g_playing = !g_playing;
        if (g_playing) {
            g_currentStep = 0;
            g_lastStepMs  = osGetTime();
            for (int t = 0; t < NUM_DRUM_TRACKS; t++)
                if (drumGrid[t][g_currentStep]) trigger_drum(t);
            StepChord *sc = &synthRoll[g_currentStep];
            u64 off = g_lastStepMs + (u64)(60000.0f / (float)g_bpm / 4.0f * 0.9f);
            for (int v = 0; v < NUM_VOICES && v < sc->count; v++)
                if (sc->notes[v] >= 0) trigger_voice(v, sc->notes[v], off);
        } else {
            for (int t = 0; t < NUM_DRUM_TRACKS; t++) ndspChnWaveBufClear(t);
        }
    }

    if (k_down & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        int px = (int)touch.px;
        int py = (int)touch.py;

        if (px >= GRID_MARGIN_X && py >= GRID_MARGIN_Y) {
            int col = (px - GRID_MARGIN_X) / (CELL_W + CELL_HGAP);
            int row = (py - GRID_MARGIN_Y) / (CELL_H + CELL_VGAP);
            if (col >= 0 && col < NUM_STEPS && row >= 0 && row < 4) {
                if (row < NUM_DRUM_TRACKS) {
                    drumGrid[row][col] = !drumGrid[row][col];
                } else {
                    /* Open note picker for this synth step */
                    g_picker_step = col;
                    g_view = VIEW_NOTE_PICKER;
                }
            }
        }
    }
}

static void handle_input_picker(u32 k_down) {
    if (k_down & KEY_B) { g_view = VIEW_SEQ; return; }

    if (k_down & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        int px = (int)touch.px;
        int py = (int)touch.py;
        StepChord *sc = &synthRoll[g_picker_step];

        /* CLEAR button */
        if (px >= 196 && px < 254 && py >= 3 && py < 31) {
            sc->count = 0;
            return;
        }
        /* DONE button */
        if (px >= 260 && px < 318 && py >= 3 && py < 31) {
            g_view = VIEW_SEQ;
            return;
        }

        if (py < KEY_Y) return;

        /* Check black keys first (drawn on top) */
        for (int k = 0; k < 10; k++) {
            int bx = KEY_X0 + BK_AFTER[k] * KEY_W + KEY_W - BK_W / 2;
            if (px >= bx && px < bx + BK_W && py >= KEY_Y && py < KEY_Y + BK_H) {
                chord_toggle(sc, BK_MIDI[k]);
                return;
            }
        }

        /* White keys */
        for (int k = 0; k < NUM_WHITE; k++) {
            int kx = KEY_X0 + k * KEY_W;
            if (px >= kx && px < kx + KEY_W - 1 && py >= KEY_Y && py < KEY_Y + KEY_H) {
                chord_toggle(sc, WHITE_MIDI[k]);
                return;
            }
        }
    }
}

static void handle_input_synth(u32 k_down, u32 k_held) {
    if (k_down & KEY_B) { g_view = VIEW_SEQ; return; }
    if (k_down & KEY_L) g_synth_tab = (g_synth_tab + 2) % 3;
    if (k_down & KEY_R) g_synth_tab = (g_synth_tab + 1) % 3;

    if (k_down & KEY_DUP)   { g_bpm += BPM_STEP; if (g_bpm > BPM_MAX) g_bpm = BPM_MAX; }
    if (k_down & KEY_DDOWN) { g_bpm -= BPM_STEP; if (g_bpm < BPM_MIN) g_bpm = BPM_MIN; }

    if (k_down & KEY_A) {
        g_playing = !g_playing;
        if (g_playing) {
            g_currentStep = 0;
            g_lastStepMs  = osGetTime();
        } else {
            for (int t = 0; t < NUM_DRUM_TRACKS; t++) ndspChnWaveBufClear(t);
        }
    }

    SynthPatch *pa = &patches[g_active_patch];
    int content_y = 26;

    /* Touch: tab switching on tab bar */
    if (k_down & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        int px = (int)touch.px;
        int py = (int)touch.py;

        if (py < 22) {
            g_synth_tab = px / 107;
            if (g_synth_tab > 2) g_synth_tab = 2;
            return;
        }

        if (g_synth_tab == 0) {
            /* OSC1 buttons */
            if (py >= content_y + 10 && py < content_y + 40) {
                int idx = (px - 2) / 64;
                if (idx >= 0 && idx < 5) pa->osc1_type = (u8)idx;
            }
            /* OSC2 buttons */
            if (py >= content_y + 56 && py < content_y + 86) {
                int idx = (px - 2) / 64;
                if (idx >= 0 && idx < 5) pa->osc2_type = (u8)idx;
            }
        }
    }

    /* Slider drag (held touch) */
    if (k_held & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        int px = (int)touch.px;
        int py = (int)touch.py;

        if (py < 22) return; /* tab bar, handled above */

        if (g_synth_tab == 0) {
            /* Detune slider: y = content_y+102..+124, x = 2..318 */
            if (py >= content_y + 102 && py < content_y + 124 && px >= 2 && px < 318)
                pa->osc2_detune = (s8)((px - 2) * 100 / 315 - 50);
            /* Mix slider: y = content_y+140..+162 */
            if (py >= content_y + 140 && py < content_y + 162 && px >= 2 && px < 318)
                pa->osc_mix = (u8)((px - 2) * 255 / 315);

        } else if (g_synth_tab == 1) {
            /* Vertical sliders A/D/S/R */
            u8 *vals[4] = { &pa->atk, &pa->dec, &pa->sus, &pa->rel };
            for (int i = 0; i < 4; i++) {
                int vx = 16 + i * 78;
                int top = content_y + 20;
                int bot = top + 160;
                if (px >= vx && px < vx + 60 && py >= top && py < bot) {
                    int raw = (bot - py) * 255 / 159;
                    if (raw < 0) raw = 0;
                    if (raw > 255) raw = 255;
                    *vals[i] = (u8)raw;
                }
            }

        } else {
            /* FLT sliders */
            if (py >= content_y + 10 && py < content_y + 38 && px >= 2 && px < 318)
                pa->cut = (u8)((px - 2) * 255 / 315);
            if (py >= content_y + 54 && py < content_y + 82 && px >= 2 && px < 318)
                pa->res = (u8)((px - 2) * 255 / 315);
            if (py >= content_y + 98 && py < content_y + 126 && px >= 2 && px < 318)
                pa->lfo_rate = (u8)((px - 2) * 255 / 315);
            if (py >= content_y + 142 && py < content_y + 170 && px >= 2 && px < 318)
                pa->lfo_depth = (u8)((px - 2) * 255 / 315);
        }
    }
}

/* ─────────────────── Entry point ─────────────────── */

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    srand((unsigned)osGetTime());

    /* Precompute MIDI note frequencies */
    for (int n = 0; n < 128; n++)
        g_note_freq[n] = 440.0f * powf(2.0f, (float)(n - 69) / 12.0f);

    /* Default patches */
    memcpy(patches, DEFAULT_PATCHES, sizeof(patches));

    /* Zero out pattern */
    memset(drumGrid, 0, sizeof(drumGrid));
    memset(synthRoll, 0, sizeof(synthRoll));
    for (int s = 0; s < NUM_STEPS; s++) {
        synthRoll[s].count = 0;
        for (int j = 0; j < MAX_CHORD; j++) synthRoll[s].notes[j] = -1;
    }

    /* SD card is auto-mounted by Homebrew Launcher — no sdmcInit needed */
    load_from_sd();

    /* Audio */
    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    init_audio();

    while (aptMainLoop()) {
        hidScanInput();
        u32 k_down = hidKeysDown();
        u32 k_held = hidKeysHeld();

        if (k_down & KEY_START) break;

        switch (g_view) {
            case VIEW_SEQ:
                handle_input_seq(k_down);
                tick_sequencer();
                draw_top_screen();
                draw_seq();
                break;
            case VIEW_NOTE_PICKER:
                handle_input_picker(k_down);
                draw_top_screen();
                draw_note_picker();
                break;
            case VIEW_SYNTH_EDIT:
                handle_input_synth(k_down, k_held);
                tick_sequencer();
                draw_top_screen();
                draw_synth_edit();
                break;
        }

        update_synth_streaming();

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    save_to_sd();
    cleanup_audio();
    gfxExit();
    return 0;
}
