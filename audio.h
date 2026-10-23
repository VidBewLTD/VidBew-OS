#ifndef AUDIO_H
#define AUDIO_H
#include "wav.h"
/* Shared WAV loader/player so the Terminal, Explorer and Control Panel use one
 * set of buffers. Several windows may ask for sound at once, so
 * audio_play_file() holds a lock for the whole load+play. Returns 0 = ok,
 * 1 = file not found, 2 = not a WAV Heroine understands, 3 = no sound card. */
int audio_play_file(const char *name, wav_info_t *info_out, unsigned int *pcm_len_out);
void audio_play_chime(void);
void audio_init(void);
#endif
