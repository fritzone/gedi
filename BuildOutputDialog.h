#ifndef BUILDOUTPUTDIALOG_H
#define BUILDOUTPUTDIALOG_H

#include "Renderer.h"
#include <functional>
#include <string>
#include <vector>

struct BuildMonitor;

class BuildOutputDialog {
public:
    // A finished build's output, until closed.
    static void show(Renderer& renderer, const std::vector<std::string>& lines);

    // Runs a build on a thread of its own and shows its output as it comes, in
    // the same window. The window stays usable meanwhile: it scrolls - following
    // the newest output unless scrolled away from it - and Esc or Cancel stops
    // the build (through the monitor's cancel flag, which kills its tools).
    // `work` runs the build, reporting through the monitor, and returns the
    // complete output, shown in place of the streamed lines once it is done.
    // Then `keep_open` decides: true leaves the window open on that output
    // until it is closed, false closes it at once.
    static void runLive(Renderer& renderer,
                        const std::function<std::vector<std::string>(const BuildMonitor&)>& work,
                        const std::function<bool()>& keep_open);
};

#endif // BUILDOUTPUTDIALOG_H
