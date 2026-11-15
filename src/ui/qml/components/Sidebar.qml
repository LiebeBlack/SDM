import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Panel lateral de categorias y filtros de estado (spec 4.2.1).
// Indicador de seleccion animado + badges de recuento.
Rectangle {
    id: sidebar

    property string libraryKey: "all"
    property string categoryKey: "none"
    property var countsArr: tasksModel.counts

    color: Theme.surface

    // Borde derecho sutil.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    function totalCount() {
        var sum = 0
        for (var i = 0; i < 7; ++i) sum += countsArr[i]
        return sum
    }

    function activateLibrary(item) {
        libraryKey = item.key
        tasksModel.setStateFilter(item.value)
    }

    function activateCategory(item) {
        categoryKey = item.key
        tasksModel.setCategoryFilter(item.value)
    }

    // Componente de fila con badge de recuento.
    component NavRow: Rectangle {
        id: navRow
        property string key: ""
        property string label: ""
        property string glyph: ""
        property int count: 0
        property bool selected: false

        signal triggered()

        Layout.fillWidth: true
        Layout.preferredHeight: 32
        radius: Style.radiusSmall
        color: selected ? Theme.selection : (navMouse.containsMouse ? Theme.hover : "transparent")
        Behavior on color { ColorAnimation { duration: Style.animFast } }

        // Indicador lateral de seleccion.
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: navRow.selected ? 18 : 0
            radius: 2
            color: Theme.accent
            Behavior on height { NumberAnimation { duration: Style.animNormal; easing.type: Style.easeOut } }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 8
            spacing: 8
            Text { text: navRow.glyph; font.pixelSize: 13; color: Theme.text }
            Text {
                text: navRow.label
                font.pixelSize: Style.fontSize
                font.weight: navRow.selected ? Font.DemiBold : Font.Normal
                color: Theme.text
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            // Badge de recuento.
            Rectangle {
                visible: navRow.count >= 0
                radius: Style.radiusPill
                color: navRow.selected ? Theme.accent : Theme.surfaceAlt
                implicitWidth: countText.implicitWidth + 12
                implicitHeight: 18
                Behavior on color { ColorAnimation { duration: Style.animFast } }

                Text {
                    id: countText
                    anchors.centerIn: parent
                    text: navRow.count
                    font.pixelSize: Style.fontSizeTiny
                    font.family: Style.fontMono
                    color: navRow.selected ? "#ffffff" : Theme.subtleText
                }
            }
        }

        MouseArea {
            id: navMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: navRow.triggered()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        Text {
            text: "BIBLIOTECA"
            font.pixelSize: Style.fontSizeSmall
            font.weight: Font.DemiBold
            color: Theme.subtleText
            Layout.leftMargin: 8
            Layout.topMargin: 6
            Layout.bottomMargin: 2
        }

        Repeater {
            model: ListModel {
                id: libraryModel
                ListElement { key: "all"; label: "Todas"; glyph: "📥"; kind: 0; value: -1 }
                ListElement { key: "downloading"; label: "En curso"; glyph: "⏬"; kind: 1; value: 2 }
                ListElement { key: "paused"; label: "Pausadas"; glyph: "⏸"; kind: 1; value: 3 }
                ListElement { key: "completed"; label: "Completadas"; glyph: "✔"; kind: 1; value: 4 }
                ListElement { key: "failed"; label: "Fallidas"; glyph: "✖"; kind: 1; value: 5 }
                ListElement { key: "queued"; label: "En cola"; glyph: "⏳"; kind: 1; value: 0 }
                ListElement { key: "expired"; label: "Expirados"; glyph: "🔑"; kind: 1; value: 6 }
            }
            delegate: NavRow {
                required property var model
                key: model.key
                label: model.label
                glyph: model.glyph
                selected: sidebar.libraryKey === model.key
                count: {
                    if (model.kind === 0) return sidebar.totalCount()
                    return sidebar.countsArr[7 + model.value]
                }
                onTriggered: sidebar.activateLibrary(model)
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.topMargin: 6; Layout.bottomMargin: 6 }

        Text {
            text: "CATEGORÍAS"
            font.pixelSize: Style.fontSizeSmall
            font.weight: Font.DemiBold
            color: Theme.subtleText
            Layout.leftMargin: 8
            Layout.bottomMargin: 2
        }

        Repeater {
            model: ListModel {
                id: categoryModel
                ListElement { key: "video"; label: "Videos"; glyph: "🎥"; kind: 2; value: 1 }
                ListElement { key: "audio"; label: "Música"; glyph: "🎵"; kind: 2; value: 2 }
                ListElement { key: "archive"; label: "Comprimidos"; glyph: "📦"; kind: 2; value: 3 }
                ListElement { key: "document"; label: "Documentos"; glyph: "📄"; kind: 2; value: 4 }
                ListElement { key: "program"; label: "Programas"; glyph: "⚙️"; kind: 2; value: 5 }
                ListElement { key: "image"; label: "Imágenes"; glyph: "🖼️"; kind: 2; value: 6 }
                ListElement { key: "other"; label: "Otros"; glyph: "🗂️"; kind: 2; value: 0 }
            }
            delegate: NavRow {
                required property var model
                key: model.key
                label: model.label
                glyph: model.glyph
                selected: sidebar.categoryKey === model.key
                count: sidebar.countsArr[model.value]
                onTriggered: sidebar.activateCategory(model)
            }
        }

        Item { Layout.fillWidth: true; Layout.fillHeight: true }

        // Resumen inferior
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            radius: Style.radiusSmall
            color: Theme.bg
            border.color: Theme.border

            Column {
                anchors.centerIn: parent
                spacing: 3
                Text {
                    text: "Activas: " + bridge.activeCount + "   Cola: " + bridge.queuedCount
                    font.pixelSize: Style.fontSizeSmall
                    color: Theme.subtleText
                }
                Text {
                    text: "RAM: " + bridge.memoryMb.toFixed(1) + " MB"
                    font.pixelSize: Style.fontSizeSmall
                    font.family: Style.fontMono
                    color: Theme.subtleText
                }
            }
        }
    }

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: bridge.refreshStats()
    }
}
