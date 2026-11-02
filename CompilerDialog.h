#pragma once
#include "CompilerDefs.h"
#include "Config.h"
#include "DialogBase.h"
#include "Renderer.h"
#include "ToolchainDetect.h"
#include "Widgets.h"
#include <map>
#include <string>
#include <vector>

class BuildSystem;

// A one-line text field with a cursor and horizontal scrolling, for the
// manually drawn dialogs below.
struct LineField {
    std::string text;
    int cursor = 0;
    int scroll = 0;

    void set(const std::string& t) { text = t; cursor = (int)t.size(); scroll = 0; }
    bool key(wint_t ch);                        // true when the text changed
    void draw(Renderer& r, int x, int y, int w);
    int  cursorX(int x, int w);                 // screen column of the cursor
};

// ═══════════════════════════════════════════════════════════════════════════════
// CompilerDialog - Options > Compiler Options...
//
// Names the compiler in use and lets it be changed: any compiler gedi has a
// definition for (CompilerDefs.h) - the shipped ones and the user's own - with
// the path to its executable, which Browse can pick and gedi then identifies.
// A compiler gedi has no definition for is introduced with New..., which
// writes a definition for it based on one of the known kinds.
//
// The choice is global - it lives in toolchain.json, not in the per-file or
// per-project options - so Ok writes it there, applies its environment to the
// running editor and tells BuildSystem to forget what it derived from the old
// one. Build > Compile Options then shows the new compiler's options.
// ═══════════════════════════════════════════════════════════════════════════════

class CompilerDialog : private DialogBase {
public:
    // `config` is the editor's live configuration; its toolchain is updated in
    // place when the user picks a different compiler.
    static void show(Renderer& renderer, BuildSystem& buildSystem, Config& config);

private:
    CompilerDialog(Renderer& renderer, BuildSystem& buildSystem, Config& config);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;
    bool onTab(bool forward) override;
    bool onAltKey(char lower) override;
    bool onMouseClick(const MEVENT& ev, int startx, int starty) override;
    bool onPlaceCursor(Renderer& renderer, int sx, int sy) override;

    //  Layout. Rows: title border, blank, "In use" + its path, blank, compiler
    //  combo, its description (as many rows as the longest one needs), blank,
    //  path field + Browse, Browse's shadow, note, blank, buttons, their
    //  shadow, bottom border.
    static constexpr int W        = 72;
    static constexpr int CUR_Y    = 2;
    static constexpr int CURP_Y   = 3;
    static constexpr int KIND_Y   = 5;
    static constexpr int DESC_Y   = 6;
    static constexpr int FIELD_X  = 13;
    static constexpr int DESC_W   = W - 3 - FIELD_X; // the description's column width
    static constexpr int DESC_MAX = 6;               // keeps the dialog within 25 rows

    // Rows the longest description of a selectable compiler wraps to.
    static int descRowsNeeded();
    static std::vector<std::string> wrapDescription(const std::string& text);
    int pathY() const  { return DESC_Y + desc_rows_ + 1; }
    int noteY() const  { return pathY() + 2; }
    int btnY() const   { return noteY() + 2; }        // shadow on the row below
    static int height(int desc_rows) { return DESC_Y + desc_rows + 1 + 2 + 2 + 3; }
    static constexpr int BROWSE_X = W - 14;
    static constexpr int BROWSE_W = 9;               // " &Browse "
    static constexpr int FIELD_W  = BROWSE_X - FIELD_X - 2;

    void refreshDefinitions(const std::string& select_id);   // after the registry changed
    const compilers::Definition& selected() const { return *defs_[sel_]; }
    void select(int i);                 // pick definition i
    bool pathUsable() const;            // false for compilers that locate themselves (MSVC)
    void probe();                       // refresh pending_ / error_ / warning_
    bool apply();                       // record the pick and make the editor use it
    void browse();
    void pressBrowse();
    void introduce();                   // New...: define a compiler gedi does not know

    Renderer&    renderer_;
    BuildSystem& buildSystem_;
    Config&      config_;
    int          desc_rows_;            // rows the description area has

    std::vector<const compilers::Definition*> defs_;   // the selectable compilers
    std::vector<std::string> names_;                    // their names, for the combo
    ComboBox combo_;
    int  sel_ = 0;                                      // index into defs_
    std::map<std::string, std::string> paths_;          // path last left for each definition
    LineField path_;

    bool dirty_   = true;               // the pick changed; re-probe before drawing
    bool changed_ = false;              // the user actually touched it; Ok applies
    toolchain::Setup pending_;          // what the current pick resolves to
    std::string error_;
    std::string warning_;               // the executable looks like a different compiler
    std::string info_;                  // what Browse found out about the executable
    std::string current_desc_;          // version banner of the compiler in use
    std::string current_name_;          // name of the compiler in use

    static constexpr int GRP_COMPILER = 0;
    static constexpr int INNER_KIND   = 0;
    static constexpr int INNER_PATH   = 1;
    static constexpr int INNER_BROWSE = 2;
    static constexpr int BTN_COUNT    = 3;     // Ok / New / Cancel
};

// ═══════════════════════════════════════════════════════════════════════════════
// NewCompilerDialog - introduces a compiler gedi has no definition for.
//
// Asks for a name, the executable and what it is like - GCC-, Clang-, MSVC- or
// Open Watcom-compatible, or generic - and whether it is a cross compiler; the
// executable is probed to suggest the answers. Create writes a definition file
// for it to the user's compilers directory, inheriting the chosen kind.
// ═══════════════════════════════════════════════════════════════════════════════

class NewCompilerDialog : private DialogBase {
public:
    // Returns the new definition's id, or "" when cancelled. `exe` is the
    // executable to start from, and on return the one chosen.
    static std::string show(Renderer& renderer, std::string& exe);

private:
    NewCompilerDialog(Renderer& renderer, const std::string& exe);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;
    bool onTab(bool forward) override;
    bool onAltKey(char lower) override;
    bool onMouseClick(const MEVENT& ev, int startx, int starty) override;
    bool onPlaceCursor(Renderer& renderer, int sx, int sy) override;

    static constexpr int W        = 68;
    static constexpr int NAME_Y   = 2;
    static constexpr int EXE_Y    = 4;
    static constexpr int BASE_Y   = 7;               // first base row
    static constexpr int FIELD_X  = 15;
    static constexpr int BROWSE_X = W - 14;
    static constexpr int BROWSE_W = 9;
    static constexpr int NAME_W   = W - FIELD_X - 4;
    static constexpr int EXE_W    = BROWSE_X - FIELD_X - 2;
    // Rows below the bases: blank, cross-compiler checkbox, blank, buttons,
    // their shadow, bottom border.
    int crossY() const { return BASE_Y + (int)bases_.size() + 1; }
    int btnY()   const { return crossY() + 2; }
    static int height(int bases) { return BASE_Y + bases + 6; }

    void suggest();                     // name, base and cross from the executable
    void browse();
    void pressBrowse();

    Renderer& renderer_;
    LineField name_;
    LineField exe_;
    bool name_edited_ = false;          // the user typed a name: stop suggesting one
    struct Base { std::string id, label; };
    static std::vector<Base> availableBases();   // the kinds a new compiler can be based on
    std::vector<Base> bases_;
    int  base_  = 0;
    bool cross_ = false;
    std::string created_;               // the id written, once Create succeeded

    static constexpr int INNER_NAME   = 0;
    static constexpr int INNER_EXE    = 1;
    static constexpr int INNER_BROWSE = 2;
    static constexpr int INNER_BASE   = 3;
    static constexpr int INNER_CROSS  = 4;
    static constexpr int BTN_COUNT    = 2;     // Create / Cancel
};
