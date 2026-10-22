import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Panel lateral de categorias y filtros de estado (spec 4.2.1).
Rectangle {
    id: sidebar

    property string libraryKey: "all"
    property string categoryKey: "none"
    property var countsArr: tasksModel.counts

    color: Theme.surface
    border.color: Theme.border
    border.width: 1

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

    ListModel {
        id: libraryModel
        ListElement { key: "all"; label: "Todas"; glyph: "📥"; kind: 0; value: -1 }
        ListElement { key: "downloading"; label: "En curso"; glyph: "⏬"; kind: 1; value: 2 }
        ListElement { key: "paused"; label: "Pausadas"; glyph: "⏸"; kind: 1; value: 3 }
        ListElement { key: "completed"; label: "Completadas"; glyph: "✔"; kind: 1; value: 4 }
        ListElement { key: "failed"; label: "Fallidas"; glyph: "✖"; kind: 1; value: 5 }
        ListElement { key: "queued"; label: "En cola"; glyph: "⏳"; kind: 1; value: 0 }
        ListElement { key: "expired"; label: "Expirados"; glyph: "🔑"; kind: 1; value: 6 }
    }

    ListModel {
        id: categoryModel
        ListElement { key: "video"; label: "Videos"; glyph: "🎥"; kind: 2; value: 1 }
        ListElement { key: "audio"; label: "Música"; glyph: "🎵"; kind: 2; value: 2 }
        ListElement { key: "archive"; label: "Comprimidos"; glyph: "📦"; kind: 2; value: 3 }
        ListElement { key: "document"; label: "Documentos"; glyph: "📄"; kind: 2; value: 4 }
        ListElement { key: "program"; label: "Programas"; glyph: "⚙️"; kind: 2; value: 5 }
        ListElement { key: "image"; label: "Imágenes"; glyph: "🖼️"; kind: 2; value: 6 }
        ListElement { key: "other"; label: "Otros"; glyph: "🗂️"; kind: 2; value: 0 }
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
            model: libraryModel
            delegate: Rectangle {
                required property var model
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                radius: Style.radiusSmall
                color: sidebar.libraryKey === model.key ? Theme.selection
                    : (rowMouse.containsMouse ? Theme.hover : "transparent")
                Behavior on color { ColorAnimation { duration: Style.animFast } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    Text { text: model.glyph; font.pixelSize: 13; color: Theme.text }
                    Text {
                        text: model.label
                        font.pixelSize: Style.fontSize
                        color: Theme.text
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: {
                            if (model.kind === 0) return sidebar.totalCount()
                            if (model.kind === 1) return sidebar.countsArr[7 + model.value]
                            return sidebar.countsArr[model.value]
                        }
                        font.pixelSize: Style.fontSizeSmall
                        color: Theme.subtleText
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sidebar.activateLibrary(model)
                }
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
            model: categoryModel
            delegate: Rectangle {
                required property var model
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                radius: Style.radiusSmall
                color: sidebar.categoryKey === model.key ? Theme.selection
                    : (catMouse.containsMouse ? Theme.hover : "transparent")
                Behavior on color { ColorAnimation { duration: Style.animFast } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    Text { text: model.glyph; font.pixelSize: 13; color: Theme.text }
                    Text {
                        text: model.label
                        font.pixelSize: Style.fontSize
                        color: Theme.text
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: sidebar.countsArr[model.value]
                        font.pixelSize: Style.fontSizeSmall
                        color: Theme.subtleText
                    }
                }

                MouseArea {
                    id: catMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sidebar.activateCategory(model)
                }
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
                    font.family: "Consolas, 'DejaVu Sans Mono', monospace"
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
