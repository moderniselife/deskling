// Deskling's voice: a handful of tiny synthesized chirps, no audio files.
// Pure C so the same code runs on the board, in the simulator, and renders the
// WAVs the website plays (firmware/tools/render_sounds).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DK_SOUND_RATE 24000

typedef enum {
    DK_SND_NONE = 0,
    DK_SND_HELLO,      // boot, setup finished
    DK_SND_TAP,        // any press
    DK_SND_PERMISSION, // "hey, I need you!"
    DK_SND_QUESTION,   // "hmm?"
    DK_SND_DONE,       // an agent finished
    DK_SND_REMINDER,   // ding-dong
    DK_SND_MESSAGE,    // a reply arrived
    DK_SND_CHEER,      // reminder ticked off
    DK_SND_SLEEPY,     // drifting off to the night clock
    DK_SND_ALLOW,      // you tapped Allow / picked an answer
    DK_SND_DENY,       // you tapped Deny
    DK_SND_OOPS,       // something didn't work
    DK_SND_COUNT,
} dk_sound_t;

/** Short lowercase name, e.g. "permission" (used for WAV file names). */
const char *sound_name(dk_sound_t s);
/** Length in samples at DK_SOUND_RATE. */
int sound_length(dk_sound_t s);
/** Render samples [start, start+n) of a sound, mono int16. Past the end is silence. */
void sound_render(dk_sound_t s, int start, int n, int16_t *out);

#ifdef __cplusplus
}
#endif
