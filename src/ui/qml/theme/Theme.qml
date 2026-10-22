pragma Singleton
import QtQuick

// Paleta Fluent (oscura por defecto, respetando el tema del sistema).
QtObject {
    id: theme

    // true = modo oscuro, false = modo claro (lo sincroniza bridge.darkMode)
    property bool dark: true

    // --- Superficies ---
    readonly property color bg: dark ? "#1f1f1f" : "#f3f3f3"
    readonly property color surface: dark ? "#2b2b2b" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#333333" : "#ececec"
    readonly property color surfaceHigh: dark ? "#3a3a3a" : "#e0e0e0"
    readonly property color border: dark ? "#3d3d3d" : "#d6d6d6"
    readonly property color hover: dark ? "#3f3f3f" : "#e5e5e5"
    readonly property color pressed: dark ? "#4a4a4a" : "#dcdcdc"
    readonly property color selection: dark ? "#2f4f70" : "#d3e6f8"

    // --- Texto ---
    readonly property color text: dark ? "#f5f5f5" : "#1c1c1c"
    readonly property color subtleText: dark ? "#a3a3a3" : "#5f5f5f"
    readonly property color disabledText: dark ? "#6a6a6a" : "#9a9a9a"

    // --- Acentos de estado ---
    readonly property color accent: "#3b9dff"
    readonly property color accentDeep: "#2b7fd4"
    readonly property color success: "#2ecc71"
    readonly property color warning: "#e0b13a"
    readonly property color danger: "#e74c3c"
    readonly property color info: "#8e7cff"
    readonly property color queued: "#9aa0a6"
    readonly property color expired: "#ff7b3d"

    // --- Overlay / sombras ---
    readonly property color overlay: dark ? "#99000000" : "#80000000"
    readonly property color progressTrack: dark ? "#404040" : "#dcdcdc"
    readonly property color progressFill: accent

    // Mapeo de estado -> color (indice de DownloadState).
    function stateColor(stateIndex) {
        switch (stateIndex) {
        case 0: return queued
        case 1: return warning       // Conectando
        case 2: return success       // Descargando
        case 3: return warning       // Pausado
        case 4: return accent        // Completado
        case 5: return danger        // Fallido
        case 6: return expired       // Enlace expirado
        default: return queued
        }
    }

    function stateName(stateIndex) {
        switch (stateIndex) {
        case 0: return "En cola"
        case 1: return "Conectando"
        case 2: return "Descargando"
        case 3: return "Pausado"
        case 4: return "Completado"
        case 5: return "Fallido"
        case 6: return "Enlace expirado"
        default: return "Desconocido"
        }
    }
}
