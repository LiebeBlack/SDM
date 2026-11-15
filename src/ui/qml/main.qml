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
            Layout.preferredHeight: 28
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
                    font.family: "Consolas, 'DejaVu Sans Mono', monospace"
                    color: Theme.success
                }
                Text {
                    text: "RAM: " + bridge.memoryMb.toFixed(1) + " MB"
                    font.pixelSize: Style.fontSizeSmall
                    font.family: "Consolas, 'DejaVu Sans Mono', monospace"
                    color: Theme.subtleText
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

        title: "Seleccionar calidad"
        titleItem.color: Theme.text
        titleItem.font.pixelSize: Style.fontSizeTitle

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
                    border.color: Theme.border

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

        footer: RowLayout {
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

    // --------------------------------------------------------------
    // Avisos flotantes (toasts)
    // --------------------------------------------------------------
    Rectangle {
        id: toast
        property string message: ""
        property int level: 0

        function show(text, lvl) {
            message = text
            level = lvl
            visible = true
            hideTimer.restart()
        }

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 18
        z: 200
        width: Math.min(460, toastText.implicitWidth + 32)
        height: toastText.implicitHeight + 20
        radius: Style.radius
        color: level === 2 ? Theme.danger : (level === 1 ? Theme.warning : Theme.surface)
        border.color: Theme.border
        opacity: 0.97
        visible: false

        Behavior on opacity { NumberAnimation { duration: 150 } }

        Text {
            id: toastText
            anchors.centerIn: parent
            width: parent.width - 28
            text: toast.message
            font.pixelSize: Style.fontSize
            color: toast.level >= 1 ? "#ffffff" : Theme.text
            wrapMode: Text.Wrap
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                hideTimer.stop()
                toast.visible = false
            }
        }

        Timer {
            id: hideTimer
            interval: 4500
            onTriggered: toast.visible = false
        }
    }
}
