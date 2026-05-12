#pragma once
#include <string>
#include <unordered_map>
#include <optional>
#include <stdexcept>

// ═══════════════════════════════════════════════════════════════════════════════
// DialogResult.h
//
// A generic, type-safe result container for modal dialogs.
//
// Callers pattern:
//
//   DialogResult r = MyDialog::show(renderer);
//   if (r.accepted()) {
//       std::string s = r["find"];          // always a string
//       int n         = r.as_int("lineNo"); // converted on demand
//   }
//
// Dialogs fill it with:
//   result.accept();
//   result.set("find",   find_buf);
//   result.set("lineNo", std::to_string(42));
// ═══════════════════════════════════════════════════════════════════════════════

class DialogResult {
public:
    // ── State ─────────────────────────────────────────────────────────────────
    void accept()  noexcept { accepted_ = true; }
    void cancel()  noexcept { accepted_ = false; }

    [[nodiscard]] bool accepted() const noexcept { return accepted_; }
    [[nodiscard]] bool cancelled() const noexcept { return !accepted_; }

    // ── Field storage ─────────────────────────────────────────────────────────
    void set(const std::string& name, const std::string& value) {
        fields_[name] = value;
    }

    // Returns the raw string for `name`, or "" if not set.
    [[nodiscard]] const std::string& operator[](const std::string& name) const {
        static const std::string empty;
        auto it = fields_.find(name);
        return (it != fields_.end()) ? it->second : empty;
    }

    // Returns std::nullopt if the field is missing or non-numeric.
    [[nodiscard]] std::optional<int> as_int(const std::string& name) const noexcept {
        auto it = fields_.find(name);
        if (it == fields_.end()) return std::nullopt;
        try   { return std::stoi(it->second); }
        catch (...) { return std::nullopt; }
    }

    [[nodiscard]] bool has(const std::string& name) const noexcept {
        return fields_.count(name) > 0;
    }

private:
    bool accepted_ = false;
    std::unordered_map<std::string, std::string> fields_;
};
