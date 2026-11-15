import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Tabla principal virtualizada (spec 4.2.2): ListView solo instancia las
// filas visibles, por que soporta listas de 50.000+ descargas sin coste.
Rectangle {
    id: table

    signal newTaskRequested()

    color: Theme.bg

    // ------------------------------------------------------------------
    // Cabecera de columnas
    // ------------------------------------------------------------------
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Style.headerHeight
        color: Theme.surfaceAlt
        border.color: Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6

            Text { text: "EST."; Layout.preferredWidth: Style.colState - 14; font.pixelSize: Style.fontSizeHeader; font.weight: Font.DemiBold; color: Theme.subtleText; elide: Text.ElideRight }
            Text { text: "NOMBRE DEL ARCHIVO"; Layout.preferredWidth: Style.colName - 6; font.pixelSize: Style.fontSizeHeader; font.weight: Font.DemiBold; color: Theme.subtleText; elide: Text.ElideRight }
            Text { text: "TAMAÑO"; Layout.preferredWidth: Style.colSize - 6; font.pixelSize: Style.fontSizeHeader; font.weight: Font.DemiBold; color: Theme.subtleText; horizontalAlignment: Text.AlignRight }
            Text { text: "PROGRESO"; Layout.preferredWidth: Style.colProgress - 6; font.pixelSize: Style.fontSizeHeader; font.weight: Font.DemiBold; color: Theme.subtleText }
            Text { text: "VELOCIDAD"; Layout.preferredWidth: Style.colSpeed - 6; font.pixelSize: Style.fontSizeHeader; font.weight: Font.DemiBold; color: Theme.subtleText; horizontalAlignment: Text.AlignRight }
            Text { text: "RESTANTE"; Layout.preferredWidth: Style.colEta - 6; font.pixelSize: Style.fontSizeHeader; font.weight: Font.DemiBold; color: Theme.subtleText; horizontalAlignment: Text.AlignRight }
            Text { text: "HILOS"; Layout.preferredWidth: Style.colThreads - 6; font.pixelSize: Style.fontSizeHeader; font.weight: Font.DemiBold; color: Theme.subtleText; horizontalAlignment: Text.AlignRight }
            Text { text: "AÑADIDA"; Layout.preferredWidth: Style.colDate; font.pixelSize: Style.fontSizeHeader; font.weight: Font.DemiBold; color: Theme.subtleText; horizontalAlignment: Text.AlignRight }
        }
    }

    // ------------------------------------------------------------------
    // Lista de descargas
    // ------------------------------------------------------------------
    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: 12
        model: tasksModel
        clip: true
        spacing: 1
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            id: scrollBar
            policy: ScrollBar.AsNeeded
        }

        delegate: Item {
            id: row
            width: list.width
            height: Style.rowHeight

            required property int index
            required property var model

            // Fondo de fila
            Rectangle {
                anchors.fill: parent
                color: ListView.isCurrentItem ? Theme.selection
                    : (rowMouse.containsMouse ? Theme.hover : Theme.surface)
                Behavior on color { ColorAnimation { duration: Style.animFast } }
            }

            // Indicador de actividad (progreso tenue tras la fila)
            Rectangle {
                width: parent.width * Math.min(1.0, Math.max(0.0, row.model.progress))
                height: parent.height
                color: Theme.accent
                opacity: row.model.state === 2 ? 0.10 : 0.0
                Behavior on opacity { NumberAnimation { duration: Style.animNormal } }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                // Estado
                Text {
                    Layout.preferredWidth: Style.colState - 14
                    text: row.model.stateIcon
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.text
                }

                // Nombre
                Text {
                    Layout.preferredWidth: Style.colName - 6
                    text: row.model.name
                    font.pixelSize: Style.fontSize
                    color: Theme.text
                    elide: Text.ElideRight
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        ToolTip.visible: containsMouse && parent.implicitWidth > parent.width
                        ToolTip.text: row.model.name + "\n" + row.model.savePath
                        ToolTip.delay: 700
                    }
                }

                // Tamano
                Text {
                    Layout.preferredWidth: Style.colSize - 6
                    text: row.model.sizeText
                    font.pixelSize: Style.fontSize
                    color: Theme.subtleText
                    horizontalAlignment: Text.AlignRight
                }

                // Progreso (barra + porcentaje)
                Item {
                    Layout.preferredWidth: Style.colProgress - 6
                    Layout.preferredHeight: 20

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 8
                        radius: 4
                        color: Theme.progressTrack
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * Math.min(1.0, Math.max(0.0, row.model.progress))
                        height: 8
                        radius: 4
                        color: Theme.stateColor(row.model.state)
                        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.progressText
                        font.pixelSize: Style.fontSizeSmall
                        font.family: "Consolas, 'DejaVu Sans Mono', monospace"
                        color: Theme.text
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.receivedText
                        font.pixelSize: Style.fontSizeSmall
                        color: Theme.subtleText
                    }
                }

                // Velocidad
                Text {
                    Layout.preferredWidth: Style.colSpeed - 6
                    text: row.model.speedText
                    font.pixelSize: Style.fontSize
                    font.family: "Consolas, 'DejaVu Sans Mono', monospace"
                    horizontalAlignment: Text.AlignRight
                    color: row.model.state === 2 ? Theme.success : Theme.subtleText
                }

                // Tiempo restante
                Text {
                    Layout.preferredWidth: Style.colEta - 6
                    text: row.model.etaText
                    font.pixelSize: Style.fontSize
                    font.family: "Consolas, 'DejaVu Sans Mono', monospace"
                    horizontalAlignment: Text.AlignRight
                    color: Theme.subtleText
                }

                // Hilos
                Text {
                    Layout.preferredWidth: Style.colThreads - 6
                    text: row.model.threads + (row.model.isStream ? " ▶" : "")
                    font.pixelSize: Style.fontSize
                    horizontalAlignment: Text.AlignRight
                    color: Theme.subtleText
                }

                // Fecha
                Text {
                    Layout.preferredWidth: Style.colDate
                    text: row.model.createdText
                    font.pixelSize: Style.fontSizeSmall
                    horizontalAlignment: Text.AlignRight
                    color: Theme.subtleText
                }
            }

            // Error explicito (una linea, solo si existe)
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 2
                visible: row.model.error !== "" && row.model.state !== 4
                color: Theme.danger
            }

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    list.currentIndex = row.index
                    bridge.selectTask(row.model.taskId)
                    if (mouse.button === Qt.RightButton) {
                        contextMenu.popup()
                    }
                }
                onDoubleClicked: bridge.openFileFor(row.model.taskId)
            }

            Menu {
                id: contextMenu
                width: 240

                MenuItem {
                    text: row.model.state === 2 || row.model.state === 1 ? "Pausar" : "Reanudar"
                    onTriggered: {
                        if (row.model.state === 2 || row.model.state === 1) {
                            tasksModel.pauseAt(row.index)
                        } else {
                            tasksModel.resumeAt(row.index)
                        }
                    }
                }
                MenuItem {
                    text: "Reintentar"
                    enabled: row.model.state === 5 || row.model.state === 6
                    onTriggered: tasksModel.retryAt(row.index)
                }
                MenuItem {
                    text: "Abrir archivo"
                    enabled: row.model.isFinished
                    onTriggered: bridge.openFileFor(row.model.taskId)
                }
                MenuItem {
                    text: "Abrir carpeta"
                    onTriggered: bridge.openFolderFor(row.model.taskId)
                }
                MenuItem {
                    text: "Copiar URL"
                    onTriggered: bridge.copyText(row.model.url)
                }
                MenuItem {
                    text: "Copiar ruta del archivo"
                    onTriggered: bridge.copyText(row.model.savePath)
                }

                Menu {
                    title: "Número de hilos"
                    MenuItem { text: "1 hilo";  onTriggered: bridge.setThreads(row.model.taskId, 1) }
                    MenuItem { text: "6 hilos"; onTriggered: bridge.setThreads(row.model.taskId, 6) }
                    MenuItem { text: "8 hilos"; onTriggered: bridge.setThreads(row.model.taskId, 8) }
                    MenuItem { text: "16 hilos"; onTriggered: bridge.setThreads(row.model.taskId, 16) }
                }

                MenuSeparator {}

                MenuItem {
                    text: "Eliminar…"
                    onTriggered: {
                        deleteDialog.taskId = row.model.taskId
                        deleteDialog.taskName = row.model.name
                        deleteDialog.open()
                    }
                }
            }
        }

        // Estado vacio
        Column {
            anchors.centerIn: parent
            visible: list.count === 0
            spacing: 10

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "📥"
                font.pixelSize: 42
                color: Theme.subtleText
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "No hay descargas en esta vista"
                font.pixelSize: Style.fontSizeTitle
                color: Theme.text
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Pulsa «＋ Nueva» o captura un enlace desde tu navegador"
                font.pixelSize: Style.fontSize
                color: Theme.subtleText
            }
            SdmToolButton {
                anchors.horizontalCenter: parent.horizontalCenter
                glyph: "＋"
                label: "Nueva descarga"
                accent: true
                onClicked: table.newTaskRequested()
            }
        }
    }

    // ------------------------------------------------------------------
    // Confirmacion de borrado
    // ------------------------------------------------------------------
    Dialog {
        id: deleteDialog
        property var taskId: 0
        property string taskName: ""

        modal: true
        anchors.centerIn: parent
        width: 440
        padding: Style.paddingLarge
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.surface
            radius: Style.radius
            border.color: Theme.border
        }

        title: "Eliminar descarga"
        titleItem.color: Theme.text
        titleItem.font.pixelSize: Style.fontSizeTitle

        ColumnLayout {
            width: parent.width
            spacing: Style.padding

            Text {
                Layout.fillWidth: true
                text: "¿Eliminar «" + deleteDialog.taskName + "» de la lista?"
                font.pixelSize: Style.fontSize
                color: Theme.text
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: "La casilla siguiente decide si también se borra el archivo del disco."
                font.pixelSize: Style.fontSizeSmall
                color: Theme.subtleText
                wrapMode: Text.Wrap
            }

            CheckBox {
                id: deleteFileCheck
                text: "Eliminar también el archivo"
                checked: false
                contentItem: Text {
                    text: deleteFileCheck.text
                    color: Theme.text
                    font.pixelSize: Style.fontSize
                    leftPadding: deleteFileCheck.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Style.spacing

                SdmToolButton {
                    label: "Cancelar"
                    onClicked: deleteDialog.close()
                }
                Rectangle {
                    Layout.preferredWidth: cancelRowWidth
                    Layout.preferredHeight: 34
                    radius: Style.radiusSmall
                    color: deleteRowMouse.containsMouse ? "#c93b2f" : Theme.danger
                    property int cancelRowWidth: 110
                    Text {
                        anchors.centerIn: parent
                        text: "Eliminar"
                        font.pixelSize: Style.fontSize
                        font.weight: Font.Medium
                        color: "#ffffff"
                    }
                    MouseArea {
                        id: deleteRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            tasksModel.removeAt(tasksModel.rowForTask(deleteDialog.taskId), deleteFileCheck.checked)
                            deleteDialog.close()
                        }
                    }
                }
            }
        }
    }
}
