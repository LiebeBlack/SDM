import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Barra de herramientas principal (spec 4.1): marca con degradado,
// acciones con tooltips y busqueda en tiempo real.
Rectangle {
    id: toolbar

    signal newTaskRequested()
    signal resumeAllRequested()
    signal pauseAllRequested()
    signal clearCompletedRequested()
    signal settingsRequested()

    height: Style.toolbarHeight
    color: Theme.surface

    // Borde inferior con degradado de marca.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.gradientStart }
            GradientStop { position: 0.5; color: Theme.border }
            GradientStop { position: 1.0; color: Theme.border }
        }
    }

    // Boton con tooltip reutilizable.
    component ToolAction: SdmToolButton {
        property string tip: ""
        ToolTip.visible: hovered && tip !== ""
        ToolTip.delay: 600
        ToolTip.text: tip
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Style.padding
        anchors.rightMargin: Style.padding
        spacing: Style.spacing

        // Marca (icono en chip con degradado)
        Row {
            spacing: 10

            Rectangle {
                width: 32
                height: 32
                radius: Style.radiusSmall
                anchors.verticalCenter: parent.verticalCenter
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Theme.gradientStart }
                    GradientStop { position: 1.0; color: Theme.gradientEnd }
                }
                Text {
                    anchors.centerIn: parent
                    text: "⬇"
                    font.pixelSize: 17
                    color: "#ffffff"
                }
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

        ToolAction {
            glyph: "＋"
            label: "Nueva"
            accent: true
            tip: "Nueva descarga (Ctrl+N)"
            onClicked: toolbar.newTaskRequested()
        }
        ToolAction {
            glyph: "▶"
            label: "Reanudar"
            tip: "Reanudar todas las descargas (Ctrl+Shift+R)"
            onClicked: toolbar.resumeAllRequested()
        }
        ToolAction {
            glyph: "⏸"
            label: "Pausar"
            tip: "Pausar todas las descargas (Ctrl+Shift+P)"
            onClicked: toolbar.pauseAllRequested()
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border; Layout.topMargin: 8; Layout.bottomMargin: 8 }

        ToolAction {
            glyph: "🧹"
            label: "Limpiar"
            tip: "Quitar de la lista las descargas completadas (Ctrl+Delete)"
            onClicked: toolbar.clearCompletedRequested()
        }
        ToolAction {
            glyph: "⚙"
            label: "Ajustes"
            tip: "Ajustes de la aplicación (Ctrl+,)"
            onClicked: toolbar.settingsRequested()
        }

        Item { Layout.fillWidth: true }

        // Busqueda en tiempo real (animacion de ancho sin tocar propiedades
        // Layout.*, que Qt Quick Layouts no permite animar).
        Item {
            Layout.preferredWidth: 280
            Layout.preferredHeight: 32

            Rectangle {
                id: searchBox
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: searchField.activeFocus ? 280 : 240
                height: 32
                radius: Style.radiusSmall
                color: Theme.bg
                border.color: searchField.activeFocus ? Theme.accent : Theme.border
                border.width: searchField.activeFocus ? 1.5 : 1

                Behavior on width { NumberAnimation { duration: Style.animNormal; easing.type: Style.easeOut } }
                Behavior on border.color { ColorAnimation { duration: Style.animFast } }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "🔍"
                    font.pixelSize: 13
                    color: searchField.activeFocus ? Theme.accent : Theme.subtleText
                    Behavior on color { ColorAnimation { duration: Style.animFast } }
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
        }

        // Velocidad global (barra de estado integrada)
        Text {
            text: bridge.globalSpeedText
            font.pixelSize: Style.fontSize
            font.family: Style.fontMono
            color: Theme.success
        }
    }
}
