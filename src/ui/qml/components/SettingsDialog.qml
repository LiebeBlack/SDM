import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Preferencias de la aplicación (generales, red, multimedia, conexión).
Dialog {
    id: settingsDialog

    modal: true
    anchors.centerIn: parent
    width: 680
    height: 620
    padding: 0
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    title: "Ajustes"
    titleItem.color: Theme.text
    titleItem.font.pixelSize: Style.fontSizeTitle

    background: Rectangle {
        color: Theme.surface
        radius: Style.radius
        border.color: Theme.border
    }

    component SectionLabel: Text {
        font.pixelSize: Style.fontSizeHeader
        font.weight: Font.DemiBold
        color: Theme.accent
        Layout.topMargin: 14
        Layout.bottomMargin: 4
    }

    component FieldLabel: Text {
        Layout.preferredWidth: 170
        font.pixelSize: Style.fontSize
        color: Theme.subtleText
        elide: Text.ElideRight
    }

    component LineEdit: TextField {
        Layout.fillWidth: true
        color: Theme.text
        selectByMouse: true
        font.pixelSize: Style.fontSize
        placeholderTextColor: Theme.subtleText
        background: Rectangle {
            color: Theme.bg
            radius: Style.radiusSmall
            border.color: parent.activeFocus ? Theme.accent : Theme.border
        }
    }

    component SdmCheck: CheckBox {
        id: checkRoot
        property string label: ""
        text: label
        indicator: Rectangle {
            implicitWidth: 18
            implicitHeight: 18
            radius: 4
            color: checkRoot.checked ? Theme.accent : Theme.bg
            border.color: checkRoot.down ? Theme.accentDeep : Theme.border
            Text {
                anchors.centerIn: parent
                visible: checkRoot.checked
                text: "✓"
                font.pixelSize: 12
                color: "#ffffff"
            }
        }
        contentItem: Text {
            text: checkRoot.text
            font.pixelSize: Style.fontSize
            color: Theme.text
            leftPadding: checkRoot.indicator.width + 8
            verticalAlignment: Text.AlignVCenter
        }
    }

    contentItem: ScrollView {
        id: settingsScroll
        clip: true

        ColumnLayout {
            width: settingsDialog.width - 40
            x: 20
            spacing: 6

            // ----------------------------------------------------------
            // GENERAL
            // ----------------------------------------------------------
            SectionLabel { text: "GENERAL" }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Carpeta de descargas" }
                LineEdit {
                    id: dirField
                    text: bridge.downloadDir
                    onEditingFinished: bridge.downloadDir = text
                }
                SdmToolButton {
                    label: "Restaurar"
                    onClicked: dirField.text = bridge.suggestedFolder()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Hilos por defecto" }

                RowLayout {
                    spacing: 6
                    Repeater {
                        model: [1, 6, 8, 16]
                        delegate: Rectangle {
                            required property int modelData
                            Layout.preferredWidth: 66
                            Layout.preferredHeight: 28
                            radius: Style.radiusSmall
                            color: bridge.defaultThreads === modelData ? Theme.accent
                                : (defMouse.containsMouse ? Theme.hover : Theme.bg)
                            border.color: Theme.border

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: Style.fontSize
                                font.weight: Font.Medium
                                color: bridge.defaultThreads === modelData ? "#ffffff" : Theme.text
                            }
                            MouseArea {
                                id: defMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: bridge.defaultThreads = modelData
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Descargas simultáneas" }
                SpinBox {
                    id: concurrentSpin
                    from: 1
                    to: 20
                    value: bridge.maxConcurrent
                    onValueModified: bridge.maxConcurrent = value
                }
                Item { Layout.fillWidth: true }
            }

            SdmCheck {
                label: "Iniciar las tareas automáticamente"
                checked: bridge.autoStart
                onToggled: bridge.autoStart = checked
            }
            SdmCheck {
                label: "Reservar espacio en disco (preallocación)"
                checked: bridge.preallocate
                onToggled: bridge.preallocate = checked
            }
            SdmCheck {
                label: "Tema oscuro (estilo Fluent)"
                checked: bridge.darkMode
                onToggled: bridge.darkMode = checked
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.topMargin: 10 }

            // ----------------------------------------------------------
            // RED
            // ----------------------------------------------------------
            SectionLabel { text: "RED" }

            SdmCheck {
                label: "Rotar el User-Agent del navegador en cada tarea (anti-bloqueo)"
                checked: bridge.rotateUserAgent
                onToggled: bridge.rotateUserAgent = checked
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Proxy (opcional)" }
                LineEdit {
                    id: proxyField
                    text: bridge.proxy
                    placeholderText: "http://host:puerto  ·  vacío = conexión directa"
                    onEditingFinished: bridge.proxy = text
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.topMargin: 10 }

            // ----------------------------------------------------------
            // MULTIMEDIA
            // ----------------------------------------------------------
            SectionLabel { text: "MULTIMEDIA (HLS / DASH)" }

            SdmCheck {
                label: "Usar yt-dlp para sitios complejos (si está instalado)"
                checked: bridge.useYtDlp
                onToggled: bridge.useYtDlp = checked
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Ruta de FFmpeg" }
                LineEdit {
                    id: ffmpegField
                    text: bridge.ffmpegPath
                    onEditingFinished: bridge.ffmpegPath = text
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Ruta de yt-dlp" }
                LineEdit {
                    id: ytdlpField
                    text: bridge.ytDlpPath
                    onEditingFinished: bridge.ytDlpPath = text
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.topMargin: 10 }

            // ----------------------------------------------------------
            // CONEXIÓN CON EL NAVEGADOR
            // ----------------------------------------------------------
            SectionLabel { text: "CONEXIÓN CON EL NAVEGADOR" }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Clave de conexión" }
                TextField {
                    id: keyField
                    Layout.fillWidth: true
                    text: bridge.connectionKey
                    readOnly: true
                    selectByMouse: true
                    color: Theme.text
                    font.family: "Consolas, 'DejaVu Sans Mono', monospace"
                    font.pixelSize: Style.fontSize
                    background: Rectangle { color: Theme.bg; radius: Style.radiusSmall; border.color: Theme.border }
                }
                SdmToolButton {
                    label: "Copiar"
                    onClicked: bridge.copyText(bridge.connectionKey)
                }
            }

            Text {
                Layout.leftMargin: 178
                text: "Pégala en el popup de la extensión (Ajustes de SDM) para vincular el navegador."
                font.pixelSize: Style.fontSizeSmall
                color: Theme.subtleText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "ID de la extensión" }
                LineEdit {
                    id: extensionField
                    text: bridge.extensionId
                    placeholderText: "p. ej. abcdefghijklmnopqrstuvwxyzabcdef"
                    onEditingFinished: bridge.extensionId = text
                }
                SdmToolButton {
                    label: "Aplicar"
                    onClicked: bridge.extensionId = extensionField.text
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Manifiesto nativo" }
                TextField {
                    Layout.fillWidth: true
                    text: bridge.nativeManifestPath
                    readOnly: true
                    selectByMouse: true
                    color: Theme.subtleText
                    font.pixelSize: Style.fontSizeSmall
                    background: Rectangle { color: Theme.bg; radius: Style.radiusSmall; border.color: Theme.border }
                }
                SdmToolButton {
                    label: "Regenerar"
                    onClicked: bridge.writeNativeHostManifest()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacing
                FieldLabel { text: "Estado del puente" }
                Text {
                    text: (bridge.connectedClients > 0 ? "🟢 " : "⚪ ")
                          + bridge.connectedClients + " cliente(s) conectado(s) · puerto " + bridge.bridgePort
                    font.pixelSize: Style.fontSize
                    color: Theme.text
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.topMargin: 10 }

            // ----------------------------------------------------------
            // ACERCA DE
            // ----------------------------------------------------------
            SectionLabel { text: "ACERCA DE" }
            Text {
                Layout.fillWidth: true
                text: "S-Download Manager (SDM) v" + appVersion
                      + " — motor nativo C++/Qt 6, multihilo, re-segmentación dinámica y captura HLS/DASH."
                font.pixelSize: Style.fontSizeSmall
                color: Theme.subtleText
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: "Memoria en uso: " + bridge.memoryMb.toFixed(1) + " MB  ·  Descargas en curso: "
                      + bridge.activeCount + "  ·  En cola: " + bridge.queuedCount
                font.pixelSize: Style.fontSizeSmall
                font.family: "Consolas, 'DejaVu Sans Mono', monospace"
                color: Theme.subtleText
            }

            Item { Layout.fillHeight: true; Layout.preferredHeight: 20 }
        }
    }

    footer: RowLayout {
        Layout.margins: Style.padding
        spacing: Style.spacing

        Item { Layout.fillWidth: true }
        SdmToolButton {
            label: "Cerrar"
            accent: true
            onClicked: settingsDialog.close()
        }
    }
}
