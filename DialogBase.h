#pragma once
#include "CyclicEnum.h"
#include "NavigationGraph.h"
#include "DialogResult.h"
#include "Widgets.h"
#include "Renderer.h"
#include <ncurses.h>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

// ═══════════════════════════════════════════════════════════════════════════════
// DialogBase.h
//
// Base class for all modal dialogs.
//
// All button management is now done through ButtonRow / Button widgets.
// There is no longer a separate ButtonDescriptor type — buttons are first-class
// widgets alongside CheckBox, Spinner, and RadioList.
//
// Two layout modes (may be combined):
//
//   MODE A — flat (GoToLineDialog, ReplaceDialog)
//     addInput()   — registers a text input field
//     addButtons() — registers the ButtonRow (one Tab stop for all buttons)
//     setNavigation() — registers arrow-key edges via NavigationGraph
//
//   MODE B — grouped (SettingsDialog)
//     addGroup()   — registers a FocusGroup (CheckBox/Spinner/RadioList)
//     addButtons() — registers the ButtonRow as the final Tab stop
//
// Mandatory hooks:  onInit(), onDraw()
// Optional hook:    onKey()
// ═══════════════════════════════════════════════════════════════════════════════

// HandleResult is defined in Widgets.h (included above)

// ── Flat input descriptor (Mode A) ───────────────────────────────────────────
struct InputDescriptor {
    int          focus_index;
    int          field_x, field_y;
    int          field_w;
    std::string  label;
    int          label_x, label_y;
    std::string& buffer;
    bool         numeric_only = false;
};


class DialogBase {
public:
    explicit DialogBase(std::string_view title, int w, int h)
        : title_(title), w_(w), h_(h) {}

    virtual ~DialogBase() = default;

    DialogResult run(Renderer& renderer);

protected:
    // ── Mandatory hooks ───────────────────────────────────────────────────────
    virtual void onInit() = 0;
    virtual void onDraw(Renderer& renderer, int startx, int starty) = 0;

    // ── Optional hooks ────────────────────────────────────────────────────────
    virtual HandleResult onKey(wint_t ch) { (void)ch; return HandleResult::CONTINUE; }

    // Called from placeCursor before the default logic runs.
    // Return true to take over cursor placement (hides/shows cursor yourself).
    virtual bool onPlaceCursor(Renderer&, int /*sx*/, int /*sy*/) { return false; }

    // ── Mode A registration ───────────────────────────────────────────────────
    void addInput  (InputDescriptor d) { inputs_.push_back(std::move(d)); }

    template<CyclicEnum E>
    void setNavigation(const NavigationGraph<E>& graph) {
        nav_ = [graph](int cur, Direction dir) -> int {
            return static_cast<int>(graph.move(static_cast<E>(cur), dir));
        };
    }

    // ── Shared: register the button row (both modes) ──────────────────────────
    // In Mode A, the ButtonRow's focus_index in the CyclicEnum determines when
    // Tab reaches it. In Mode B it is always the last Tab stop automatically.
    void addButtons(ButtonRow row) { button_row_ = std::move(row); }

    // ── Mode B registration ───────────────────────────────────────────────────
    void addGroup(FocusGroup g) { groups_.push_back(std::move(g)); }

    // ── Focus management ──────────────────────────────────────────────────────
    void setFocus     (int i) noexcept { focus_       = i; }
    void setFocusCount(int n) noexcept { focus_count_ = n; }
    int  getFocus     ()const noexcept { return focus_;    }

    void setGroupFocus   (int g) noexcept { group_focus_     = g; }
    void setGroupBtnFocus(int b) noexcept { button_row_.inner_focus = b; }

    // Read the currently focused group index (Mode B)
    int  getFocusedGroup()    const noexcept { return group_focus_; }
    bool inButtonRow()        const noexcept { return inGroupButtonRow(); }
    int  getBtnInnerFocus()   const noexcept { return button_row_.inner_focus; }
    int  groupCount()         const noexcept { return (int)groups_.size(); }

    // Mode B: intercept Tab / Shift-Tab before default navigation runs.
    // Call setGroupFocus() / setGroupBtnFocus() to redirect focus, then return true.
    // Return false to let the default logic handle it.
    virtual bool onTab(bool /*forward*/) { return false; }

    // Direct access to groups so subclasses can read/write widget state
    // (e.g. sync OptionList content from a TabControl in onDraw)
    std::vector<FocusGroup>& groups() noexcept { return groups_; }

    // Trigger the press animation for a button by its index in the ButtonRow.
    // Safe to call from onKey() — the animation runs on the next frame.
    void activateButtonByIndex(int index) {
        button_row_.inner_focus = index;
        if (!groups_.empty())
            group_focus_ = static_cast<int>(groups_.size());
        else
            focus_ = btn_row_focus_index_;
        Button* btn = button_row_.focusedButton();
        if (btn) armButton(btn);
    }

    // ── The button row focus index used in Mode A ─────────────────────────────
    // Subclass sets this so the base knows which focus_index == button row.
    void setButtonRowFocusIndex(int i) noexcept { btn_row_focus_index_ = i; }

    DialogResult& result() noexcept { return result_; }

private:
    // ── Rendering ─────────────────────────────────────────────────────────────
    void drawFrame        (Renderer&, int sx, int sy, bool pressed);
    void clearInterior    (int sx, int sy);
    void drawInputs       (Renderer&, int sx, int sy);
    void drawGroups       (Renderer&, int sx, int sy, bool pressed);
    void placeCursor      (Renderer&, int sx, int sy);
    void runPressAnimation(Renderer&, int sx, int sy);

    // ── Dispatch — Mode A ─────────────────────────────────────────────────────
    HandleResult dispatchKey      (wint_t ch);
    HandleResult dispatchAltKey   (wint_t ch);
    HandleResult dispatchArrow    (wint_t ch);
    HandleResult dispatchEnter    ();
    HandleResult dispatchBackspace();
    HandleResult dispatchChar     (wint_t ch);

    // ── Dispatch — Mode B ─────────────────────────────────────────────────────
    HandleResult dispatchGroupKey   (wint_t ch);
    HandleResult dispatchGroupAltKey(wint_t ch);

    bool inGroupButtonRow() const noexcept {
        return !groups_.empty() &&
               group_focus_ == static_cast<int>(groups_.size());
    }

    // Try to activate a button by hotkey (both modes).
    // Sets pending_button_ and pressed_. Returns true if found.
    bool tryHotkeyActivate(char lower);

    // Arm a button for the press animation.
    void armButton(Button* btn) {
        pending_button_ = btn;
        pressed_        = true;
    }

    // ── Data ──────────────────────────────────────────────────────────────────
    std::string title_;
    int         w_, h_;

    // Mode A
    int  focus_              = 0;
    int  focus_count_        = 0;
    int  btn_row_focus_index_= -1; // focus value that means "button row is active"
    std::vector<InputDescriptor>      inputs_;
    std::function<int(int,Direction)> nav_;

    // Mode B
    int  group_focus_ = 0;
    std::vector<FocusGroup> groups_;

    // Shared — the single ButtonRow
    ButtonRow button_row_;

    // Shared — press animation state
    bool    pressed_        = false;
    Button* pending_button_ = nullptr;

    DialogResult result_;
};
