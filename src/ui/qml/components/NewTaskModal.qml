import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platforms
import "../theme"

// Dialogo flotante de captura de nueva tarea (spec 4.2.4).
Dialog {
    id: modal

    // Datos capturados (extensión / portapapeles / manifiesto)
    property string source: "manual"
    property string referer: ""
    property string cookies: ""
    property string fileName: ""
    property string pageUrl: ""
    property bool streamMode: false
    property var scheduledTaskId: 0

    function urlToLocal(value) {
        var s = decodeURIComponent(String(value))
        if (s.indexOf("file://") === 0) {
            s = s.slice(7)
        }
        if (/^\/[A-Za-z]:/.test(s)) {
            s = s.slice(1)
        }
        return s
    }

    function prepare(options) {
        options = options || {}
        source = options.source || "manual"
        referer = options.referer || ""
        cookies = options.cookies || ""
        fileName = options.fileName || ""
        pageUrl = options.pageUrl || ""
        var initialUrl = options.url || ""
        if (!initialUrl) {
            var clip = bridge.clipboardText()
            if (clip && (clip.indexOf("http") === 0 || clip.indexOf("ftp") === 0)) {
                initialUrl = clip
            }
        }
        urlField.text = initialUrl
        folderField.text = options.dir || bridge.suggestedFolder()
        userField.text = ""
        passField.text = ""
        threadsRow.selectedThreads = bridge.defaultThreads
        holdCheck.checked = false
        scheduleCheck.checked = false
        errorLabel.visible = false
        streamMode = false
        open()
        urlField.forceActiveFocus()
    }

    function looksLikeStream() {
        var u = urlField.text.toLowerCase()
        return u.indexOf(".m3u8") >= 0 || u.indexOf(".mpd") >= 0 || u.indexOf("format=m3u8") >= 0
    }

    function submit() {
        var url = urlField.text.trim()
        if (url.indexOf("http://") !== 0 && url.indexOf("https://") !== 0 && url.indexOf("ftp://") !== 0) {
            errorLabel.text = "Introduce una URL válida (http, https o ftp)."
            errorLabel.visible = true
            return
        }
        var options = {
            url: url,
            dir: folderField.text.trim(),
            threads: threadsRow.selectedThreads,
            referer: referer,
            cookies: cookies,
            fileName: fileName,
            pageUrl: pageUrl,
            user: userField.text,
            pass: passField.text,
            autoStart: !holdCheck.checked,
            source: source
        }
        scheduledTaskId = bridge.addTask(options)

        if (scheduleCheck.checked && scheduledTaskId > 0) {
            // Programacion de inicio: se crea en cola y se arranca a la hora.
            var now = new Date()
            var target = new Date()
            target.setHours(timeEdit.hours(), timeEdit.minutes(), 0, 0)
            if (target <= now) {
                target.setDate(target.getDate() + 1)
            }
            var delta = target.getTime() - now.getTime()
            scheduleTimer.interval = Math.min(delta, 2147483000)
            scheduleTimer.start()
        }
        modal.close()
    }

    Timer {
        id: scheduleTimer
        repeat: false
        onTriggered: bridge.resumeTask(modal.scheduledTaskId)
    }

    modal: true
    anchors.centerIn: parent
    width: 640
    padding: Style.paddingLarge
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Entrada/salida animada (fade + escala) coherente con el resto de diálogos.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
        NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Style.animNormal; easing.type: Style.easeOut }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.animFast; easing.type: Style.easeInOut }
    }

    title: streamMode ? "Nueva descarga multimedia" : "Nueva descarga"
    // Cabecera propia con el título estilizado (Dialog no expone titleItem).
    header: Item {
        implicitHeight: modalTitle.implicitHeight + 2 * Style.padding
        Text {
            id: modalTitle
            anchors.left: parent.left
            anchors.leftMargin: Style.paddingLarge
            anchors.verticalCenter: parent.verticalCenter
            text: modal.title
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

    Platforms.FolderDialog {
        id: folderDialog
        title: "Selecciona la carpeta de destino"
        onAccepted: folderField.text = modal.urlToLocal(folderDialog.folder)
    }

    contentItem: ColumnLayout {
        spacing: Style.padding

        // --- URL ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacing

            Text { text: "URL"; Layout.preferredWidth: 84; font.pixelSize: Style.fontSize; color: Theme.subtleText }
            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "https://…"
                placeholderTextColor: Theme.subtleText
                color: Theme.text
                selectByMouse: true
                font.pixelSize: Style.fontSize
                background: Rectangle {
                    color: Theme.bg
                    radius: Style.radiusSmall
                    border.color: urlField.activeFocus ? Theme.accent : Theme.border
                }
                onTextChanged: hintLabel.visible = modal.looksLikeStream()
            }
            SdmToolButton {
                label: "Pegar"
                onClicked: {
                    var clip = bridge.clipboardText()
                    if (clip && clip.length > 0) {
                        urlField.text = clip
                    }
                }
            }
        }

        Text {
            id: hintLabel
            visible: false
            Layout.leftMargin: 92
            text: "🎬 Se detectó un manifiesto multimedia (HLS/DASH): SDM mostrará las calidades disponibles."
            font.pixelSize: Style.fontSizeSmall
            color: Theme.info
        }

        // --- Carpeta destino ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacing

            Text { text: "Guardar en"; Layout.preferredWidth: 84; font.pixelSize: Style.fontSize; color: Theme.subtleText }
            TextField {
                id: folderField
                Layout.fillWidth: true
                placeholderTextColor: Theme.subtleText
                color: Theme.text
                selectByMouse: true
                font.pixelSize: Style.fontSize
                background: Rectangle {
                    color: Theme.bg
                    radius: Style.radiusSmall
                    border.color: Theme.border
                }
            }
            SdmToolButton {
                label: "Examinar…"
                onClicked: folderDialog.open()
            }
        }

        // --- Hilos (spec 5.3) ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacing

            Text { text: "Hilos"; Layout.preferredWidth: 84; font.pixelSize: Style.fontSize; color: Theme.subtleText }

            RowLayout {
                id: threadsRow
                spacing: 6
                property int selectedThreads: bridge.defaultThreads

                Repeater {
                    model: [1, 6, 8, 16]
                    delegate: Rectangle {
                        required property int modelData
                        Layout.preferredWidth: 74
                        Layout.preferredHeight: 30
                        radius: Style.radiusSmall
                        color: threadsRow.selectedThreads === modelData ? Theme.accent
                            : (thrMouse.containsMouse ? Theme.hover : Theme.bg)
                        border.color: Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: modelData + (modelData === 1 ? " hilo" : " hilos")
                            font.pixelSize: Style.fontSize
                            font.weight: Font.Medium
                            color: threadsRow.selectedThreads === modelData ? "#ffffff" : Theme.text
                        }
                        MouseArea {
                            id: thrMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: threadsRow.selectedThreads = modelData
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Categoría: se detecta por extensión"
                    font.pixelSize: Style.fontSizeSmall
                    color: Theme.subtleText
                }
            }
        }

        // --- Autenticación HTTP ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacing

            Text { text: "Autenticación"; Layout.preferredWidth: 84; font.pixelSize: Style.fontSize; color: Theme.subtleText }
            TextField {
                id: userField
                Layout.preferredWidth: 180
                placeholderText: "Usuario (opcional)"
                placeholderTextColor: Theme.subtleText
                color: Theme.text
                font.pixelSize: Style.fontSize
                background: Rectangle { color: Theme.bg; radius: Style.radiusSmall; border.color: Theme.border }
            }
            TextField {
                id: passField
                Layout.preferredWidth: 180
                placeholderText: "Contraseña"
                placeholderTextColor: Theme.subtleText
                echoMode: TextInput.Password
                color: Theme.text
                font.pixelSize: Style.fontSize
                background: Rectangle { color: Theme.bg; radius: Style.radiusSmall; border.color: Theme.border }
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        // --- Opciones ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 24

            CheckBox {
                id: holdCheck
                text: "No iniciar aún (dejar en cola)"
                contentItem: Item {
                    implicitWidth: holdLabel.implicitWidth + holdCheck.indicator.width + 8
                    implicitHeight: holdLabel.implicitHeight
                    Text {
                        id: holdLabel
                        anchors.left: parent.left
                        anchors.leftMargin: holdCheck.indicator.width + 8
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: holdCheck.text
                        font.pixelSize: Style.fontSize
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                }
            }

            CheckBox {
                id: scheduleCheck
                text: "Programar inicio:"
                contentItem: Item {
                    implicitWidth: scheduleLabel.implicitWidth + scheduleCheck.indicator.width + 8
                    implicitHeight: scheduleLabel.implicitHeight
                    Text {
                        id: scheduleLabel
                        anchors.left: parent.left
                        anchors.leftMargin: scheduleCheck.indicator.width + 8
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: scheduleCheck.text
                        font.pixelSize: Style.fontSize
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                }
            }
            // Selector de hora programada (HH:MM) con dos campos numéricos.
            RowLayout {
                id: timeEdit
                spacing: 4
                enabled: scheduleCheck.checked

                function hours() { return hourBox.value }
                function minutes() { return minuteBox.value }

                component TimeBox: SpinBox {
                    id: box
                    editable: true
                    font.pixelSize: Style.fontSize
                    font.family: Style.fontMono
                    leftPadding: 22
                    rightPadding: 22
                    background: Rectangle {
                        color: Theme.bg
                        radius: Style.radiusSmall
                        border.color: box.activeFocus ? Theme.accent : Theme.border
                        Behavior on border.color { ColorAnimation { duration: Style.animFast } }
                    }
                    contentItem: TextInput {
                        text: box.textFromValue(box.value, box.locale)
                        font: box.font
                        color: Theme.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !box.editable
                        validator: box.validator
                        inputMethodHints: box.inputMethodHints
                        selectByMouse: true
                        onTextEdited: box.value = box.valueFromText(text, box.locale)
                    }
                    up.indicator: Text {
                        x: box.width - width - 4
                        y: 2
                        text: "▲"
                        font.pixelSize: 7
                        color: box.up.hovered ? Theme.accent : Theme.subtleText
                    }
                    down.indicator: Text {
                        x: box.width - width - 4
                        y: box.height - height - 2
                        text: "▼"
                        font.pixelSize: 7
                        color: box.down.hovered ? Theme.accent : Theme.subtleText
                    }
                }

                TimeBox {
                    id: hourBox
                    from: 0
                    to: 23
                    Layout.preferredWidth: 64
                }
                Text {
                    text: ":"
                    font.pixelSize: Style.fontSizeTitle
                    font.family: Style.fontMono
                    color: Theme.text
                }
                TimeBox {
                    id: minuteBox
                    from: 0
                    to: 59
                    Layout.preferredWidth: 64
                }
            }
            Item { Layout.fillWidth: true }
        }

        Text {
            id: errorLabel
            visible: false
            text: ""
            font.pixelSize: Style.fontSizeSmall
            color: Theme.danger
        }

        // --- Acciones ---
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: Style.spacing

            Text {
                text: "Se iniciará de forma automática" + (holdCheck.checked ? " (en cola)" : "")
                font.pixelSize: Style.fontSizeSmall
                color: Theme.subtleText
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            SdmToolButton {
                label: "Cancelar"
                onClicked: modal.close()
            }
            SdmToolButton {
                label: "Iniciar descarga"
                accent: true
                onClicked: modal.submit()
            }
        }
    }
}
