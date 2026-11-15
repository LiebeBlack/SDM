import QtQuick
import "../theme"

// Gráfico de velocidad global en vivo (sparkline tipo monitor de ancho de
// banda).  Muestrea bridge.globalSpeed cada segundo y dibuja con Canvas:
// cero dependencias externas y coste de render mínimo (solo pinta al
// muestrear, no por frame).
Item {
    id: graph

    property int maxSamples: 90          // 90 s de historia
    property var samples: []
    property color lineColor: Theme.success

    // Muestreo a 1 Hz (barra de estado: suficiente y barato).
    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            var next = graph.samples.slice()
            next.push(Math.max(0, bridge.globalSpeed))
            while (next.length > graph.maxSamples) next.shift()
            graph.samples = next
            canvas.requestPaint()
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true
        opacity: 0.9

        // Leyenda minima de escala.
        Text {
            anchors.right: parent.right
            anchors.top: parent.top
            font.pixelSize: Style.fontSizeTiny
            font.family: Style.fontMono
            color: Theme.subtleText
            text: {
                var max = 0
                for (var i = 0; i < graph.samples.length; i++) {
                    if (graph.samples[i] > max) max = graph.samples[i]
                }
                return max > 0 ? "max" : ""
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var data = graph.samples
            if (data.length < 2) return

            var max = 1
            for (var i = 0; i < data.length; i++) {
                if (data[i] > max) max = data[i]
            }

            var step = width / (graph.maxSamples - 1)
            var x0 = width - (data.length - 1) * step

            function yOf(v) {
                return height - (v / max) * (height - 3) - 1.5
            }

            // Area rellena bajo la curva.
            ctx.beginPath()
            ctx.moveTo(x0, height)
            for (var j = 0; j < data.length; j++) {
                ctx.lineTo(x0 + j * step, yOf(data[j]))
            }
            ctx.lineTo(x0 + (data.length - 1) * step, height)
            ctx.closePath()
            var grad = ctx.createLinearGradient(0, 0, 0, height)
            grad.addColorStop(0, Qt.rgba(graph.lineColor.r, graph.lineColor.g, graph.lineColor.b, 0.35))
            grad.addColorStop(1, Qt.rgba(graph.lineColor.r, graph.lineColor.g, graph.lineColor.b, 0.02))
            ctx.fillStyle = grad
            ctx.fill()

            // Linea superior.
            ctx.beginPath()
            ctx.moveTo(x0, yOf(data[0]))
            for (var k = 1; k < data.length; k++) {
                ctx.lineTo(x0 + k * step, yOf(data[k]))
            }
            ctx.strokeStyle = graph.lineColor
            ctx.lineWidth = 1.4
            ctx.stroke()
        }
    }

    // Eje base.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }
}
