pragma Singleton
import QtQuick

// Sistema de diseño SDM: espaciado, tipografía, métricas, animaciones y
// curvas de easing compartidas por toda la interfaz (design tokens).
QtObject {
    // --- Espaciado (escala 4px) ---
    readonly property int spacing: 8
    readonly property int spacingSmall: 4
    readonly property int spacingLarge: 16
    readonly property int padding: 12
    readonly property int paddingLarge: 20
    readonly property int radius: 8
    readonly property int radiusSmall: 5
    readonly property int radiusLarge: 12
    readonly property int radiusPill: 999

    // --- Dimensiones ---
    readonly property int toolbarHeight: 54
    readonly property int rowHeight: 42
    readonly property int headerHeight: 34
    readonly property int sidebarWidth: 224
    readonly property int segmentHeight: 116

    // --- Tipografía ---
    readonly property int fontSize: 13
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeTiny: 10
    readonly property int fontSizeTitle: 15
    readonly property int fontSizeHeader: 12
    readonly property int fontSizeHero: 20
    readonly property string fontMono: "Consolas, 'DejaVu Sans Mono', monospace"

    // --- Animaciones ---
    readonly property int animFast: 120
    readonly property int animNormal: 200
    readonly property int animSlow: 320
    readonly property int animToast: 260

    // --- Curvas de easing (uso: easing.type: Style.easeOut) ---
    readonly property int easeOut: Easing.OutCubic
    readonly property int easeInOut: Easing.InOutCubic
    readonly property int easeSpring: Easing.OutBack

    // --- Elevación (sombras por capas, sin efectos externos) ---
    readonly property int elevationNone: 0
    readonly property int elevationLow: 1
    readonly property int elevationHigh: 2

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
