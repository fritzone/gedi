#include "QuestionDialog.h"

// ═══════════════════════════════════════════════════════════════════════════════
// QuestionDialog.cpp
//
// Layout (w=54, h=8):
//
//   ╔══════════════════ Question ═════════════════════╗
//   ║                                                 ║
//   ║  <question text>                                ║
//   ║    <info text, bold, truncated if too long>     ║
//   ║                                                 ║
//   ║           [ &Yes ]    [ &No ]                   ║
//   ║                                                 ║
//   ╚═════════════════════════════════════════════════╝
//
// Button positions are computed from label widths so the row stays centred
// even if the labels change.
// ═══════════════════════════════════════════════════════════════════════════════

static constexpr int W = 54;
static constexpr int H = 8;

// ── Constructor ───────────────────────────────────────────────────────────────

QuestionDialog::QuestionDialog(const std::string& question,
                               const std::string& info)
    : DialogBase("Question", W, H)
    , question_(question)
    , info_(info)
{
    // Truncate info to fit: w - 6 usable chars (2-char margin each side + 1 pad)
    const int max_w = W - 6;
    if ((int)info_.length() > max_w)
        info_ = "..." + info_.substr(info_.length() - (max_w - 3));
}

// ── Static factory ────────────────────────────────────────────────────────────

int QuestionDialog::ask(Renderer&          renderer,
                        const std::string& question,
                        const std::string& info)
{
    QuestionDialog dlg(question, info);
    DialogResult r = dlg.run(renderer);
    if (r.cancelled()) return -1;
    return r.as_int("answer").value_or(-1);
}

// ── onInit ────────────────────────────────────────────────────────────────────

void QuestionDialog::onInit()
{
    // This is a button-only dialog — the ButtonRow is the sole Tab stop.
    // focus_count=1 means Tab/Shift-Tab cycle within the row (Yes↔No),
    // handled by the special case in DialogBase::dispatchKey.
    setFocusCount(1);
    setFocus(0);
    setButtonRowFocusIndex(0);

    // ── Button positions — centred, computed from label widths ────────────────
    const std::string yes_label  = " &Yes ";
    const std::string no_label   = " &No ";
    const int gap        = 4;
    const int total_w    = (int)yes_label.size() + gap + (int)no_label.size();
    const int yes_x      = (W - total_w) / 2;
    const int no_x       = yes_x + (int)yes_label.size() + gap;
    const int btn_y      = H - 3;

    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = yes_label,
                .x = yes_x, .y = btn_y,
                .on_activate = [this]() -> HandleResult {
                    result().accept();
                    result().set("answer", "1");
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = no_label,
                .x = no_x, .y = btn_y,
                .on_activate = [this]() -> HandleResult {
                    result().accept();
                    result().set("answer", "0");
                    return HandleResult::CLOSE;
                }
            },
        },
        .inner_focus = 0,   // start with Yes highlighted
    });
}

// ── onDraw ────────────────────────────────────────────────────────────────────

void QuestionDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    renderer.drawText(startx + 2, starty + 2, question_, Renderer::CP_DIALOG);
    if (!info_.empty())
        renderer.drawText(startx + 4, starty + 3, info_,
                          Renderer::CP_DIALOG, A_BOLD);
}

// ── onKey ─────────────────────────────────────────────────────────────────────
//
// Reached for any printable character that no input field consumed
// (dispatchChar fall-through). Handles Y/N direct activation so the user
// doesn't have to press Alt — matching the original dialog's behaviour.
//
// Routes through activateButtonByIndex() so the press animation (dip + rise)
// runs before the on_activate lambda is called, exactly as for Enter.

HandleResult QuestionDialog::onKey(wint_t ch)
{
    switch (ch) {
    case 'y': case 'Y':
        activateButtonByIndex(0);       // arm Yes for the press animation
        return HandleResult::CONTINUE;  // run() will animate then call on_activate

    case 'n': case 'N':
        activateButtonByIndex(1);       // arm No for the press animation
        return HandleResult::CONTINUE;

    default:
        return HandleResult::CONTINUE;
    }
}
