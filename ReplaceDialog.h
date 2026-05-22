#pragma once
#include "DialogBase.h"
#include "Renderer.h"
#include <functional>
#include <string>

// Callback signature for all three replace actions.
// The callback receives the current find/replace text and writes a status
// string (e.g. "3 of 12 matches") that the dialog displays on the next frame.
using ReplaceCallback = std::function<void(const std::string& find,
                                           const std::string& replace,
                                           std::string& status)>;

class ReplaceDialog : private DialogBase {
public:
    // Non-modal: the dialog stays open until the user clicks Close or presses
    // Escape. Actions are delivered via callbacks; no DialogResult is returned.
    static void show(Renderer& renderer,
                     const std::string& initial_find,
                     const std::string& initial_replace,
                     ReplaceCallback    on_find_next,
                     ReplaceCallback    on_replace,
                     ReplaceCallback    on_replace_all,
                     std::function<void()> on_background_refresh);
private:
    ReplaceDialog(const std::string& initial_find,
                  const std::string& initial_replace,
                  ReplaceCallback    on_find_next,
                  ReplaceCallback    on_replace,
                  ReplaceCallback    on_replace_all);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;

    // FIND → REPLACE → BTN_ROW (one Tab stop for all buttons)
    DeclareCyclicEnum(Focus, FIND, REPLACE, BTN_ROW);

    std::string find_buf_;
    std::string replace_buf_;
    std::string status_;
    NavigationGraph<Focus> nav_;

    ReplaceCallback on_find_next_;
    ReplaceCallback on_replace_;
    ReplaceCallback on_replace_all_;
};
