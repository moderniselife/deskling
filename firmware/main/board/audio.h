// Speaker: ES8311 codec + amplifier, fed over I2S. Plays Deskling's synthesized sounds.
#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "sound.h"

/** Set up the codec on the shared I2C bus and start the playback task. Safe to call with NULL (no sound). */
void audio_init(i2c_master_bus_handle_t i2c);
/** Queue a sound. A new sound interrupts whatever is playing. Returns straight away. */
void audio_play(dk_sound_t s);
/** 0..100; 0 mutes. */
void audio_set_volume(uint8_t percent);
