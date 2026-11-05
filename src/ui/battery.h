/* battery.h -- what the header says about the battery.
 *
 * Reads /sys/class/power_supply the way the menu does: the first entry whose `type` is "Battery" holds the
 * percentage, and ANY entry with `online` == 1 means on the cable -- the rk817 gauge reports "Discharging"
 * while charging, so its own status is not to be trusted. No battery at all (a different board, or mains) is
 * have == false, and the header then draws no percentage.
 */
#ifndef BANANATUBE_BATTERY_H
#define BANANATUBE_BATTERY_H

#include <stdbool.h>

typedef struct {
    bool have;          /* there is a battery to report */
    int  percent;       /* 0..100, or 100 when unknown */
    bool charging;      /* on the cable */
} Battery;

/* Reads `sysdir` (<sys>/class/power_supply) now. Never fails: no battery is have == false. */
void battery_read(Battery *b, const char *sysdir);

/* The same, at most once every `secs` seconds: the header draws every frame and this is a few file reads.
 * Returns whether it re-read; `*b` is untouched when it did not. */
bool battery_get(Battery *b, const char *sysdir, double now_s, double secs);

#endif /* BANANATUBE_BATTERY_H */
