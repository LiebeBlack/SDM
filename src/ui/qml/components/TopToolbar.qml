import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Barra de herramientas principal (spec 4.1).
Rectangle {
    id: toolbar

    signal newTaskRequested()
    signal resumeAllRequested()
    signal pauseAllRequested()
    signal settingsRequested()

    height: Style.toolbarHeight
    color: Theme.surface
    border.color: Theme.border
    border.width: 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Style.padding
        anchors.rightMargin: Style.padding
        spacing: Style.spacing

        // Marca
        Row {
            spacing: 8
            Text {
                text: "⬇"
                font.pixelSize: 20
                color: Theme.accent
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "SDM"
                font.pixelSize: Style.fontSizeTitle
                font.weight: Font.Bold
                color: Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "S-Download Manager"
                font.pixelSize: Style.fontSizeSmall
                color: Theme.subtleText
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border; Layout.topMargin: 8; Layout.bottomMargin: 8 }

        SdmToolButton {
            glyph: "＋"
            label: "Nueva"
            accent: true
            onClicked: toolbar.newTaskRequested()
        }
        SdmToolButton {
            glyph: "▶"
            label: "Reanudar"
            onClicked: toolbar.resumeAllRequested()
        }
        SdmToolButton {
            glyph: "⏸"
            label: "Pausar"
            onClicked: toolbar.pauseAllRequested()
        }
        SdmToolButton {
            glyph: "⏹"
            label: "Detener"
            onClicked: toolbar.pauseAllRequested()
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border; Layout.topMargin: 8; Layout.bottomMargin: 8 }

        SdmToolButton {
            glyph: "⚙"
            label: "Ajustes"
            onClicked: toolbar.settingsRequested()
        }

        Item { Layout.fillWidth: true }

        // Busqueda en tiempo real
        Rectangle {
            Layout.preferredWidth: 240
            Layout.preferredHeight: 32
            radius: Style.radiusSmall
            color: Theme.bg
            border.color: searchField.activeFocus ? Theme.accent : Theme.border
            border.width: 1

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: "🔍"
                font.pixelSize: 13
                color: Theme.subtleText
            }
            TextField {
                id: searchField
                anchors.fill: parent
                anchors.leftMargin: 30
                anchors.rightMargin: 8
                background: Item {}
                placeholderText: "Buscar…"
                placeholderTextColor: Theme.subtleText
                color: Theme.text
                font.pixelSize: Style.fontSize
                onTextChanged: tasksModel.setSearchText(text)
            }
        }

        // Velocidad global (barra de estado integrada)
        Text {
            text: bridge.globalSpeedText
            font.pixelSize: Style.fontSize
            font.family: "Consolas, 'DejaVu Sans Mono', monospace"
            color: Theme.success
        }
    }
}
