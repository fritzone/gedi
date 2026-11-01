#pragma once
#include <string>
#include <vector>

#include "Install.h"
#include "Payload.h"
#include "Renderer.h"
#include "ToolchainDetect.h"
#include "curses_compat.h"    // MEVENT and the mouse bit masks

// The setup front end: a Turbo-era full-screen wizard drawn on the same VGA text
// grid gedi itself runs on. It owns no drawing code of its own beyond layout -
// the shaded desktop, the double-ruled panel, the drop shadows and the buttons
// all come from gedi's Renderer, so the installer and the editor look like two
// screens of the same program.
class Wizard {
public:
    Wizard(Renderer& renderer, const payload::Payload& pl);

    // Drive the install flow. Returns true when files were actually installed.
    bool run();

    // The uninstall flow: confirm, remove, report.
    bool runUninstall(const nlohmann::json& manifest);

    // Set once the user asks for it on the final page.
    bool launchRequested() const { return launch_; }
    std::string launchTarget() const { return launch_target_; }

    // Where the files actually went (the user may have edited it).
    std::string installDir() const { return dir_; }

private:
    enum Page { P_WELCOME, P_LICENSE, P_DIR, P_COMPONENTS, P_TOOLCHAIN,
                P_CONFIRM, P_PROGRESS, P_DONE, P_COUNT };
    enum Focus { F_CONTENT, F_BUTTONS };

    // What a button does, kept separate from what it says. Matching on the label
    // was a trap: "E&xit" does not contain "Exit", so the hotkey fell through to
    // the default action and X advanced the wizard instead of leaving it.
    enum ButtonId { B_CONTINUE, B_BACK, B_EXIT, B_BROWSE, B_FINISH, B_REMOVE, B_CANCEL };

    struct Btn {
        std::string text;
        ButtonId    id;
        int         x = 0;     // filled in by drawButtons, so the mouse hit-test
        int         w = 0;     // and the painting can never disagree
    };

    struct Component {
        std::string id, name, description;
        bool        required = false;
        bool        selected = true;
        uint64_t    bytes    = 0;
        // Marked "provides_toolchain" in the script: leaving this one out is
        // what makes the compiler page appear.
        bool        provides_toolchain = false;
    };

    // ---- chrome ----
    void drawDesktop();
    void drawPanel(const std::string& title);
    void drawHints(const std::string& hints);
    void drawButtons();
    int  panelX() const;
    int  panelY() const;
    int  contentX() const { return panelX() + 3; }
    int  contentY() const { return panelY() + 2; }
    int  contentW() const { return W - 6; }
    int  contentH() const { return H - 6; }

    // ---- pages ----
    void drawWelcome();
    void drawLicense();
    void drawDir();
    void drawComponents();
    void drawToolchain();
    void drawConfirm();
    void drawProgress(const std::string& current, uint64_t done, uint64_t total);
    void drawDone();
    void drawCurrentPage();

    // Reads one key, resolving the Alt prefix. The backend delivers Alt+<key> as
    // ESC followed by the key (curses_compat's push_alt), so a bare 27 has to be
    // disambiguated by peeking before it can be treated as Escape. Returns ERR
    // for a sequence that should be ignored.
    wint_t nextKey(bool& alt);

    bool handleKey(wint_t ch, bool alt);   // false = leave the loop

    // True when typing should go into a text field rather than fire a hotkey.
    bool textFieldFocused() const {
        if (focus_ != F_CONTENT) return false;
        return page_ == P_DIR || (page_ == P_TOOLCHAIN && tc_cursor_ == tcPathRow());
    }
    bool handleMouse(const MEVENT& ev);
    bool activate(ButtonId id);       // false = leave the loop

    // Paint a button in its pressed state briefly before acting on it, with the
    // same timing DialogBase::runPressAnimation uses in the editor.
    void flashButton(int idx);
    int  buttonRowY() const { return panelY() + H - 3; }
    void setPage(Page p);
    bool canAdvance() const;
    void advance();
    void back();
    bool confirmQuit();

    // ---- helpers ----
    static std::vector<std::string> wrap(const std::string& text, int width);
    void drawTextBlock(const std::vector<std::string>& lines, int x, int y, int h, int scroll, int color);
    void drawProgressBar(int x, int y, int w, double frac);
    void drawInputField(int x, int y, int w, const std::string& text, int cursor, int scroll, bool focused);
    void message(const std::string& title, const std::string& text);
    static std::string humanSize(uint64_t bytes);

    // Selected component ids, in payload order.
    std::vector<std::string> selectedIds() const;

    // ---- compiler selection ------------------------------------------------
    // The compiler page is only reached when no component in the payload
    // supplies one, i.e. the user unticked the bundled toolchain.
    bool needCompilerPage() const;
    toolchain::Kind tcKind() const;
    // Row index of the path field; one past the last radio button. Also the
    // cursor's last row, since nothing follows it.
    int  tcPathRow() const { return (int)toolchain::kinds().size(); }
    bool tcPathRowUsable() const { return toolchain::needsPath(tcKind()); }
    void tcSetButtons();          // the button row, which depends on the chosen kind
    void tcSelect(int choice);    // switch kind, keeping the page consistent
    void browseForCompiler();     // the Open File dialog, filtered to this kind's exe
    void probeToolchain();        // refresh tc_setup_ / tc_error_ for the current choice

    // ---- state ----
    Renderer&                 r_;
    const payload::Payload&   pl_;
    const nlohmann::json&     spec_;

    static constexpr int W = 70;      // panel width
    static constexpr int H = 19;      // panel height

    Page  page_  = P_WELCOME;
    Focus focus_ = F_BUTTONS;
    int   button_ = 0;                // index into the current page's buttons
    int   pressed_button_ = -1;       // drawn pushed in, for the press animation
    std::vector<Btn> buttons_;

    // Rows recorded while drawing, so a click can find controls whose position
    // depends on how much text landed above them.
    int confirm_chk_y_[2] = { -1, -1 };
    int done_chk_y_       = -1;
    int dir_field_y_      = -1;

    // license / text scrolling
    std::vector<std::string> license_lines_;
    int license_scroll_ = 0;

    // target directory editing
    std::string dir_;
    int dir_cursor_ = 0;
    int dir_scroll_ = 0;

    // components
    std::vector<Component> components_;
    int comp_cursor_ = 0;
    int comp_scroll_ = 0;

    // compiler page
    int         tc_choice_ = 0;             // index into toolchain::kinds()
    int         tc_cursor_ = 0;             // focused row: a radio, or tcPathRow()
    std::string tc_path_;                   // what the user typed, for the path kinds
    int         tc_path_cursor_ = 0;
    int         tc_path_scroll_ = 0;
    bool        tc_dirty_ = true;           // the choice or the path changed; re-probe
    toolchain::Setup tc_setup_;
    std::string tc_error_;
    // Rows recorded while drawing, so a click can find the controls.
    int tc_radio_y_ = -1;
    int tc_path_y_  = -1;

    // options on the final pages
    bool make_start_menu_ = true;
    bool make_desktop_    = false;
    bool launch_          = false;
    std::string launch_target_;

    // outcome
    bool        installed_ = false;
    inst::Result result_;
    bool        uninstall_mode_ = false;
};
