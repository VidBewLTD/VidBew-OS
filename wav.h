#ifndef WAV_H
#define WAV_H
typedef struct {
    unsigned int sample_rate;
    unsigned short channels;
    unsigned short bits_per_sample;
    const unsigned char *data;
    unsigned int data_size;
} wav_info_t;
int wav_parse(const unsigned char *buf, unsigned int buf_size, wav_info_t *out);
unsigned int wav_to_u8_mono(const wav_info_t *wav, unsigned char *out, unsigned int out_capacity);
#endif
