import QtQuick
import QtQuick.Layouts
import "../theme"

// Tarjeta reutilizable con sombra por capas (elevación Fluent) y degradado
// sutil de fondo.  Se usa en paneles, diálogos y resúmenes.
Rectangle {
    id: card

    property int elevation: Style.elevationLow
    property bool gradientBackground: false
    default property alias content: inner.data

    radius: Style.radius
    color: Theme.surface
    border.color: Theme.border
    border.width: 1

    // Sombra exterior por capas (sin QtGraphicalEffects: render nativo rápido).
    Rectangle {
        visible: card.elevation >= Style.elevationLow
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.leftMargin: 1
        anchors.rightMargin: -1
        anchors.bottomMargin: -2
        radius: card.radius
        color: Theme.shadow
        opacity: 0.5
        z: -2
    }
    Rectangle {
        visible: card.elevation >= Style.elevationHigh
        anchors.fill: parent
        anchors.topMargin: 5
        anchors.leftMargin: 2
        anchors.rightMargin: -2
        anchors.bottomMargin: -5
        radius: card.radius + 2
        color: Theme.shadow
        opacity: 0.28
        z: -3
    }

    // Degradado opcional de fondo.
    Rectangle {
        visible: card.gradientBackground
        anchors.fill: parent
        radius: card.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.gradientStart; }
            GradientStop { position: 1.0; color: Theme.gradientEnd; }
        }
        opacity: 0.10
        z: -1
    }

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: Style.padding
    }
}
