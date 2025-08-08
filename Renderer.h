#ifndef RENDERER_H
#define RENDERER_H

#include "nlohmann/json.hpp"

using json = nlohmann::json;

class Renderer {
public:
    enum ColorPairID {
        CP_DEFAULT_TEXT = 1, CP_HIGHLIGHT, CP_MENU_BAR, CP_MENU_ITEM,
        CP_MENU_SELECTED, CP_DIALOG, CP_DIALOG_BUTTON, CP_SELECTION,
        CP_STATUS_BAR, CP_STATUS_BAR_HIGHLIGHT, CP_SHADOW,
        CP_DIALOG_TITLE, CP_CHANGED_INDICATOR, CP_LIST_BOX,
        CP_SYNTAX_KEYWORD, CP_SYNTAX_COMMENT, CP_SYNTAX_STRING,
        CP_SYNTAX_NUMBER, CP_SYNTAX_PREPROCESSOR, CP_SYNTAX_REGISTER_VAR,

        // New theme-independent colors
        CP_COMPILE_ERROR, CP_COMPILE_WARNING
    };

    enum BoxStyle { SINGLE, DOUBLE };

    Renderer();

    ~Renderer();
    void clear();
    void refresh();
    void updateDimensions();

    void drawText(int x, int y, const std::string& text, int colorId, int flags = 0);

    void drawStyledText(int x, int y, const std::string& text, int colorId);

    void drawBox(int x, int y, int w, int h, int colorId, BoxStyle style = SINGLE);

    void drawBoxWithTitle(int x, int y, int w, int h, int colorId, BoxStyle style, const std::string& title, int title_color, int title_flags);

    void drawShadow(int x, int y, int w, int h);

    wint_t getChar();
    void hideCursor();
    void showCursor();
    int getWidth() const;
    int getHeight() const;
    void setCursor(int x, int y);
    int getStyleFlags(ColorPairID id) const;


    void loadColors();

    void loadColors(const json& theme_data);


    void createDefaultColorsFile();

private:
    int m_width = 0, m_height = 0;
    std::map<std::string, int> m_color_map;
    std::map<std::string, int> m_color_pair_map;
    std::map<Renderer::ColorPairID, int> m_style_attributes;

};

#endif // RENDERER_H
