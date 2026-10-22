import QtQuick
import QtQuick.Controls
import "../theme"

// Boton plano estilo Fluent: icono + etiqueta, estados hover/pressed.
Rectangle {
    id: control

    property string label: ""
    property string glyph: ""
    property bool accent: false
    property bool active: false
    property bool actionable: true
    property int glyphSize: 15

    signal clicked()

    readonly property bool hovered: mouse.containsMouse
    readonly property bool pressed: mouse.pressed

    implicitWidth: contentRow.implicitWidth + 26
    implicitHeight: 34
    radius: Style.radiusSmall

    color: !actionable ? "transparent"
        : pressed ? Theme.pressed
        : (active || hovered) ? (accent ? Theme.accentDeep : Theme.hover)
        : (accent ? Theme.accent : "transparent")

    border.width: active && !accent ? 1 : 0
    border.color: Theme.accent

    Behavior on color { ColorAnimation { duration: Style.animFast } }
    opacity: actionable ? 1.0 : 0.45

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 6

        Text {
            visible: control.glyph !== ""
            text: control.glyph
            font.pixelSize: control.glyphSize
            color: control.accent ? "#ffffff" : Theme.text
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.label !== ""
            text: control.label
            font.pixelSize: Style.fontSize
            font.weight: Font.Medium
            color: control.accent ? "#ffffff" : Theme.text
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: control.actionable
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
