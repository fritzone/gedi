#include "HelpProvider.h"
#include <fstream>
#include <regex>

void HelpProvider::loadHelpFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;

    m_help_data.clear();
    std::string line;
    HelpSection* current_section = nullptr;
    std::regex section_re(R"(^\[(\w+)\])");
    std::regex link_re(R"(\[\[(\w+)(?:\|([^\]]+))?\]\])");
    std::regex bold_re(R"(\*\*([^\*]+)\*\*)");

    while (std::getline(f, line)) {
        std::smatch match;
        if (std::regex_match(line, match, section_re)) {
            std::string id = match[1].str();
            m_help_data[id] = HelpSection{id};
            current_section = &m_help_data[id];
        } else if (current_section) {
            HelpLine help_line;
            std::string remaining_text = line;

            while (!remaining_text.empty()) {
                auto link_match = std::sregex_iterator(remaining_text.begin(), remaining_text.end(), link_re);
                auto bold_match = std::sregex_iterator(remaining_text.begin(), remaining_text.end(), bold_re);

                auto first_token = (link_match != std::sregex_iterator() && bold_match != std::sregex_iterator())
                                       ? (link_match->position() < bold_match->position() ? link_match : bold_match)
                                       : (link_match != std::sregex_iterator() ? link_match : bold_match);

                if (first_token == std::sregex_iterator()) {
                    if (!remaining_text.empty()) {
                        help_line.segments.push_back({remaining_text, STYLE_NORMAL});
                    }
                    break;
                }

                if (first_token->position() > 0) {
                    help_line.segments.push_back({remaining_text.substr(0, first_token->position()), STYLE_NORMAL});
                }

                if (first_token->str().starts_with("[[")) { // It's a link
                    help_line.segments.push_back({
                        first_token->operator[](2).matched ? first_token->operator[](2).str() : first_token->operator[](1).str(),
                        STYLE_LINK,
                        first_token->operator[](1).str()
                    });
                } else { // It's bold
                    help_line.segments.push_back({first_token->operator[](1).str(), STYLE_BOLD});
                }
                remaining_text = first_token->suffix().str();
            }
            current_section->lines.push_back(help_line);
        }
    }
}
