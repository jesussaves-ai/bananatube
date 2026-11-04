#include "test.h"
#include "ui/theme.h"

int main(void)
{
    UiTheme t = ui_theme_with("dark", "yellow");
    check_int(t.bg, 0xff17161c);
    check_int(t.accent, 0xfff0b429);

    t = ui_theme_with("light", "blue");
    check_int(t.bg, 0xfff3efe6);
    check_int(t.accent, 0xff4a9fe0);
    check_int(t.ink, 0xff2a2620);

    t = ui_theme_with("nonsense", "nonsense");
    check_int(t.bg, 0xff17161c);
    check_int(t.accent, 0xfff0b429);

    test_done();
}
