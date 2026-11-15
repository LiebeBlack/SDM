import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "theme"

// Ventana principal - SDM (spec 4.1: layout Fluent con panel lateral,
// tabla virtualizada, visualizador de segmentos y barra de estado).
ApplicationWindow {
    id: root

    width: 1320
    height: 840
    minimumWidth: 1040
    minimumHeight: 660
    visible: true
    title: "SDM - S-Download Manager Pro"
    color: bridge.micaEnabled ? "transparent" : Theme.bg

    property var selectedTaskId: 0

    // Fondo con degradado sutil (solo cuando Mica esta activo no pinta).
    Rectangle {
        anchors.fill: parent
        visible: !bridge.micaEnabled
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.bgGradientStart }
            GradientStop { position: 1.0; color: Theme.bgGradientEnd }
        }
    }

    // Sincronizacion del tema con los ajustes de la aplicación.
    Connections {
        target: bridge
        function onChromeChanged() {
            Theme.dark = bridge.darkMode
        }
        function onTaskSelected(id) {
            root.selectedTaskId = id
        }
        function onToast(message, level) {
            toast.show(message, level)
        }
        function onStreamVariantsReady(id, variants, manifestUrl) {
            variantsDialog.openFor(id, variants, manifestUrl)
        }
    }

    Component.onCompleted: {
        Theme.dark = bridge.darkMode
        tasksModel.clearFilters()
    }

    Shortcut {
        sequence: "Ctrl+N"
        onActivated: newTaskModal.prepare({})
    }
    Shortcut {
        sequence: "Ctrl+Shift+P"
        onActivated: bridge.pauseAll()
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        onActivated: bridge.resumeAll()
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+Delete"
        onActivated: bridge.clearCompleted()
    }
    Shortcut {
        sequence: "Ctrl+O"
        onActivated: bridge.openFolderFor(root.selectedTaskId)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --------------------------------------------------------------
        // Barra de herramientas
        // --------------------------------------------------------------
        TopToolbar {
            Layout.fillWidth: true
            onNewTaskRequested: newTaskModal.prepare({})
            onResumeAllRequested: bridge.resumeAll()
            onPauseAllRequested: bridge.pauseAll()
            onClearCompletedRequested: bridge.clearCompleted()
            onSettingsRequested: settingsDialog.open()
        }

        // --------------------------------------------------------------
        // Cuerpo: panel lateral + tabla + monitor de segmentos
        // --------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                Layout.preferredWidth: Style.sidebarWidth
                Layout.fillHeight: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                DownloadTable {
                    id: table
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onNewTaskRequested: newTaskModal.prepare({})
                }

                SegmentBar {
                    id: segmentBar
                    Layout.fillWidth: true
                    taskId: root.selectedTaskId
                }
            }
        }

        // --------------------------------------------------------------
        // Barra de estado
        // --------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: Theme.surfaceAlt
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 18

                Text {
                    text: "Total: " + bridge.totalCount
                    font.pixelSize: Style.fontSizeSmall
                    color: Theme.subtleText
                }
                Text {
                    text: "Activas: " + bridge.activeCount
                    font.pixelSize: Style.fontSizeSmall
                    color: Theme.success
                }
                Text {
                    text: "En cola: " + bridge.queuedCount
                    font.pixelSize: Style.fontSizeSmall
                    color: Theme.warning
                }
                Text {
                    text: "▼ " + bridge.globalSpeedText
                    font.pixelSize: Style.fontSizeSmall
                    font.family: Style.fontMono
                    color: Theme.success
                }

                // Grafico de velocidad en vivo (muestreo 1 s, 90 puntos).
                SpeedGraph {
                    Layout.preferredWidth: 150
                    Layout.preferredHeight: 20
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: bridge.connectedClients > 0
                          ? "🌐 Extensión conectada (" + bridge.connectedClients + ")"
                          : "🌐 Extensión no vinculada (Ajustes → Conexión)"
                    font.pixelSize: Style.fontSizeSmall
                    color: bridge.connectedClients > 0 ? Theme.success : Theme.subtleText
                }
                Text {
                    text: "RAM: " + bridge.memoryMb.toFixed(1) + " MB"
                    font.pixelSize: Style.fontSizeSmall
                    font.family: Style.fontMono
                    color: Theme.subtleText
                }
                Text {
                    text: "v" + appVersion
                    font.pixelSize: Style.fontSizeSmall
                    color: Theme.subtleText
                }
            }
        }
    }

    // --------------------------------------------------------------
    // Diálogos
    // --------------------------------------------------------------
    NewTaskModal {
        id: newTaskModal
    }

    SettingsDialog {
        id: settingsDialog
    }

    // Selección de calidad de un flujo multimedia (spec 4.2 / cap. 9)
    Dialog {
        id: variantsDialog
        property var taskId: 0
        property var variantsList: []
        property string manifestUrl: ""

        function openFor(id, variants, url) {
            taskId = id
            variantsList = variants || []
            manifestUrl = url
            open()
        }

        modal: true
        anchors.centerIn: parent
        width: 560
        padding: Style.paddingLarge
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // Entrada/salida animada (fade + escala).
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.animFast; easing.type: Style.easeInOut }
        }

        title: "Seleccionar calidad"
        // Cabecera propia con el título estilizado (Dialog no expone titleItem).
        header: Item {
            implicitHeight: variantsTitle.implicitHeight + 2 * Style.padding
            Text {
                id: variantsTitle
                anchors.left: parent.left
                anchors.leftMargin: Style.paddingLarge
                anchors.verticalCenter: parent.verticalCenter
                text: variantsDialog.title
                color: Theme.text
                font.pixelSize: Style.fontSizeTitle
                font.weight: Font.DemiBold
            }
        }

        background: Rectangle {
            color: Theme.surface
            radius: Style.radius
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: Style.spacing

            Text {
                Layout.fillWidth: true
                text: variantsDialog.manifestUrl
                font.pixelSize: Style.fontSizeSmall
                color: Theme.subtleText
                elide: Text.ElideMiddle
            }

            Text {
                visible: variantsDialog.variantsList.length === 0
                text: "No se detectaron calidades."
                font.pixelSize: Style.fontSize
                color: Theme.text
            }

            Repeater {
                model: variantsDialog.variantsList
                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 46
                    radius: Style.radiusSmall
                    color: variantMouse.containsMouse ? Theme.hover : Theme.bg
                    border.color: variantMouse.containsMouse ? Theme.accent : Theme.border
                    scale: variantMouse.containsMouse ? 1.01 : 1.0
                    Behavior on color { ColorAnimation { duration: Style.animFast } }
                    Behavior on scale { NumberAnimation { duration: Style.animFast; easing.type: Style.easeOut } }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10

                        Text {
                            text: "🎬"
                            font.pixelSize: 15
                            color: Theme.text
                        }
                        Column {
                            spacing: 2
                            Text {
                                text: modelData.label
                                font.pixelSize: Style.fontSize
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }
                            Text {
                                text: (modelData.resolution && modelData.resolution.length > 0
                                       ? modelData.resolution + " · " : "")
                                      + modelData.bandwidthText
                                      + (modelData.hasSeparateAudio ? " · audio separado" : "")
                                font.pixelSize: Style.fontSizeSmall
                                color: Theme.subtleText
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "Descargar →"
                            font.pixelSize: Style.fontSizeSmall
                            color: Theme.accent
                        }
                    }

                    MouseArea {
                        id: variantMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            bridge.selectStreamVariant(variantsDialog.taskId, index)
                            variantsDialog.close()
                        }
                    }
                }
            }
        }

        footer: Item {
            implicitHeight: variantsFooter.implicitHeight + 2 * Style.padding

            RowLayout {
                id: variantsFooter
                anchors.fill: parent
                anchors.margins: Style.padding
                spacing: Style.spacing
                Item { Layout.fillWidth: true }
                SdmToolButton {
                    label: "Cancelar"
                    onClicked: {
                        bridge.cancelStreamTask(variantsDialog.taskId)
                        variantsDialog.close()
                    }
                }
            }
        }
    }

    // --------------------------------------------------------------
    // Avisos flotantes (toasts animados: slide + fade)
    // --------------------------------------------------------------
    Rectangle {
        id: toast
        property string message: ""
        property int level: 0

        function show(text, lvl) {
            animOut.stop()
            message = text
            level = lvl
            visible = true
            animIn.restart()
            hideTimer.restart()
        }

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 18
        z: 200
        width: Math.min(460, toastText.implicitWidth + 64)
        height: Math.max(40, toastText.implicitHeight + 22)
        radius: Style.radius
        color: level === 2 ? Theme.danger : (level === 1 ? Theme.warning : Theme.surface)
        border.color: level === 2 ? Theme.danger : Theme.border
        opacity: 0
        visible: false

        // Icono por nivel.
        Text {
            id: toastIcon
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: toast.level === 2 ? "✕" : (toast.level === 1 ? "⚠" : "✓")
            font.pixelSize: 15
            font.weight: Font.Bold
            color: toast.level >= 1 ? Theme.onAccent : Theme.accent
        }

        Text {
            id: toastText
            anchors.left: toastIcon.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: toast.message
            font.pixelSize: Style.fontSize
            color: toast.level >= 1 ? Theme.onAccent : Theme.text
            wrapMode: Text.Wrap
        }

        // Sombra del toast.
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 3
            anchors.bottomMargin: -3
            radius: parent.radius
            color: Theme.shadow
            opacity: 0.4
            z: -1
        }

        // Animacion de entrada: deslizamiento lateral + fade.
        ParallelAnimation {
            id: animIn
            NumberAnimation { target: toast; property: "opacity"; from: 0; to: 0.98; duration: Style.animToast; easing.type: Style.easeOut }
            NumberAnimation { target: toast; property: "anchors.rightMargin"; from: -40; to: 18; duration: Style.animToast; easing.type: Style.easeOut }
        }

        // Animacion de salida: fade.
        ParallelAnimation {
            id: animOut
            NumberAnimation { target: toast; property: "opacity"; to: 0; duration: Style.animNormal; easing.type: Style.easeInOut }
            onStopped: toast.visible = false
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                hideTimer.stop()
                animOut.restart()
            }
        }

        Timer {
            id: hideTimer
            interval: 4500
            onTriggered: animOut.restart()
        }
    }
}
