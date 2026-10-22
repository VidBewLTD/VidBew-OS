#include "audio.h"
#include "fat16.h"
#include "sb16.h"
#include "sync.h"
#include "heap.h"

#define FILE_BUF_SIZE 131072

static unsigned char *file_buf = 0;
static unsigned char *pcm_buf = 0;
static mutex_t audio_lock;

void audio_init(void)
{
    mutex_init(&audio_lock);
}

int audio_play_file(const char *name, wav_info_t *info_out, unsigned int *pcm_len_out)
{
    if (!sb16_present()) return 3;
    mutex_lock(&audio_lock);
    if (!file_buf) file_buf = (unsigned char *)kmalloc(FILE_BUF_SIZE);
    if (!pcm_buf) pcm_buf = (unsigned char *)kmalloc(SB16_BUFFER_CAPACITY);
    if (!file_buf || !pcm_buf) { mutex_unlock(&audio_lock); return 1; }
    wav_info_t info;
    unsigned int size = 0, pcm_len = 0;
    int rc = 0;
    if (!fat16_read_file(name, file_buf, FILE_BUF_SIZE, &size)) rc = 1;
    else if (!wav_parse(file_buf, size, &info)) rc = 2;
    else {
        pcm_len = wav_to_u8_mono(&info, pcm_buf, SB16_BUFFER_CAPACITY);
        if (info_out) *info_out = info;
        if (pcm_len_out) *pcm_len_out = pcm_len;
        sb16_play_u8_mono(pcm_buf, pcm_len, info.sample_rate);
    }
    mutex_unlock(&audio_lock);
    return rc;
}
