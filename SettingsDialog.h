#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "Renderer.h"
#include "ConfigManager.h"
#include <string>
#include <vector>

class SettingsDialog {
public:
    static void show(Renderer& renderer, Config& config, ConfigManager& configManager, const std::vector<std::string>& themes);
};

#endif // SETTINGSDIALOG_H
