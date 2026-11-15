import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Tabla principal virtualizada (spec 4.2.2): ListView solo instancia las
// filas visibles, por lo que soporta listas de 50.000+ descargas sin coste.
// Renderizado Fluent: píldoras de estado, barras con degradado y
// animaciones de entrada/selección.
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
        reuseItems: true
        ScrollBar.vertical: ScrollBar {
            id: scrollBar
            policy: ScrollBar.AsNeeded
        }

        // Animacion de entrada de filas nuevas.
        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
            NumberAnimation { property: "x"; from: -24; to: 0; duration: Style.animNormal; easing.type: Style.easeOut }
        }

        delegate: Item {
            id: row
            width: list.width
            height: Style.rowHeight

            required property int index
            required property var model

            readonly property color stateColor: Theme.stateColor(row.model.state)

            // Fondo de fila
            Rectangle {
                anchors.fill: parent
                color: ListView.isCurrentItem ? Theme.selection
                    : (rowMouse.containsMouse ? Theme.hover : Theme.surface)
                Behavior on color { ColorAnimation { duration: Style.animFast } }
            }

            // Indicador lateral de estado (barra de acento a la izquierda).
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 3
                color: row.stateColor
                opacity: row.model.state === 2 ? 1.0 : (ListView.isCurrentItem ? 0.8 : 0.35)
                Behavior on opacity { NumberAnimation { duration: Style.animNormal } }
            }

            // Indicador de actividad (progreso tenue tras la fila).
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

                // Estado (píldora animada)
                Rectangle {
                    Layout.preferredWidth: Style.colState - 14
                    Layout.preferredHeight: 22
                    radius: Style.radiusPill
                    color: Qt.rgba(row.stateColor.r, row.stateColor.g, row.stateColor.b, 0.16)

                    Text {
                        anchors.centerIn: parent
                        text: row.model.stateIcon
                        font.pixelSize: 13
                        color: row.stateColor
                    }
                }

                // Nombre + ruta
                Column {
                    Layout.preferredWidth: Style.colName - 6
                    spacing: 1

                    Text {
                        width: parent.width
                        text: row.model.name
                        font.pixelSize: Style.fontSize
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: row.model.categoryIcon + " " + row.model.categoryText
                              + (row.model.isStream ? " · ▶ stream" : "")
                        font.pixelSize: Style.fontSizeTiny
                        color: Theme.subtleText
                        elide: Text.ElideRight
                    }
                }

                // Tamaño
                Text {
                    Layout.preferredWidth: Style.colSize - 6
                    text: row.model.sizeText
                    font.pixelSize: Style.fontSize
                    color: Theme.subtleText
                    horizontalAlignment: Text.AlignRight
                }

                // Progreso (barra con degradado + porcentaje)
                Item {
                    Layout.preferredWidth: Style.colProgress - 6
                    Layout.preferredHeight: 22

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 8
                        radius: 4
                        color: Theme.progressTrack
                    }
                    Rectangle {
                        id: progressFill
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * Math.min(1.0, Math.max(0.0, row.model.progress))
                        height: 8
                        radius: 4
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: row.stateColor }
                            GradientStop { position: 1.0; color: Qt.lighter(row.stateColor, 1.25) }
                        }
                        Behavior on width { NumberAnimation { duration: 180; easing.type: Style.easeOut } }
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.progressText
                        font.pixelSize: Style.fontSizeSmall
                        font.family: Style.fontMono
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
                    font.family: Style.fontMono
                    horizontalAlignment: Text.AlignRight
                    color: row.model.state === 2 ? Theme.success : Theme.subtleText
                }

                // Tiempo restante
                Text {
                    Layout.preferredWidth: Style.colEta - 6
                    text: row.model.etaText
                    font.pixelSize: Style.fontSize
                    font.family: Style.fontMono
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
                ToolTip.visible: containsMouse && !contextMenu.visible
                ToolTip.text: row.model.name + "\n" + row.model.savePath
                ToolTip.delay: 700
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

        // Estado vacio (icono con pulso suave)
        Column {
            anchors.centerIn: parent
            visible: list.count === 0
            spacing: 10

            Text {
                id: emptyIcon
                anchors.horizontalCenter: parent.horizontalCenter
                text: "📥"
                font.pixelSize: 42
                color: Theme.subtleText
                opacity: 0.6
                SequentialAnimation on opacity {
                    running: list.count === 0
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.45; to: 0.9; duration: 1400; easing.type: Style.easeInOut }
                    NumberAnimation { from: 0.9; to: 0.45; duration: 1400; easing.type: Style.easeInOut }
                }
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

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.animFast; easing.type: Style.easeInOut }
        }

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
                    scale: deleteRowMouse.pressed ? 0.96 : 1.0
                    Behavior on color { ColorAnimation { duration: Style.animFast } }
                    Behavior on scale { NumberAnimation { duration: Style.animFast } }
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
