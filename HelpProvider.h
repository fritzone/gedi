#ifndef HELPPROVIDER_H
#define HELPPROVIDER_H

#include <string>
#include <vector>
#include <map>

enum TextStyle { STYLE_NORMAL, STYLE_BOLD, STYLE_LINK };

struct TextSegment {
    std::string text;
    TextStyle style = STYLE_NORMAL;
    std::string target_id; // Only used for STYLE_LINK
};

struct HelpLine {
    std::vector<TextSegment> segments;
};

struct HelpSection {
    std::string id;
    std::vector<HelpLine> lines;
};

class HelpProvider {
public:
    void loadHelpFile(const std::string& path);
    const std::map<std::string, HelpSection>& getHelpData() const { return m_help_data; }

private:
    std::map<std::string, HelpSection> m_help_data;
};

#endif // HELPPROVIDER_H
