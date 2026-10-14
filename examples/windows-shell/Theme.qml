pragma Singleton
import QtQuick

QtObject {
    // Omarchy's Gruvbox palette: themes/gruvbox/colors.toml.
    property color accent: "#7daea3"
    readonly property color background: "#282828"
    readonly property color darkBackground: "#1e1e1e"
    readonly property color surface: "#3c3836"
    readonly property color selection: "#504945"
    readonly property color border: "#665c54"
    readonly property color foreground: "#d4be98"
    readonly property color secondary: "#bdae93"
    readonly property color muted: "#7c6f64"
    readonly property color yellow: "#d8a657"
    readonly property color green: "#a9b665"
    readonly property color red: "#ea6962"
    readonly property string fontFamily: {
        const installed = Qt.fontFamilies();
        for (const name of ["JetBrainsMono Nerd Font", "JetBrains Mono", "Cascadia Mono", "Consolas"])
            if (installed.indexOf(name) !== -1)
                return name;
        return "monospace";
    }
}
