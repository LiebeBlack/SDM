import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Monitor de hilos en tiempo real (spec 4.2.3 - Segment Visualizer).
// Barras animadas por segmento con colores de estado y telemetría.
Rectangle {
    id: segmentPanel

    property var taskId: 0
    property var info: ({})
    property int segmentCount: 0

    height: Style.segmentHeight
    color: Theme.surface

    // Borde superior sutil.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.border
    }

    function refresh() {
        if (taskId && taskId > 0) {
            info = bridge.taskInfo(taskId)
            segmentCount = segmentsModel.rowCount()
        } else {
            info = ({})
            segmentCount = segmentsModel.rowCount()
        }
    }

    // Muestreo a 2 Hz (suficiente para el monitor y barato en recursos).
    Timer {
        interval: 500
        running: true
        repeat: true
        onTriggered: segmentPanel.refresh()
    }

    Component.onCompleted: refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // Titulo + resumen
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "INFORMACIÓN DE SEGMENTOS"
                font.pixelSize: Style.fontSizeSmall
                font.weight: Font.DemiBold
                color: Theme.subtleText
            }
            Rectangle {
                radius: Style.radiusPill
                color: Theme.bg
                border.color: Theme.border
                implicitWidth: hilosText.implicitWidth + 16
                implicitHeight: 20
                Text {
                    id: hilosText
                    anchors.centerIn: parent
                    text: "HILOS: " + (segmentPanel.info.threads !== undefined ? segmentPanel.info.threads : 1)
                          + (segmentPanel.info.state === 2 ? " ACTIVOS" : " EN ESPERA")
                    font.pixelSize: Style.fontSizeSmall
                    font.family: Style.fontMono
                    color: Theme.text
                }
            }
            Rectangle {
                visible: segmentPanel.info.stateText !== undefined
                radius: Style.radiusPill
                color: Theme.bg
                border.color: Theme.border
                implicitWidth: estadoText.implicitWidth + 16
                implicitHeight: 20
                Text {
                    id: estadoText
                    anchors.centerIn: parent
                    text: (segmentPanel.info.stateIcon !== undefined ? segmentPanel.info.stateIcon + " " : "")
                          + (segmentPanel.info.stateText !== undefined ? segmentPanel.info.stateText : "")
                    font.pixelSize: Style.fontSizeSmall
                    color: Theme.stateColor(segmentPanel.info.state !== undefined ? segmentPanel.info.state : -1)
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: segmentPanel.info.name !== undefined && segmentPanel.info.name !== "" ? segmentPanel.info.name : "Sin selección"
                font.pixelSize: Style.fontSize
                color: Theme.subtleText
                elide: Text.ElideRight
                Layout.maximumWidth: 380
            }
        }

        // Barras de segmentos (colores por estado, animadas)
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            spacing: 4

            Repeater {
                model: segmentsModel
                delegate: Rectangle {
                    required property var model
                    Layout.fillWidth: true
                    Layout.minimumWidth: 8
                    Layout.preferredHeight: 24
                    radius: 3
                    color: Theme.progressTrack
                    clip: true

                    Rectangle {
                        width: parent.width * Math.min(1.0, Math.max(0.0, model.progress))
                        height: parent.height
                        radius: 3
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0.0; color: Qt.lighter(model.color, 1.2) }
                            GradientStop { position: 1.0; color: model.color }
                        }
                        Behavior on width { NumberAnimation { duration: 180; easing.type: Style.easeOut } }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: model.segmentIndex + 1
                        font.pixelSize: Style.fontSizeSmall
                        font.family: Style.fontMono
                        color: "#ffffff"
                        style: Text.Raised
                        styleColor: "#66000000"
                        visible: parent.width > 22
                    }
                    ToolTip.visible: segMouse.containsMouse
                    ToolTip.delay: 300
                    ToolTip.text: model.stateText + " · " + model.rangeText
                    MouseArea {
                        id: segMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
            }

            Text {
                visible: segmentPanel.segmentCount === 0
                text: "Sin segmentos activos (seleccione una descarga)"
                font.pixelSize: Style.fontSizeSmall
                color: Theme.subtleText
            }
        }

        // Fila de telemetria global
        RowLayout {
            Layout.fillWidth: true
            spacing: 18

            Text {
                text: "Velocidad Global: " + bridge.globalSpeedText
                font.pixelSize: Style.fontSizeSmall
                font.family: Style.fontMono
                color: Theme.success
            }
            Text {
                text: "Restante: " + (segmentPanel.info.etaText !== undefined ? segmentPanel.info.etaText : "--")
                font.pixelSize: Style.fontSizeSmall
                font.family: Style.fontMono
                color: Theme.subtleText
            }
            Text {
                text: "Recibido: " + (segmentPanel.info.receivedText !== undefined ? segmentPanel.info.receivedText : "--")
                      + " / " + (segmentPanel.info.sizeText !== undefined ? segmentPanel.info.sizeText : "--")
                font.pixelSize: Style.fontSizeSmall
                font.family: Style.fontMono
                color: Theme.subtleText
            }
            Text {
                text: "Disco: " + (segmentPanel.info.progressText !== undefined ? segmentPanel.info.progressText : "--")
                font.pixelSize: Style.fontSizeSmall
                font.family: Style.fontMono
                color: Theme.subtleText
            }
            Text {
                text: "RAM: " + bridge.memoryMb.toFixed(1) + " MB"
                font.pixelSize: Style.fontSizeSmall
                font.family: Style.fontMono
                color: Theme.subtleText
            }

            Item { Layout.fillWidth: true }

            // Mensaje de error prominente.
            Rectangle {
                visible: segmentPanel.info.error !== undefined && segmentPanel.info.error !== ""
                radius: Style.radiusSmall
                color: Theme.dangerSoft
                implicitWidth: errText.implicitWidth + 16
                implicitHeight: 20

                Text {
                    id: errText
                    anchors.centerIn: parent
                    text: segmentPanel.info.error !== undefined ? segmentPanel.info.error : ""
                    font.pixelSize: Style.fontSizeSmall
                    color: Theme.danger
                    elide: Text.ElideRight
                }
                Layout.maximumWidth: 420
            }
        }
    }
}
