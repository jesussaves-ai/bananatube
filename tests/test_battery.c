/* The header's battery: the percentage and whether it is on the cable. The gauge lies about `status` while
 * charging, so "on the cable" is any supply saying online -- the same rule the menu uses. */
#include "ui/battery.h"
#include "test.h"

int main(void)
{
    Battery b;

    /* the fixture: 67%, and the charger says online, so it is charging even though status says Discharging */
    memset(&b, 0, sizeof b);
    battery_read(&b, TEST_DATA_DIR "/power_supply");
    check(b.have);
    check_int(b.percent, 67);
    check(b.charging);

    /* nothing but the battery: still 67%, not charging */
    memset(&b, 0, sizeof b);
    battery_read(&b, TEST_DATA_DIR "/power_supply_empty");
    check(!b.have);
    check_int(b.percent, 100);
    check(!b.charging);

    /* the cache: the second call inside the window does not read again */
    b.have = false;
    check(battery_get(&b, TEST_DATA_DIR "/power_supply", 100.0, 2.0));
    check(b.have);
    b.percent = 1;                                     /* scribble on it */
    check(!battery_get(&b, TEST_DATA_DIR "/power_supply", 101.0, 2.0));
    check_int(b.percent, 1);                           /* it was left alone */
    check(battery_get(&b, TEST_DATA_DIR "/power_supply", 103.0, 2.0));
    check_int(b.percent, 67);

    test_done();
}
