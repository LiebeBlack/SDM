import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

// Preferencias de la aplicación con navegación por pestañas (General, Red,
// Multimedia, Conexión, Acerca de).  Entrada/salida animadas.
Dialog {
    id: settingsDialog

    modal: true
    anchors.centerIn: parent
    width: 720
    height: 640
    padding: 0
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
        NumberAnimation { property: "scale"; from: 0.95; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.animFast; easing.type: Style.easeInOut }
    }

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
            Behavior on border.color { ColorAnimation { duration: Style.animFast } }
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
            Behavior on color { ColorAnimation { duration: Style.animFast } }
            Text {
                anchors.centerIn: parent
                visible: checkRoot.checked
                text: "✓"
                font.pixelSize: 12
                color: "#ffffff"
                scale: checkRoot.checked ? 1.0 : 0.5
                Behavior on scale { NumberAnimation { duration: Style.animFast; easing.type: Style.easeSpring } }
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

    // Selector de hilo reutilizable (1/6/8/16).
    component ThreadSelector: RowLayout {
        id: threadRow
        property int selected: bridge.defaultThreads
        spacing: 6
        Repeater {
            model: [1, 6, 8, 16]
            delegate: Rectangle {
                required property int modelData
                Layout.preferredWidth: 66
                Layout.preferredHeight: 28
                radius: Style.radiusSmall
                color: threadRow.selected === modelData ? Theme.accent
                    : (thrMouse.containsMouse ? Theme.hover : Theme.bg)
                border.color: Theme.border
                Behavior on color { ColorAnimation { duration: Style.animFast } }

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.pixelSize: Style.fontSize
                    font.weight: Font.Medium
                    color: threadRow.selected === modelData ? "#ffffff" : Theme.text
                }
                MouseArea {
                    id: thrMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        threadRow.selected = modelData
                        bridge.defaultThreads = modelData
                    }
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // ------------------------------------------------------------
        // Barra de pestañas
        // ------------------------------------------------------------
        TabBar {
            id: tabBar
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.bottomMargin: 0
            background: Rectangle {
                color: Theme.bg
                radius: Style.radiusSmall
                border.color: Theme.border
            }

            Repeater {
                model: ["General", "Red", "Multimedia", "Conexión", "Acerca de"]
                delegate: TabButton {
                    required property string modelData
                    required property int index
                    width: implicitWidth + 24
                    height: 36

                    contentItem: Text {
                        text: modelData
                        font.pixelSize: Style.fontSize
                        font.weight: tabBar.currentIndex === index ? Font.DemiBold : Font.Normal
                        color: tabBar.currentIndex === index ? Theme.accent : Theme.subtleText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Style.animFast } }
                    }
                    background: Rectangle {
                        color: parent.hovered ? Theme.hover : "transparent"
                        radius: Style.radiusSmall
                        Behavior on color { ColorAnimation { duration: Style.animFast } }
                    }
                }
            }
        }

        // Indicador de pestaña activa (linea acento animada).
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: -1
            height: 2
            color: Theme.accent
            radius: 1
        }

        // ------------------------------------------------------------
        // Contenido por pestaña
        // ------------------------------------------------------------
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            currentIndex: tabBar.currentIndex

            // === GENERAL ===
            ScrollView {
                clip: true
                ColumnLayout {
                    width: settingsDialog.width - 64
                    spacing: 6

                    SectionLabel { text: "DESCARGA" }

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
                        ThreadSelector { id: generalThreads }
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

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.topMargin: 10 }

                    SectionLabel { text: "APARIENCIA" }
                    SdmCheck {
                        label: "Tema oscuro (estilo Fluent)"
                        checked: bridge.darkMode
                        onToggled: bridge.darkMode = checked
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        text: "Las animaciones de la interfaz se optimizan automáticamente según el uso de recursos."
                        font.pixelSize: Style.fontSizeSmall
                        color: Theme.subtleText
                        wrapMode: Text.Wrap
                    }

                    Item { Layout.fillHeight: true; Layout.preferredHeight: 20 }
                }
            }

            // === RED ===
            ScrollView {
                clip: true
                ColumnLayout {
                    width: settingsDialog.width - 64
                    spacing: 6

                    SectionLabel { text: "CONEXIÓN" }

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

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 178
                        text: "El proxy se aplica a todas las descargas y sondas. Deja vacío para conexión directa."
                        font.pixelSize: Style.fontSizeSmall
                        color: Theme.subtleText
                        wrapMode: Text.Wrap
                    }

                    Item { Layout.fillHeight: true; Layout.preferredHeight: 20 }
                }
            }

            // === MULTIMEDIA ===
            ScrollView {
                clip: true
                ColumnLayout {
                    width: settingsDialog.width - 64
                    spacing: 6

                    SectionLabel { text: "HLS / DASH" }

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

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 178
                        text: "FFmpeg ensambla los segmentos HLS/DASH en el archivo final. yt-dlp es opcional para sitios con protección."
                        font.pixelSize: Style.fontSizeSmall
                        color: Theme.subtleText
                        wrapMode: Text.Wrap
                    }

                    Item { Layout.fillHeight: true; Layout.preferredHeight: 20 }
                }
            }

            // === CONEXIÓN CON EL NAVEGADOR ===
            ScrollView {
                clip: true
                ColumnLayout {
                    width: settingsDialog.width - 64
                    spacing: 6

                    SectionLabel { text: "PUENTE CON EL NAVEGADOR" }

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
                            font.family: Style.fontMono
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
                        Rectangle {
                            radius: Style.radiusPill
                            color: bridge.connectedClients > 0 ? Theme.successSoft : Theme.surfaceAlt
                            implicitWidth: bridgeText.implicitWidth + 24
                            implicitHeight: 26
                            Behavior on color { ColorAnimation { duration: Style.animNormal } }

                            Text {
                                id: bridgeText
                                anchors.centerIn: parent
                                text: (bridge.connectedClients > 0 ? "● " : "○ ")
                                      + bridge.connectedClients + " cliente(s) · puerto " + bridge.bridgePort
                                font.pixelSize: Style.fontSizeSmall
                                color: bridge.connectedClients > 0 ? Theme.success : Theme.subtleText
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Item { Layout.fillHeight: true; Layout.preferredHeight: 20 }
                }
            }

            // === ACERCA DE ===
            ScrollView {
                clip: true
                ColumnLayout {
                    width: settingsDialog.width - 64
                    spacing: 10

                    SectionLabel { text: "ACERCA DE" }

                    RowLayout {
                        spacing: 14
                        Text {
                            text: "⬇"
                            font.pixelSize: 38
                            color: Theme.accent
                        }
                        Column {
                            spacing: 3
                            Text {
                                text: "S-Download Manager"
                                font.pixelSize: Style.fontSizeHero
                                font.weight: Font.Bold
                                color: Theme.text
                            }
                            Text {
                                text: "v" + appVersion + " · motor nativo C++/Qt 6"
                                font.pixelSize: Style.fontSizeSmall
                                font.family: Style.fontMono
                                color: Theme.subtleText
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Acelerador de descargas multihilo con re-segmentación dinámica, reanudación byte exacta, "
                              + "captura HLS/DASH y protección anti-descarga de páginas web."
                        font.pixelSize: Style.fontSize
                        color: Theme.subtleText
                        wrapMode: Text.Wrap
                    }

                    SdmCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 74
                        elevation: Style.elevationLow
                        gradientBackground: true

                        RowLayout {
                            anchors.fill: parent
                            spacing: 24

                            Column {
                                spacing: 2
                                Text { text: "Memoria"; font.pixelSize: Style.fontSizeTiny; color: Theme.subtleText }
                                Text {
                                    text: bridge.memoryMb.toFixed(1) + " MB"
                                    font.pixelSize: Style.fontSizeTitle
                                    font.family: Style.fontMono
                                    color: Theme.text
                                }
                            }
                            Column {
                                spacing: 2
                                Text { text: "En curso"; font.pixelSize: Style.fontSizeTiny; color: Theme.subtleText }
                                Text {
                                    text: String(bridge.activeCount)
                                    font.pixelSize: Style.fontSizeTitle
                                    font.family: Style.fontMono
                                    color: Theme.success
                                }
                            }
                            Column {
                                spacing: 2
                                Text { text: "En cola"; font.pixelSize: Style.fontSizeTiny; color: Theme.subtleText }
                                Text {
                                    text: String(bridge.queuedCount)
                                    font.pixelSize: Style.fontSizeTitle
                                    font.family: Style.fontMono
                                    color: Theme.warning
                                }
                            }
                            Column {
                                spacing: 2
                                Text { text: "Velocidad"; font.pixelSize: Style.fontSizeTiny; color: Theme.subtleText }
                                Text {
                                    text: bridge.globalSpeedText
                                    font.pixelSize: Style.fontSizeTitle
                                    font.family: Style.fontMono
                                    color: Theme.success
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }

                    Item { Layout.fillHeight: true; Layout.preferredHeight: 20 }
                }
            }
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
