/* wav.c -- RIFF/WAVE chunk-walking parser + downconversion to 8-bit mono. */
#include "wav.h"

static unsigned int read_u32le(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static unsigned short read_u16le(const unsigned char *p) { return (unsigned short)(p[0] | (p[1] << 8)); }
static int chunk_id_is(const unsigned char *p, const char *id)
{
    return p[0] == id[0] && p[1] == id[1] && p[2] == id[2] && p[3] == id[3];
}

int wav_parse(const unsigned char *buf, unsigned int buf_size, wav_info_t *out)
{
    if (buf_size < 12) return 0;
    if (!chunk_id_is(buf, "RIFF") || !chunk_id_is(buf + 8, "WAVE")) return 0;

    unsigned int pos = 12;
    int have_fmt = 0;
    while (pos + 8 <= buf_size) {
        const unsigned char *chunk_id = buf + pos;
        unsigned int chunk_size = read_u32le(buf + pos + 4);
        unsigned int chunk_data = pos + 8;
        if (chunk_data + chunk_size > buf_size) chunk_size = buf_size - chunk_data;

        if (chunk_id_is(chunk_id, "fmt ")) {
            if (chunk_size < 16) return 0;
            out->channels = read_u16le(buf + chunk_data + 2);
            out->sample_rate = read_u32le(buf + chunk_data + 4);
            out->bits_per_sample = read_u16le(buf + chunk_data + 14);
            have_fmt = 1;
        } else if (chunk_id_is(chunk_id, "data")) {
            if (!have_fmt) return 0;
            out->data = buf + chunk_data;
            out->data_size = chunk_size;
            return 1;
        }
        pos = chunk_data + chunk_size;
        if (chunk_size & 1) pos++;
    }
    return 0;
}

unsigned int wav_to_u8_mono(const wav_info_t *wav, unsigned char *out, unsigned int out_capacity)
{
    unsigned int written = 0;
    unsigned int bytes_per_sample = wav->bits_per_sample / 8;
    unsigned int frame_size = bytes_per_sample * wav->channels;
    if (frame_size == 0) return 0;
    unsigned int frame_count = wav->data_size / frame_size;

    for (unsigned int f = 0; f < frame_count && written < out_capacity; f++) {
        const unsigned char *frame = wav->data + f * frame_size;
        int sum = 0;
        for (unsigned int c = 0; c < wav->channels; c++) {
            const unsigned char *sp = frame + c * bytes_per_sample;
            if (wav->bits_per_sample == 8) sum += (int)sp[0] - 128;
            else sum += (short)read_u16le(sp);
        }
        int avg = sum / (int)wav->channels;
        if (wav->bits_per_sample > 8) avg >>= 8;
        int u8 = avg + 128;
        if (u8 < 0) u8 = 0;
        if (u8 > 255) u8 = 255;
        out[written++] = (unsigned char)u8;
    }
    return written;
}
