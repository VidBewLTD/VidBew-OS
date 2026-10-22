#ifndef SB16_H
#define SB16_H
#define SB16_BUFFER_CAPACITY 65536
int sb16_init(void);
int sb16_present(void);
int sb16_get_volume(void);
void sb16_set_volume(int pct);   /* 0-100 */
void sb16_play_u8_mono(const unsigned char *data, unsigned int length, unsigned int sample_rate);
#endif
