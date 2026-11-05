/* The shared pieces: the sizes and colour roles every screen draws with. If these drift the screens stop
 * agreeing with each other, which is exactly what "it looks like a first grader made it" was. */
#include "ui/widget.h"
#include "test.h"

#include <string.h>

static const DrawOp *first_text(const DrawList *dl)
{
    for (int i = 0; i < dl->n; i++) if (dl->op[i].kind == OP_TEXT) return &dl->op[i];
    return NULL;
}

int main(void)
{
    UiTheme t = ui_theme();
    DrawList dl;

    dl_reset(&dl);
    widget_title(&dl, &t, 0, 0, 100, 20, "Title", ALIGN_LEFT);
    const DrawOp *op = first_text(&dl);
    check(op != NULL);
    check_int(op->size, W_SIZE_DISPLAY);
    check_int(op->font, FONT_UI_BOLD);
    check_int(op->color, t.ink);

    dl_reset(&dl);
    widget_label(&dl, &t, 0, 0, 100, 20, "Label", ALIGN_LEFT);
    op = first_text(&dl);
    check_int(op->size, W_SIZE_TITLE);
    check_int(op->font, FONT_UI);
    check_int(op->color, t.ink);

    dl_reset(&dl);
    widget_small(&dl, &t, 0, 0, 100, 20, "Body", ALIGN_LEFT);
    op = first_text(&dl);
    check_int(op->size, W_SIZE_BODY);
    check_int(op->color, t.muted);

    dl_reset(&dl);
    widget_tiny(&dl, &t, 0, 0, 100, 20, "Small", ALIGN_LEFT);
    op = first_text(&dl);
    check_int(op->size, W_SIZE_SMALL);
    check_int(op->color, t.faint);

    /* the four steps are four different sizes, in the order they are used */
    check(W_SIZE_DISPLAY > W_SIZE_TITLE && W_SIZE_TITLE > W_SIZE_BODY && W_SIZE_BODY > W_SIZE_SMALL);

    test_done();
}
