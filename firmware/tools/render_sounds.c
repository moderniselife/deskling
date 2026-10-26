// Render Deskling's sounds to WAV files (the website plays these).
//   cc -O2 -Imain/app tools/render_sounds.c main/app/sound_synth.c -lm -o /tmp/render_sounds
//   /tmp/render_sounds ../site/public/sounds
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound.h"

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    for (int s = DK_SND_NONE + 1; s < DK_SND_COUNT; s++) {
        int n = sound_length((dk_sound_t)s) + DK_SOUND_RATE / 50; // 20 ms of tail
        int16_t *pcm = calloc((size_t)n, sizeof(int16_t));
        sound_render((dk_sound_t)s, 0, n, pcm);
        int peak = 0;
        for (int i = 0; i < n; i++) peak = abs(pcm[i]) > peak ? abs(pcm[i]) : peak;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s.wav", dir, sound_name((dk_sound_t)s));
        FILE *f = fopen(path, "wb");
        if (!f) {
            perror(path);
            return 1;
        }
        uint32_t bytes = (uint32_t)n * 2;
        fwrite("RIFF", 1, 4, f), put32(f, 36 + bytes), fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f), put32(f, 16), put16(f, 1), put16(f, 1);
        put32(f, DK_SOUND_RATE), put32(f, DK_SOUND_RATE * 2), put16(f, 2), put16(f, 16);
        fwrite("data", 1, 4, f), put32(f, bytes), fwrite(pcm, 2, (size_t)n, f);
        fclose(f);
        printf("%-11s %5d ms  peak %3d%%\n", sound_name((dk_sound_t)s), n * 1000 / DK_SOUND_RATE, peak * 100 / 32767);
        free(pcm);
    }
    return 0;
}
