pragma Singleton
import QtQuick

// Metricas, tipografia y animaciones compartidas.
QtObject {
    // --- Espaciado ---
    readonly property int spacing: 8
    readonly property int padding: 12
    readonly property int paddingLarge: 20
    readonly property int radius: 8
    readonly property int radiusSmall: 5

    // --- Dimensiones ---
    readonly property int toolbarHeight: 54
    readonly property int rowHeight: 42
    readonly property int headerHeight: 34
    readonly property int sidebarWidth: 224
    readonly property int segmentHeight: 104

    // --- Tipografia ---
    readonly property int fontSize: 13
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeTitle: 15
    readonly property int fontSizeHeader: 12

    // --- Animaciones ---
    readonly property int animFast: 120
    readonly property int animNormal: 200

    // --- Columnas de la tabla (px) ---
    readonly property int colState: 40
    readonly property int colName: 320
    readonly property int colSize: 96
    readonly property int colProgress: 190
    readonly property int colSpeed: 120
    readonly property int colEta: 84
    readonly property int colThreads: 66
    readonly property int colDate: 120
}
