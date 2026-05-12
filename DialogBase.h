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
// Base class for all modal dialogs. Owns the event loop, backing-window
// save/restore, shadow, box, Tab/Shift-Tab, and ESC handling.
//
// Two usage modes — may be combined in one dialog:
//
// MODE A — Flat fields (GoToLineDialog, ReplaceDialog):
//   Register InputDescriptor and ButtonDescriptor via addInput()/addButton().
//   Tab cycles through all registered items by focus_index.
//   Arrow keys use NavigationGraph edges.
//
// MODE B — Focus groups (SettingsDialog):
//   Register FocusGroup via addGroup() and ButtonDescriptor via addButton().
//   Tab cycles between groups + the button row.
//   Arrow keys dispatch to the active group's inner widgets.
//
// Mandatory hooks:
//   void onInit()  — register widgets, set focus
//   void onDraw()  — draw dynamic/static text not covered by widgets
//
// Optional hook:
//   HandleResult onKey(wint_t ch)  — handle exotic keys
// ═══════════════════════════════════════════════════════════════════════════════

enum class HandleResult { CONTINUE, CLOSE };

// ── Flat input descriptor (Mode A) ───────────────────────────────────────────
struct InputDescriptor {
    int         focus_index;
    int         field_x, field_y;
    int         field_w;
    std::string label;
    int         label_x, label_y;
    std::string& buffer;
    bool        numeric_only = false;
};

// ── Button descriptor (both modes) ───────────────────────────────────────────
struct ButtonDescriptor {
    int         focus_index;
    int         btn_x, btn_y;
    std::string label;
    std::function<HandleResult()> on_activate;

    char hotkey() const noexcept {
        for (std::size_t i = 0; i + 1 < label.size(); ++i)
            if (label[i] == '&')
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(label[i + 1])));
        return '\0';
    }
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

    // ── Optional hook ─────────────────────────────────────────────────────────
    virtual HandleResult onKey(wint_t ch) { (void)ch; return HandleResult::CONTINUE; }

    // ── Mode A registration ───────────────────────────────────────────────────
    void addInput (InputDescriptor  d) { inputs_.push_back(std::move(d));   }
    void addButton(ButtonDescriptor d) { buttons_.push_back(std::move(d));  }

    template<CyclicEnum E>
    void setNavigation(const NavigationGraph<E>& graph) {
        nav_ = [graph](int cur, Direction dir) -> int {
            return static_cast<int>(graph.move(static_cast<E>(cur), dir));
        };
    }

    // ── Mode B registration ───────────────────────────────────────────────────
    void addGroup(FocusGroup g) { groups_.push_back(std::move(g)); }

    // In Mode B the button row is always the last tab stop.
    // focus_index on ButtonDescriptors is the index within the button row (0, 1, …).
    void addGroupButton(ButtonDescriptor d) { group_buttons_.push_back(std::move(d)); }

    // ── Focus management ──────────────────────────────────────────────────────
    void setFocus     (int i) noexcept { focus_ = i;       }
    void setFocusCount(int n) noexcept { focus_count_ = n; }
    int  getFocus     ()const noexcept { return focus_;    }

    // Mode B helpers
    void setGroupFocus    (int g) noexcept { group_focus_     = g; }
    void setGroupBtnFocus (int b) noexcept { group_btn_focus_ = b; }

    DialogResult& result() noexcept { return result_; }

private:
    // ── Rendering ─────────────────────────────────────────────────────────────
    void drawFrame    (Renderer&, int sx, int sy, bool pressed);
    void clearInterior(int sx, int sy);
    void drawInputs   (Renderer&, int sx, int sy);
    void drawButtons  (Renderer&, int sx, int sy, bool pressed);
    void drawGroups   (Renderer&, int sx, int sy, bool pressed);
    void placeCursor  (Renderer&, int sx, int sy);
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

    // ── Data ──────────────────────────────────────────────────────────────────
    std::string title_;
    int         w_, h_;

    // Mode A
    int  focus_       = 0;
    int  focus_count_ = 0;
    std::vector<InputDescriptor>  inputs_;
    std::vector<ButtonDescriptor> buttons_;
    std::function<int(int,Direction)> nav_;

    // Mode B
    int  group_focus_     = 0;   // which group (or groups_.size() = button row)
    int  group_btn_focus_ = 0;   // which button within the button row
    std::vector<FocusGroup>       groups_;
    std::vector<ButtonDescriptor> group_buttons_;

    // Shared
    bool              pressed_        = false;
    ButtonDescriptor* pending_button_ = nullptr;
    DialogResult      result_;
};
