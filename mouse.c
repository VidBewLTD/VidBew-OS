/* mouse.c -- PS/2 mouse driver.
 *
 * Protocol: the 8042 keyboard controller has a second ("auxiliary") port for
 * a mouse. Once enabled, it sends 3-byte packets on IRQ12:
 *   byte0: bit0=left button, bit1=right button, bit2=middle button,
 *          bit3=always 1 (sync check), bit4=X sign, bit5=Y sign
 *   byte1: X movement delta
 *   byte2: Y movement delta (screen Y is inverted vs. the packet's Y)
 *
 * IRQ12 is on the *slave* PIC, so its handler must send an EOI to both the
 * slave (0xA0) and the master (0x20). Cursor drawing now lives in gfx.c --
 * this file only produces position/button events. */

#include "mouse.h"
#include "io.h"
#include "input_mode.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

volatile int mouse_x = 320;
volatile int mouse_y = 240;

static volatile int buttons = 0;
static int bound_w = 640, bound_h = 480;
static int speed_pct = 100;
static int acc_x = 0, acc_y = 0;

#define MQ_SIZE 128
static mouse_event_t mq[MQ_SIZE];
static volatile int mq_head = 0, mq_tail = 0;

static unsigned char packet_cycle = 0;
static unsigned char packet[3];

static void mq_push(void)
{
    int next = (mq_head + 1) % MQ_SIZE;
    if (next == mq_tail) {
        /* queue full: coalesce into the newest entry instead of dropping the
         * latest button state */
        int last = (mq_head + MQ_SIZE - 1) % MQ_SIZE;
        mq[last].x = mouse_x; mq[last].y = mouse_y; mq[last].buttons = buttons;
        return;
    }
    mq[mq_head].x = mouse_x;
    mq[mq_head].y = mouse_y;
    mq[mq_head].buttons = buttons;
    mq_head = next;
}

int mouse_get_event(mouse_event_t *ev)
{
    if (mq_head == mq_tail) return 0;
    *ev = mq[mq_tail];
    mq_tail = (mq_tail + 1) % MQ_SIZE;
    return 1;
}

static void clamp(void)
{
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > bound_w - 1) mouse_x = bound_w - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > bound_h - 1) mouse_y = bound_h - 1;
}

void mouse_set_bounds(int w, int h)
{
    bound_w = w;
    bound_h = h;
    clamp();
}

void mouse_set_speed(int percent)
{
    if (percent < 50) percent = 50;
    if (percent > 300) percent = 300;
    speed_pct = percent;
}

int mouse_get_speed(void) { return speed_pct; }

void mouse_inject_move(int dx, int dy)
{
    mouse_x += dx;
    mouse_y += dy;
    clamp();
    mq_push();
}

void mouse_inject_click(void)
{
    int saved = buttons;
    buttons = saved | 1;
    mq_push();
    buttons = saved & ~1;
    mq_push();
}

static void ps2_wait_input(void)
{
    int timeout = 100000;
    while (timeout-- && (inb(PS2_STATUS) & 0x02));
}

/* Returns 1 if data became available, 0 on timeout (no device responding). */
static int ps2_wait_output(void)
{
    int timeout = 100000;
    while (timeout-- && !(inb(PS2_STATUS) & 0x01));
    return timeout != 0;
}

static void mouse_write(unsigned char data)
{
    ps2_wait_input();
    outb(PS2_CMD, 0xD4); /* "next byte goes to the mouse" */
    ps2_wait_input();
    outb(PS2_DATA, data);
}

static int mouse_read_ack(unsigned char *out)
{
    if (!ps2_wait_output()) return 0;
    *out = inb(PS2_DATA);
    return 1;
}

int mouse_init(void)
{
    /* throw away anything the firmware left in the controller's output buffer */
    for (int i = 0; i < 16 && (inb(PS2_STATUS) & 0x01); i++) (void)inb(PS2_DATA);

    ps2_wait_input();
    outb(PS2_CMD, 0xA8); /* enable auxiliary (mouse) port */

    ps2_wait_input();
    outb(PS2_CMD, 0x20); /* read controller configuration byte */
    if (!ps2_wait_output()) return 0;
    unsigned char cfg = inb(PS2_DATA);
    cfg |= 0x02;   /* enable IRQ12 */
    cfg &= ~0x20;  /* enable the mouse's clock line */

    ps2_wait_input();
    outb(PS2_CMD, 0x60); /* write controller configuration byte */
    ps2_wait_input();
    outb(PS2_DATA, cfg);

    unsigned char ack;
    mouse_write(0xF6); /* set defaults */
    if (!mouse_read_ack(&ack) || ack != 0xFA) return 0;

    mouse_write(0xF4); /* enable data reporting */
    if (!mouse_read_ack(&ack) || ack != 0xFA) return 0;

    packet_cycle = 0;
    return 1; /* a real mouse answered both commands correctly */
}

void mouse_handler_c(void)
{
    unsigned char data = inb(PS2_DATA);

    if (packet_cycle == 0 && (!(data & 0x08) || data == 0xFA || data == 0xAA)) {
        /* out of sync with the packet stream -- drop until we see a byte
         * with the sync bit set, rather than misreading forever. 0xFA/0xAA
         * are command ACK / self-test bytes that can trail an init
         * sequence; they look like status bytes but are never real ones. */
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
        return;
    }

    packet[packet_cycle] = data;
    packet_cycle++;

    if (packet_cycle == 3) {
        packet_cycle = 0;

        int dx = (signed char)packet[1];
        int dy = (signed char)packet[2];

        buttons = packet[0] & 0x07;

        /* speed scaling with a remainder so slow settings still move */
        acc_x += dx * speed_pct;
        acc_y += dy * speed_pct;
        int sx = acc_x / 100, sy = acc_y / 100;
        acc_x -= sx * 100;
        acc_y -= sy * 100;

        mouse_x += sx;
        mouse_y -= sy; /* PS/2 Y is inverted relative to screen coordinates */
        clamp();

        mq_push();
        input_mode_notify_mouse();
    }

    outb(0xA0, 0x20); /* EOI to slave PIC */
    outb(0x20, 0x20); /* EOI to master PIC (this IRQ cascades through it) */
}
