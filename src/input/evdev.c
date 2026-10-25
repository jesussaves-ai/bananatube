/* evdev.c */
#include "input/evdev.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static bool bit_set(const unsigned long *bits, int n)
{
    return (bits[n / (int)(8 * sizeof(long))] >> (n % (int)(8 * sizeof(long)))) & 1ul;
}

/* Does this device look like the bottom screen's touchscreen? */
static bool probe_touch(int fd)
{
    unsigned long abs[16] = { 0 }, keys[16] = { 0 };
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs) < 0) return false;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0) return false;
    if (bit_set(abs, ABS_MT_POSITION_X) && bit_set(abs, ABS_MT_POSITION_Y)) return true;
    /* single-touch with a button, or QEMU's absolute tablet (BTN_LEFT, and not a pad's stick) */
    return bit_set(abs, ABS_X) && bit_set(abs, ABS_Y) &&
           (bit_set(keys, BTN_TOUCH) || (bit_set(keys, BTN_LEFT) && !bit_set(keys, BTN_SOUTH)));
}

static bool probe_gamepad(int fd)
{
    unsigned long keys[16] = { 0 };
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0) return false;
    return bit_set(keys, BTN_SOUTH) || bit_set(keys, BTN_EAST) ||
           bit_set(keys, BTN_DPAD_UP) || bit_set(keys, BTN_START) || bit_set(keys, BTN_SELECT);
}

static void read_ranges(int fd, InputCtx *in)
{
    struct input_absinfo ai;
    if (ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0) { in->tx_min = ai.minimum; in->tx_max = ai.maximum; }
    if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0) { in->ty_min = ai.minimum; in->ty_max = ai.maximum; }
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0) { in->tx_min = ai.minimum; in->tx_max = ai.maximum; }
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0) { in->ty_min = ai.minimum; in->ty_max = ai.maximum; }
}

void input_init(InputCtx *in, const char *touch_hint)
{
    memset(in, 0, sizeof *in);
    in->tx_max = 640;
    in->ty_max = 480;

    DIR *d = opendir("/dev/input");
    if (!d) { log_line("no /dev/input"); return; }
    struct dirent *e;
    while ((e = readdir(d)) && in->n < INPUT_MAX_DEV) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[300];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        char name[80] = "";
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);

        bool touch = probe_touch(fd);
        bool pad = probe_gamepad(fd);
        /* With two touch-capable devices (a panel and a tablet), BANANA_TOUCH says which is the bottom one:
         * it is part of that device's sysfs path. */
        if (touch && touch_hint && *touch_hint) {
            char link[512] = "";
            char sys[320];
            snprintf(sys, sizeof sys, "/sys/class/input/%s", e->d_name);
            if (readlink(sys, link, sizeof link - 1) > 0 && !str_contains(link, touch_hint)) {
                log_line("input: %s is not the bottom touchscreen (%s)", e->d_name, touch_hint);
                touch = false;
            }
        }
        if (!touch && !pad) {
            unsigned long sw[16] = { 0 };
            bool lid = ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw) >= 0 && bit_set(sw, SW_LID);
            if (!lid) { close(fd); continue; }        /* keys we do not use (volume, power: bananad's) */
        }

        in->fd[in->n] = fd;
        in->is_touch[in->n] = touch;
        in->is_gamepad[in->n] = pad;
        if (touch) read_ranges(fd, in);
        log_line("input: %s (%s%s%s)", name[0] ? name : e->d_name, touch ? "touch" : "keys",
                 pad ? " gamepad" : "", touch ? "" : "");
        in->n++;
    }
    closedir(d);
}

void input_close(InputCtx *in)
{
    for (int i = 0; i < in->n; i++) if (in->fd[i] >= 0) close(in->fd[i]);
    in->n = 0;
}

int input_pollfds(InputCtx *in, struct pollfd *pfds, int max)
{
    int added = 0;
    for (int i = 0; i < in->n && added < max; i++) {
        pfds[added].fd = in->fd[i];
        pfds[added].events = POLLIN;
        added++;
    }
    return added;
}

static int axis_to_screen(int v, int lo, int hi, int span)
{
    if (hi <= lo) return 0;
    int r = (v - lo) * span / (hi - lo);
    return clampi(r, 0, span - 1);
}

int input_dispatch(InputCtx *in, struct pollfd *pfds, KeyEvent *keys, int key_max, TouchEvent *touch)
{
    int nkeys = 0;

    for (int i = 0; i < in->n; i++) {
        if (!(pfds[i].revents & POLLIN)) continue;
        struct input_event ev;
        while (read(in->fd[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
            if (ev.type == EV_KEY) {
                BananaKey k = key_from_code(ev.code);
                if (k != BK_NONE && nkeys < key_max) {
                    keys[nkeys].key = k;
                    keys[nkeys].down = ev.value != 0;
                    keys[nkeys].repeat = ev.value == 2;
                    nkeys++;
                } else if (k == BK_NONE && in->is_touch[i] &&
                           (ev.code == BTN_TOUCH || ev.code == BTN_LEFT)) {
                    if (touch) touch->down = ev.value != 0;
                }
            } else if (ev.type == EV_ABS) {
                if (in->is_touch[i]) {
                    if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) {
                        if (touch) touch->x = axis_to_screen(ev.value, in->tx_min, in->tx_max, SCREEN_W);
                    } else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) {
                        if (touch) touch->y = axis_to_screen(ev.value, in->ty_min, in->ty_max, SCREEN_H);
                    }
                } else if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) {
                    /* a USB pad's hat: report it as a D-pad press */
                    if (nkeys < key_max && ev.value != 0) {
                        BananaKey k = ev.code == ABS_HAT0X ? (ev.value > 0 ? BK_RIGHT : BK_LEFT)
                                                           : (ev.value > 0 ? BK_DOWN : BK_UP);
                        keys[nkeys].key = k; keys[nkeys].down = true; keys[nkeys].repeat = false;
                        nkeys++;
                    }
                }
            } else if (ev.type == EV_SW && ev.code == SW_LID) {
                in->lid_closed = ev.value != 0;
                log_line("lid %s", in->lid_closed ? "closed" : "opened");
            }
        }
    }
    return nkeys;
}
