#include "sound.h"

#include <math.h>
#include <stddef.h>

// Every sound is a short list of notes. A note is a little voice with a pitch glide,
// a soft attack and a decay. Rendering is stateless per sample, so the board can
// stream a sound in small chunks without keeping oscillator state.

typedef enum {
    W_SOFT, // sine with a touch of overtones: the chirpy "voice"
    W_SINE, // pure and round
    W_BELL, // inharmonic partials with a fast-decaying shimmer
    W_TRI,  // a little 8-bit
} wave_t;

typedef struct {
    uint16_t at;    // ms from the start of the sound
    uint16_t dur;   // ms
    float f0, f1;   // Hz: glide from f0 to f1 over `glide` ms (f1 = 0 means no glide)
    uint16_t glide; // ms, 0 = over the whole note
    wave_t wave;
    float vol;      // 0..1
    float decay;    // ms time constant of the exponential decay, 0 = none
    float vib;      // vibrato depth in semitones (5.5 Hz)
} note_t;

typedef struct {
    const char *name;
    const note_t *notes;
    int n;
} sound_def_t;

// Pitches
#define C5 523.25f
#define D5 587.33f
#define E5 659.25f
#define F5 698.46f
#define G5 783.99f
#define A5 880.00f
#define B5 987.77f
#define C6 1046.50f
#define D6 1174.66f
#define E6 1318.51f
#define G6 1567.98f
#define A6 1760.00f
#define B6 1975.53f
#define C7 2093.00f
#define E7 2637.02f
#define G7 3135.96f

static const note_t HELLO[] = {
    {0, 110, G5, C6, 70, W_SOFT, 0.75f, 160, 0},
    {120, 100, E6, 0, 0, W_SOFT, 0.70f, 140, 0},
    {225, 260, G6, C7, 90, W_SOFT, 0.75f, 180, 0.15f},
};
static const note_t TAP[] = {
    {0, 32, 1500, 1100, 0, W_SINE, 0.32f, 14, 0},
};
static const note_t PERMISSION[] = {
    {0, 110, A5, E6, 60, W_SOFT, 0.85f, 120, 0},
    {150, 110, A5, E6, 60, W_SOFT, 0.85f, 120, 0},
    {320, 300, B6, 0, 0, W_BELL, 0.70f, 170, 0},
};
static const note_t QUESTION[] = {
    {0, 230, E5, A5, 0, W_SOFT, 0.70f, 0, 0.25f},
    {270, 170, C6, G6, 0, W_SOFT, 0.75f, 200, 0},
};
static const note_t DONE[] = {
    {0, 130, C6, 0, 0, W_SOFT, 0.65f, 110, 0},
    {95, 130, E6, 0, 0, W_SOFT, 0.65f, 110, 0},
    {190, 130, G6, 0, 0, W_SOFT, 0.65f, 110, 0},
    {285, 520, C7, 0, 0, W_BELL, 0.70f, 240, 0},
};
static const note_t REMINDER[] = {
    {0, 220, G7, 0, 0, W_SINE, 0.12f, 60, 0},
    {0, 800, E6, 0, 0, W_BELL, 0.80f, 320, 0},
    {380, 1000, C6, 0, 0, W_BELL, 0.80f, 380, 0},
};
static const note_t MESSAGE[] = {
    {0, 70, 900, 1500, 0, W_SOFT, 0.60f, 60, 0},
    {95, 85, 1200, 2000, 0, W_SOFT, 0.60f, 70, 0},
};
static const note_t CHEER[] = {
    {0, 200, C6, C7, 0, W_TRI, 0.35f, 0, 0},
    {210, 260, E7, 0, 0, W_BELL, 0.50f, 120, 0},
    {310, 260, G7, 0, 0, W_BELL, 0.40f, 120, 0},
    {420, 520, C7, 0, 0, W_BELL, 0.60f, 220, 0},
};
static const note_t SLEEPY[] = {
    {0, 650, A5, D5, 0, W_SINE, 0.45f, 0, 0.35f},
    {720, 340, F5, E5, 0, W_SINE, 0.28f, 220, 0},
};
static const note_t ALLOW[] = {
    {0, 80, G6, 0, 0, W_SOFT, 0.65f, 70, 0},
    {75, 280, C7, 0, 0, W_BELL, 0.65f, 160, 0},
};
static const note_t DENY[] = {
    {0, 100, E5, 0, 0, W_SINE, 0.55f, 90, 0},
    {120, 200, C5, 0, 0, W_SINE, 0.55f, 140, 0},
};
static const note_t OOPS[] = {
    {0, 220, 1396.91f, C6, 0, W_SOFT, 0.60f, 0, 0},
    {260, 280, A5, E5, 0, W_SOFT, 0.55f, 260, 0.2f},
};

#define DEF(name, arr) {name, arr, (int)(sizeof(arr) / sizeof(arr[0]))}
static const sound_def_t SOUNDS[DK_SND_COUNT] = {
    [DK_SND_NONE] = {"none", NULL, 0},
    [DK_SND_HELLO] = DEF("hello", HELLO),
    [DK_SND_TAP] = DEF("tap", TAP),
    [DK_SND_PERMISSION] = DEF("permission", PERMISSION),
    [DK_SND_QUESTION] = DEF("question", QUESTION),
    [DK_SND_DONE] = DEF("done", DONE),
    [DK_SND_REMINDER] = DEF("reminder", REMINDER),
    [DK_SND_MESSAGE] = DEF("message", MESSAGE),
    [DK_SND_CHEER] = DEF("cheer", CHEER),
    [DK_SND_SLEEPY] = DEF("sleepy", SLEEPY),
    [DK_SND_ALLOW] = DEF("allow", ALLOW),
    [DK_SND_DENY] = DEF("deny", DENY),
    [DK_SND_OOPS] = DEF("oops", OOPS),
};

#define TWO_PI 6.28318531f
#define ATTACK_MS 6.0f
#define RELEASE_MS 14.0f
#define MASTER 0.95f // loud enough for a small speaker, still under full scale

const char *sound_name(dk_sound_t s)
{
    return (s > DK_SND_NONE && s < DK_SND_COUNT) ? SOUNDS[s].name : "none";
}

int sound_length(dk_sound_t s)
{
    if (s <= DK_SND_NONE || s >= DK_SND_COUNT) return 0;
    int end = 0;
    for (int i = 0; i < SOUNDS[s].n; i++) {
        int e = SOUNDS[s].notes[i].at + SOUNDS[s].notes[i].dur;
        if (e > end) end = e;
    }
    return end * DK_SOUND_RATE / 1000;
}

/** Oscillator phase in cycles at time t (seconds) into the note: the integral of its frequency. */
static float phase_at(const note_t *n, float t)
{
    if (n->f1 <= 0) return n->f0 * t;
    float g = (n->glide ? n->glide : n->dur) / 1000.0f;
    if (t < g) return n->f0 * t + (n->f1 - n->f0) * t * t / (2 * g);
    return n->f0 * g + (n->f1 - n->f0) * g / 2 + n->f1 * (t - g);
}

static float voice(const note_t *n, float t_ms)
{
    float t = t_ms / 1000.0f;
    float ph = phase_at(n, t);
    if (n->vib > 0) {
        // Vibrato as a small phase wobble (keeps the phase continuous).
        float f = n->f1 > 0 ? n->f1 : n->f0;
        ph += f * (powf(2.0f, n->vib / 12.0f) - 1.0f) * sinf(TWO_PI * 5.5f * t) / (TWO_PI * 5.5f);
    }
    float x = TWO_PI * ph;
    float s;
    switch (n->wave) {
    case W_SINE:
        s = sinf(x);
        break;
    case W_BELL: {
        // Partials above ~9 kHz would alias at 24 kHz, so high notes get fewer of them.
        float f = n->f1 > 0 ? (n->f0 > n->f1 ? n->f0 : n->f1) : n->f0;
        float shimmer = expf(-t_ms / 90.0f);
        s = sinf(x);
        if (f * 2.76f < 9000) s += 0.45f * shimmer * sinf(2.76f * x);
        if (f * 5.4f < 9000) s += 0.2f * shimmer * sinf(5.4f * x);
        s *= 0.7f;
        break;
    }
    case W_TRI: {
        // Band-limited triangle: odd harmonics at 1/k^2, only those under ~9 kHz.
        float f = n->f1 > n->f0 ? n->f1 : n->f0;
        s = 0;
        float sign = 1;
        for (int k = 1; k <= 9 && f * k < 9000; k += 2, sign = -sign) s += sign * sinf(k * x) / (float)(k * k);
        s *= 0.81f;
        break;
    }
    case W_SOFT:
    default:
        s = (sinf(x) + 0.22f * sinf(2 * x) + 0.07f * sinf(3 * x)) * 0.8f;
        break;
    }
    float env = t_ms < ATTACK_MS ? t_ms / ATTACK_MS : 1.0f;
    if (n->decay > 0) env *= expf(-t_ms / n->decay);
    float left = n->dur - t_ms;
    if (left < RELEASE_MS) env *= left > 0 ? left / RELEASE_MS : 0;
    return s * env * n->vol;
}

void sound_render(dk_sound_t s, int start, int n, int16_t *out)
{
    const sound_def_t *d = (s > DK_SND_NONE && s < DK_SND_COUNT) ? &SOUNDS[s] : NULL;
    for (int i = 0; i < n; i++) {
        float t_ms = (float)(start + i) * 1000.0f / DK_SOUND_RATE;
        float acc = 0;
        for (int k = 0; d && k < d->n; k++) {
            const note_t *note = &d->notes[k];
            float local = t_ms - note->at;
            if (local >= 0 && local < note->dur) acc += voice(note, local);
        }
        acc *= MASTER;
        if (acc > 1) acc = 1;
        if (acc < -1) acc = -1;
        out[i] = (int16_t)(acc * 32000);
    }
}
