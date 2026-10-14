#pragma once

namespace qs::windows {
// Dialogs are native so restoration remains usable after Qt/QML teardown.
bool confirmExplorerReplacement(bool& stoppedExplorer);
void offerExplorerRestart();
} // namespace qs::windows
