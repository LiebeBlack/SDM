# BLUEPRINT DE ARQUITECTURA, PLANIFICACIÓN TÉCNICA Y ESPECIFICACIONES DE DISEÑO

## PROYECTO: S-DOWNLOAD MANAGER (SDM)
### *El Sucesor Definitivo de Internet Download Manager (IDM)*

---

## ÍNDICE DE CONTENIDOS

1. [Visión General del Sistema y Filosofía de Diseño](#1-visión-general-del-sistema-y-filosofía-de-diseño)
2. [Análisis Comparativo y Ventajas Competitivas](#2-análisis-comparativo-y-ventajas-competitivas)
3. [Arquitectura Tecnológica del Sistema Core](#3-arquitectura-tecnológica-del-sistema-core)
4. [Diseño y Protocolos de la Interfaz de Usuario (UI/UX Nativa)](#4-diseño-y-protocolos-de-la-interfaz-de-usuario-uiux-nativa)
5. [Motor de Descargas Multihilo y Algoritmo de Segmentación](#5-motor-de-descargas-multihilo-y-algoritmo-de-segmentación)
6. [Módulo de Suplantación de Navegador y Anti-Bloqueos (Fingerprinting)](#6-módulo-de-suplantación-de-navegador-y-anti-bloqueos-fingerprinting)
7. [Arquitectura de Integración con Navegadores (Manifest V3)](#7-arquitectura-de-integración-con-navegadores-manifest-v3)
8. [Servidor de Comunicación Local (IPC, Sockets & Native Messaging)](#8-servidor-de-comunicación-local-ipc-sockets--native-messaging)
9. [Módulo Multimedia y Captura de Streaming (HLS & DASH)](#9-módulo-multimedia-y-captura-de-streaming-hls--dash)
10. [Manejo de Errores, Auto-Refresco de Enlaces y Reanudación Inteligente](#10-manejo-de-errores-auto-refresco-de-enlaces-y-reanudación-inteligente)
11. [Estructura del Repositorio y Configuración de Proyectos](#11-estructura-del-repositorio-y-configuración-de-proyectos)
12. [Estrategia de CI/CD con GitHub Actions para Windows 10/11](#12-estrategia-de-cicd-con-github-actions-para-windows-1011)
13. [Hoja de Ruta Detallada de Desarrollo (Roadmap por Fases)](#13-hoja-de-ruta-detallada-de-desarrollo-roadmap-por-fases)
14. [Consideraciones de Seguridad, Certificación y Mantenimiento](#14-consideraciones-de-seguridad-certificación-y-mantenimiento)

---

## 1. VISIÓN GENERAL DEL SISTEMA Y FILOSOFÍA DE DISEÑO

### 1.1 Declaración de Misión
**S-Download Manager (SDM)** nace con el objetivo de erradicar los cuellos de botella y las deficiencias de usabilidad de los gestores de descargas tradicionales como Internet Download Manager (IDM). A través de un enfoque obsesivo en la optimización de código nativo, la coherencia estética con los sistemas operativos modernos y la resiliencia en la captura de tráfico, SDM se posiciona como el estándar definitivo de velocidad, ligereza y confiabilidad.

### 1.2 Principios Rectores

*   **Rendimiento Nativo Absoluto (Cero Overhead Web):** Queda estrictamente prohibida la inclusión de motores de renderizado web como Chromium, Electron, WebView2 o Tauri para la interfaz gráfica. Todo el software se compila directamente en lenguaje nativo C++20 con enlaces estáticos/dinámicos optimizados, garantizando un consumo en reposo (*idle*) menor a **20 MB de memoria RAM** y un tiempo de arranque inicial inferior a **150 milisegundos**.
*   **Diseño Fluido y Moderno (Fluent Design System):** Integración estética perfecta con Windows 10 y Windows 11. Soporte completo para efectos Mica/Acrylic, modo oscuro nativo respetando el tema del sistema, escalado DPI dinámico y animaciones por hardware a 60 FPS mediante la API gráfica nativa del sistema.
*   **Invisible e Ininterrumpido:** La integración con el navegador no debe ralentizar la navegación web ni introducir latencias en las peticiones. La captura de enlaces ocurre en un plano transparente, procesando tokens de descarga y enviándolos al motor de escritorio sin bloquear el hilo principal de renderizado del navegador.
*   **Control Fino por el Usuario:** El software prioriza la transparencia y el control total. Configuración exacta de particiones de red (1, 6, 8 o 16 hilos), manipulación de cabeceras HTTP, rotación de agentes de usuario (*User-Agents*) y gestión transparente de recursos en el disco rígido o unidad SSD.

---

## 2. ANÁLISIS COMPARATIVO Y VENTAJAS COMPETITIVAS

| Característica / Parámetro | IDM Clásico | Gestores Basados en Electron / Web | **S-Download Manager (SDM)** |
| :--- | :--- | :--- | :--- |
| **Tecnología de Interfaz** | Win32 MFC (Diseño tipo Windows 11) | HTML / CSS / JS (Chromium) | **Qt 6 QML / C++ (Aceleración GPU Nactiva)** |
| **Consumo de Memoria RAM** | ~120 MB - 250 MB | ~250 MB - 400 MB | **< 20 MB (Idle) / < 45 MB (Descargando)** |
| **Respuesta de Arranque** | Rápida (< 300 ms) | Lenta (> 1.5 - 3 segundos) | **Instantánea (< 120 ms)** |
| **Algoritmo de Hilos** | Segmentación estática/dinámica simple | Hilos de JS o bindings restringidos | **Segmentación dinámica adaptativa re-balanceada** |
| **Opciones de Hilos** | Configurables (Varía) | Limitados | **1, 6, 8 o 16 hilos en tiempo real** |
| **Suplantación de Cliente** | Cabeceras rígidas estáticas | Limitado por el entorno Node/Browser | **Emulación TLS Fingerprint + User-Agent dinámico** |
| **Soporte de Plataformas** | Únicamente Windows | Multiplataforma (Pesado) | **Windows 10/11 (Optimizado) / Linux / macOS** |
| **Estética y Tema** | Obsoleta, botones bitmap | Moderna pero ineficiente | **Fluent Design Nativo / Modo Oscuro Automático** |

---

## 3. ARQUITECTURA TECNOLÓGICA DEL SISTEMA CORE

La arquitectura general de SDM adopta un patrón **Model-View-ViewModel (MVVM)** combinado con un motor de eventos asíncronos en C++ para desacoplar totalmente el rendimiento de la red de la interfaz gráfica.

```
+-----------------------------------------------------------------------+
|                         NAVEGADOR WEB (Chrome/Firefox/Edge)           |
|  +-----------------------------------------------------------------+  |
|  | Extensión Manifest V3 (Content Script + Background Worker)      |  |
|  +-----------------------------------------------------------------+  |
+-----------------------------------|-----------------------------------+
                                    | Native Messaging / Local WebSocket
                                    v
+-----------------------------------------------------------------------+
|                      S-DOWNLOAD MANAGER CORE (C++26)                  |
|                                                                       |
|  +-----------------------------------------------------------------+  |
|  | Bridge Layer: NativeHost & WebSocket Server (Qt Network)        |  |
|  +-----------------------------------------------------------------+  |
|                                   |                                   |
|  +--------------------------------v--------------------------------+  |
|  | Download Engine Coordinator (Manager de Tareas y Colas)         |  |
|  +-----------------------------------------------------------------+  |
|         |                         |                        |          |
|         v                         v                        v          |
|  +--------------+        +------------------+     +----------------+  |
|  | User-Agent   |        | Segment Worker   |     | Yt-Dlp / Media |  |
|  | & TLS Mask   |        | (libcurl Engine) |     | Stream Module  |  |
|  +--------------+        +------------------+     +----------------+  |
|                                   |                                   |
|                                   v                                   |
|  +-----------------------------------------------------------------+  |
|  | I/O Engine: File Allocator & Asynchronous Disk Writer           |  |
|  +-----------------------------------------------------------------+  |
|                                   |                                   |
|                                   v                                   |
|  +-----------------------------------------------------------------+  |
|  | Interfaz Gráfica Nativa: Qt 6 QML / Scene Graph (Render por GPU)|  |
|  +-----------------------------------------------------------------+  |
+-----------------------------------------------------------------------+
```

### 3.1 Pila de Tecnologías Seleccionada
*   **Lenguaje de Programación Core:** C++26 (Compiladores: MSVC para Windows, GCC 16.2 / Clang + 23.1.4).
*   **Framework UI:** Qt 6.12 (Qt Quick / QML con renderizador acelerado por RHI - Rendering Hardware Interface).
*   **Librería de Transferencia de Red:** `libcurl` (Versión 9.x+) compilada con soporte `OpenSSL 4.x` y `nghttp3` para soporte HTTP/2 y HTTP/3 UPDUM etc (QUIC).
*   **Sistema de Construcción:** CMake 3.25+.
*   **Librerías Complementarias:**
    *   `nlohmann/json`: Para parseo ultrarrápido de mensajes JSON entre el navegador y la aplicación.
    *   `fmt`: Para formateo de cadenas de texto de alto rendimiento.
    *   `sqlite3`: Para el almacenamiento persistente del historial de descargas, colas y metadatos.

---

## 4. DISEÑO Y PROTOCOLOS DE LA INTERFAZ DE USUARIO (UI/UX NATIVA)

### 4.1 Filosofía de Diseño UI
La interfaz de SDM está proyectada para ser limpia, funcional y altamente reactiva. Se evitan menús sobrecargados y elementos innecesarios, enfocando la atención del usuario en el rendimiento de la descarga y el estado de sus archivos.

```
+---------------------------------------------------------------------------------------+
|  SDM - S-Download Manager Pro                                               [-][□][X] |
+---------------------------------------------------------------------------------------+
| [ + Nueva ] [ ▶ Reanudar ] [ ⏸ Pausar ] [ ⏹ Detener ] [ ⚙ Ajustes ] | 🔍 [Buscar...]  |
+-------------------+-------------------------------------------------------------------+
| CATEGORÍAS        | ESTADO  | NOMBRE DEL ARCHIVO      | TAMAÑO  | PROGRESO | VELOCIDAD  |
| 📥 Todas (12)     | ⏬ Desc | ISO_Windows11_23H2.iso  | 5.4 GB  | [68%]    | 45.2 MB/s  |
| 🎥 Videos (4)     | ⏸ Paus | Video_4K_Nature_Doc.mp4 | 1.2 GB  | [32%]    | --         |
| 🎵 Música (2)     | ✔ Comp | Setup_Development.exe   | 450 MB  | [100%]   | Completado |
| 📦 Comprimidos(3) | ⏬ Desc | Archive_Project_v2.zip  | 2.1 GB  | [12%]    | 12.8 MB/s  |
| 📄 Documentos (1) |         |                                                         |
| ⚙️ En Cola (2)     |                                                                         |

Ventana de Desacarga
+-------------------+-------------------------------------------------------------------+
| INFORMACIÓN DE SEGMENTOS (HILOS: 16 ACTIVO)                                           |
| [████████████][████████████][██████████░░][████████░░░░][██████░░░░░░][████░░░░░░░░]  |
| Velocidad Global: 58.0 MB/s | Restante: 01m 12s | Disco: 120 MB/s Escritura | RAM: 18 MB  |
+---------------------------------------------------------------------------------------+
```

### 4.2 Especificaciones de Componentes UX/UI

1.  **Panel Lateral de Categorías:**
    *   Clasificación automática mediante reglas asociadas a la extensión del archivo (MIME Types).
    *   Filtros en tiempo real: En curso, Pausadas, Completadas, Fallidas, En Cola.
2.  **Tabla Principal de Descargas (Custom QML TableView):**
    *   Renderizado virtualizado: Permite manipular listas de más de 50,000 descargas sin caídas de framerate ni sobrecarga de memoria.
    *   Columnas configurables: Nombre, Estado, Tamaño Total, Progreso (Barra visual interactiva), Velocidad, Tiempo Restante (ETA), Fecha de Adición, URL de Origen.
3.  **Monitor de Hilos en Tiempo Real (Segment Visualizer):**
    *   Representación gráfica tipo bloque que muestra visualmente los rangos del archivo asignados a cada hilo (1, 6, 8 o 16 hilos).
    *   Indicadores de color por estado de hilo: Verde (Descargando), Amarillo (Re-segmentando), Azul (Completado y unificado), Rojo (Error / Reintentando conexión).
4.  **Diálogo Flotante de Captura de Enlace (New Task Modal):**
    *   Activado al capturar una URL del portapapeles o mediante extensión.
    *   Opciones inmediatas: Selección de directorio de destino, categoría, número de hilos específico para esta tarea (Anular global: 1, 6, 8 o 16), autenticación HTTP (Usuario/Contraseña) y programación de inicio.

---

## 5. MOTOR DE DESCARGAS MULTIHILO Y ALGORITMO DE SEGMENTACIÓN

### 5.1 Principio de Funcionamiento por Peticiones de Rango HTTP (`Range: bytes`)
El motor de aceleración de SDM funciona enviando múltiples solicitudes HTTP en paralelo al servidor remoto, solicitando partes específicas del mismo archivo utilizando la cabecera `Range`.

```
Archivo Completo en Servidor (100 MB)
|-------------------------------------------------------------------------|
  
  Hilo 1: Range: bytes=0-26214399          (0 MB - 25 MB)   --> [Descargando]
  Hilo 2: Range: bytes=26214400-52428799     (25 MB - 50 MB)  --> [Descargando]
  Hilo 3: Range: bytes=52428800-78643199     (50 MB - 75 MB)  --> [Descargando]
  Hilo 4: Range: bytes=78643200-104857599    (75 MB - 100 MB) --> [Descargando]
```

### 5.2 Algoritmo de Segmentación Dinámica Adaptativa
A diferencia de los gestores simples que dividen el archivo una sola vez al inicio, SDM implementa un **Balanceador de Carga por Re-segmentación Continua**:

1.  **Evaluación de Soporte de Servidor:** Se realiza una petición previa `HEAD` o `GET` parcial para verificar la existencia de la cabecera `Accept-Ranges: bytes` y obtener el tamaño total (`Content-Length`).
2.  **Particionado Inicial:** Se divide el tamaño total entre el número de hilos configurado por el usuario ($N \in \{1, 6, 8, 16\}$).
3.  **Monitoreo de Rendimiento de Hilos:** Si un hilo $A$ se encuentra rezagado (debido a un estrangulamiento del servidor en esa conexión) y el hilo $B$ completa su segmento asignado, el hilo $B$ no se apaga.
4.  **División Dinámica:** El motor toma el fragmento no descargado restante del hilo $A$, lo divide por la mitad y le asigna la segunda mitad al hilo $B$, lanzando inmediatamente una nueva petición `Range`.
5.  **Escritura Asíncrona sin Punteros Solapados:** Cada hilo escribe en el archivo de disco utilizando desplazamientos de posición (*offsets*) directos mediante la API del sistema de archivos, eliminando la necesidad de fusionar archivos temporales al finalizar.

### 5.3 Opciones de Configuración de Hilos

```
         +-------------------------------------------------------+
         | Opciones de Hilos Configurable en la Interfaz         |
         +-------------------------------------------------------+
         |  ( ) 1 Hilo   - Modo Compatibilidad / Servidores Rígidos |
         |  ( ) 6 Hilos  - Conexión Estándar / Uso Moderado      |
         |  ( ) 8 Hilos  - Modo Equilibrado Óptimo (Default)     |
         |  (●) 16 Hilos - Máxima Aceleración / Banda Ancha Alta  |
         +-------------------------------------------------------+
```

---

## 6. MÓDULO DE SUPLANTACIÓN DE NAVEGADOR Y ANTI-BLOQUEOS (FINGERPRINTING)

### 6.1 El Problema de los Bloqueos en Gestores de Descarga
Muchos servidores web modernos (Cloudflare, Akamai, Imperva) bloquean las descargas solicitadas por programas automatizados mediante el análisis de dos elementos clave:
1.  **Cabecera `User-Agent` Inconsistente:** Identificación explícita de librerías como `curl/7.x`, `Python-urllib` o firmas desactualizadas.
2.  **Huella TLS (TLS Fingerprint / JA3):** La secuencia de cifrados (*ciphers*), extensiones TLS y versiones de handshake que utiliza la librería de red difiere de la huella que envía un navegador real como Google Chrome o Microsoft Edge.

### 6.2 Estrategia de Emulación Total en SDM

```
+-----------------------------------------------------------------------+
| MÓDULO DE EMULACIÓN DE NAVEGADOR (BrowserEmulationEngine)            |
+-----------------------------------------------------------------------+
|  1. Inyección Dinámica de User-Agent (Versiones Actualizadas Chrome)   |
|  2. Replicación de Cabeceras HTTP/2 (Sec-CH-UA, Accept-Language, etc) |
|  3. Configuración de TLS Ciphersuite Matching (Simulación OpenSSL)     |
|  4. Gestión Transparente de Cookies y Sessions Tokens (Navegador)     |
+-----------------------------------------------------------------------+
```

#### Conjunto de Cabeceras Inyectadas por Defecto (Simulación Chrome en Win11):
*   `User-Agent`: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36`
*   `Accept`: `text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8`
*   `Accept-Language`: `es-ES,es;q=0.9,en;q=0.8`
*   `Sec-CH-UA`: `"Chromium";v="122", "Not(A:Brand";v="24", "Google Chrome";v="122"`
*   `Sec-CH-UA-Mobile`: `?0`
*   `Sec-CH-UA-Platform`: `"Windows"`
*   `Sec-Fetch-Dest`: `document`
*   `Sec-Fetch-Mode`: `navigate`
*   `Sec-Fetch-Site`: `none`
*   `Sec-Fetch-User`: `?1`

---

## 7. ARQUITECTURA DE INTEGRACIÓN CON NAVEGADORES (MANIFEST V3)

La extensión web para Google Chrome, Microsoft Edge, Brave y Mozilla Firefox está construida respetando la especificación **Manifest V3**, utilizando un *Service Worker* de fondo para el monitoreo no intrusivo del tráfico de red.

### 7.1 Flujo de Intercepción de Enlaces

```
[ Navegador Web ] --(Usuario hace clic en un enlace .ZIP)--> [ Declarative Net Request / Downloads API ]
                                                                      |
                                                                      v
                                                    [ Background Service Worker ]
                                                                      |
                                            (Cancela descarga nativa del navegador)
                                                                      |
                                                                      v
                                                  [ Envia JSON con URL + Cookies + UA ]
                                                                      |
                                                                      v
                                              [ Native Messaging / WebSocket Local ]
                                                                      |
                                                                      v
                                                 [ SDM Core App - Inicia Descarga ]
```

### 7.2 Estructura Interna de la Extensión

*   `manifest.json`: Definición del proyecto, permisos estrictos (`downloads`, `declarativeNetRequest`, `nativeMessaging`, `storage`, `webRequest`).
*   `background.js`: Interceptor principal de descargas y gestor de eventos de comunicación con el programa de escritorio.
*   `content_script.js`: Inyección del widget flotante de captura sobre reproductores de video HTML5 (`<video>`).
*   `popup/`: Interfaz rápida en HTML/CSS/JS nativo para activar/desactivar la intercepción automática y gestionar la lista negra de sitios.

---

## 8. SERVIDOR DE COMUNICACIÓN LOCAL (IPC, SOCKETS & NATIVE MESSAGING)

SDM utiliza un esquema redundante de comunicación entre la extensión del navegador y la aplicación nativa para evitar desconexiones o fallos de puerto.

```
                  +-----------------------------------+
                  | Extensión Web (Background Worker) |
                  +-----------------------------------+
                                    |
            +-----------------------+-----------------------+
            |                                               |
            v (Canal Primario)                              v (Canal Secundario)
+------------------------+                     +------------------------+
| Native Messaging Host  |                     | WebSocket Server Local |
| (STDIN / STDOUT)       |                     | (127.0.0.1:9010)       |
+------------------------+                     +------------------------+
            |                                               |
            +-----------------------+-----------------------+
                                    |
                                    v
                  +-----------------------------------+
                  |  SDM Core IPC Router (C++ Qt)     |
                  +-----------------------------------+
```

### 8.1 Protocolo Native Messaging Protocol (Canal Primario)
Consiste en la ejecución de un pequeño binario puente o comunicación directa vía `STDIN` y `STDOUT`. Cada mensaje está prefijado por la longitud del paquete en formato entero no signado de 32 bits en orden nativo de bytes (*Little-Endian*), seguido de la carga útil en formato JSON.

### 8.2 WebSocket Local Cifrado (Canal Secundario / Fallback)
En caso de restricciones en la API de Native Messaging, SDM levanta un servidor `QWebSocketServer` que escucha exclusivamente en la interfaz de bucle de retorno local (`127.0.0.1`) en el puerto dinámico o fijo `9010`. Los mensajes requieren un token de autenticación local generado en la instalación del software para evitar que páginas externas maliciosas interactúen con el gestor.

---

## 9. MÓDULO MULTIMEDIA Y CAPTURA DE STREAMING (HLS & DASH)

### 9.1 Detección de Transmisiones en Reproductores Web
El script de contenido (`content_script.js`) analiza las peticiones de red del navegador buscando archivos de manifiesto multimedia:
*   **HLS (HTTP Live Streaming):** Archivos `.m3u8` que contienen la lista de reproducción de segmentos `.ts` o `.m4s`.
*   **DASH (Dynamic Adaptive Streaming over HTTP):** Archivos `.mpd` con manifiestos XML de representación de audio y video separados.

### 9.2 Integración del Motor Interno de Descarga Streaming
SDM integra un módulo especializado en C++ que interactúa con la herramienta `yt-dlp` en modo embebido sin interfaz, o parsea directamente los manifiestos `.m3u8` y `.mpd`:

1.  **Parseo de Calidades:** Extrae las resoluciones disponibles (1080p, 2K, 4K, 8K) y tasas de bits (*bitrates*).
2.  **Descarga Multihilo de Segmentos:** Descarga los bloques de video (`.ts`) y audio de forma paralela utilizando el mismo motor de aceleración.
3.  **Muxing / Ensamblado Transparente:** Unifica los flujos de audio y video descargados utilizando una librería estática de `FFmpeg` integrada, produciendo un archivo de salida único en formato `.MP4` o `.MKV` de forma totalmente transparente para el usuario.

---

## 10. MANEJO DE ERRORES, AUTO-REFRESCO DE ENLACES Y REANUDACIÓN INTELIGENTE

### 10.1 Gestión del Error HTTP 403 / Enlaces Temporales Caducados
Muchos servidores de alojamiento de archivos asignan enlaces con una vida útil corta (ej. tokens que expiran en 2 horas). Si una descarga se pausa y se intenta reanudar al día siguiente, el servidor responderá con un error `403 Forbidden` o `401 Unauthorized`.

```
[ Error 403 Detectado en Hilo de Descarga ]
                   |
                   v
[ SDM Entra en Estado "Pausa por Token Expirado" ]
                   |
                   v
[ Envia Solicitud de Refresco a Extensión de Navegador ]
                   |
                   v
[ Extensión Abre Silenciosamente la URL de Origen en Background ]
                   |
                   v
[ Captura el Nuevo Token de Sesión y Enlace Actualizado ]
                   |
                   v
[ Notifica a SDM Core con la Nueva URL + Cookies ]
                   |
                   v
[ SDM Actualiza la URL de la Tarea y REANUDA en el Byte Exacto ]
```

---

## 11. ESTRUCTURA DEL REPOSITORIO Y CONFIGURACIÓN DE PROYECTOS

El proyecto se organiza en un repositorio único monolítico (*monorepo*) para facilitar la sincronización entre el código C++, la extensión web y los recursos de empaquetado.

```text
/S-Download-Manager
│
├── .github/
│   └── workflows/
│       ├── build-windows.yml       # CI/CD Compilación Automática en MSVC
│       ├── build-linux.yml         # CI/CD Compilación AppImage/Deb
│       └── linting.yml             # Verificación de formato C++ y JS
│
├── CMakeLists.txt                  # Configuración Global de CMake
├── LICENSE                         # Licencia del Software
├── README.md                       # Documentación Principal
│
├── src/                            # CÓDIGO FUENTE APLICACIÓN DESKOTOP
│   ├── main.cpp                    # Punto de entrada de la aplicación
│   ├── core/                       # Motor de descargas y lógica
│   │   ├── DownloadEngine.cpp/.h   # Coordinador global de tareas
│   │   ├── SegmentWorker.cpp/.h    # Manejador de hilos de descarga con libcurl
│   │   ├── FileAllocator.cpp/.h    # Reservador de espacio asíncrono en disco
│   │   ├── BrowserEmulator.cpp/.h  # Módulo de suplantación User-Agent y TLS
│   │   └── StreamDownloader.cpp/.h # Procesador de manifiestos M3U8/MPD
│   │
│   ├── bridge/                     # Capa de comunicación IPC
│   │   ├── NativeMessagingHost.cpp/.h
│   │   ├── WebSocketServer.cpp/.h
│   │   └── ProtocolHandler.cpp/.h
│   │
│   ├── database/                   # Capa de persistencia
│   │   ├── DatabaseManager.cpp/.h
│   │   └── Models.h                # Estructuras de datos de descargas
│   │
│   └── ui/                         # INTERFAZ GRÁFICA (Qt 6)
│       ├── GuiManager.cpp/.h
│       ├── qml/
│       │   ├── main.qml            # Ventana Principal Fluent Design
│       │   ├── components/
│       │   │   ├── DownloadTable.qml
│       │   │   ├── SegmentBar.qml
│       │   │   ├── Sidebar.qml
│       │   │   ├── TopToolbar.qml
│       │   │   └── NewTaskModal.qml
│       │   └── theme/
│       │       ├── Theme.qml       # Paleta de colores Dark/Light Fluent
│       │       └── Style.qml
│       └── assets/                 # Recurso de iconos vectoriales SVG
│
├── extension/                      # EXTENSIÓN DE NAVEGADOR (MANIFEST V3)
│   ├── manifest.json
│   ├── background/
│   │   └── service_worker.js
│   ├── content/
│   │   ├── content_script.js
│   │   └── media_detector.js
│   ├── popup/
│   │   ├── popup.html
│   │   ├── popup.css
│   │   └── popup.js
│   └── icons/
│       ├── icon16.png
│       ├── icon48.png
│       └── icon128.png
│
└── packaging/                      # INSTALADORES Y EMPAQUETADO
    ├── windows/
    │   ├── installer.nsi           # Script para Instalador NSIS
    │   └── app.manifest            # Manifest para escalado High-DPI y Windows 10/11
    └── linux/
        └── sdm.desktop
```

---

## 12. ESTRATEGIA DE CI/CD CON GITHUB ACTIONS PARA WINDOWS 10/11

El pipeline de integración y despliegue continuo (CI/CD) está configurado para ejecutarse en cada `push` o `pull request` en la rama `main`. Genera de manera automática los ejecutables binarios y el instalador listo para distribución.

### 12.1 Flujo de Trabajo de GitHub Actions (`.github/workflows/build-windows.yml`)

```yaml
name: Build and Package SDM for Windows

on:
  push:
    branches: [ "main" ]
  pull_request:
    branches: [ "main" ]

jobs:
  build-windows:
    runs-on: windows-2022

    steps:
    - name: Checkout Código Repositorio
      uses: actions/checkout@v5
      with:
        submodules: recursive

    - name: Configurar Entorno de C++ (MSVC 2022)
      uses: ilammy/msvc-dev-cmd@v3

    - name: Instalar Qt 6.6 Framework
      uses: jurplel/install-qt@v5
      with:
        version: '6.6.0'
        host: 'windows'
        target: 'desktop'
        arch: 'win64_msvc2019_64'
        dir: '${{ github.workspace }}/qt'

    - name: Instalar CMake y Ninja
      uses: jtimon/install-ninja-action@v3

    - name: Configurar Proyecto con CMake
      run: |
        cmake -B build -G "Ninja" `
          -DCMAKE_BUILD_TYPE=Release `
          -DCMAKE_PREFIX_PATH="${{ github.workspace }}/qt/6.6.0/msvc2019_64"

    - name: Compilar Proyecto Core y UI
      run: |
        cmake --build build --config Release

    - name: Ejecutar Pruebas Unitarias Automatizadas
      run: |
        ctest --test-dir build --output-on-failure

    - name: Desplegar Dependencias de Qt (windeployqt)
      run: |
        ${{ github.workspace }}/qt/6.12.0/msvc2024_64/bin/windeployqt.exe --qmldir src/ui/qml build/Release/SDM.exe

    - name: Empaquetar Artefacto Binario (.zip)
      uses: actions/upload-artifact@v5
      with:
        name: SDM-Windows-x64-Portable
        path: |
          build/Release/*
```

---

## 13. HOJA DE RUTA DETALLADA DE DESARROLLO (ROADMAP POR FASES)

```
FASE 1: CORE ENGINE    FASE 2: INTERFAZ UI     FASE 3: BROWSER BRIDGES  FASE 4: TESTING & RELEASE
[ SeMANAS 1 - 4 ]      [ SEMANAS 5 - 8 ]       [ SEMANAS 9 - 11 ]      [ SEMANAS 12 - 14 ]
+-------------------+  +-------------------+   +-------------------+   +--------------------+
| • Integration Curl|  | • Qt 6 QML Layout |   | • Extensión MV3   |   | • Pruebas Estrés   |
| • Engine 1/6/8/16 |  | • Dark Fluent UI  |   | • Native Host IPC |   | • Anti-Error 403   |
| • Allocator Disco |  | • Thread Monitor  |   | • Stream Capture  |   | • Installer Build  |
+-------------------+  +-------------------+   +-------------------+   +--------------------+
```

### Fase 1: Arquitectura Base y Motor de Descargas (1 a 4)
*   **:** Configuración del repositorio CMake, integración de `libcurl` y desarrollo del módulo de solicitudes HTTP con soporte `Range`.
*   **** Desarrollo del algoritmo de segmentación dinámica multihilo (1, 6, 8 y 16 hilos) con balanceador de carga automático.
*   **:** Implementación del sistema de reserva asíncrona de espacio en disco y escritura por desplazamientos (*offsets*).
*   **:** Desarrollo del módulo `BrowserEmulator` para la rotación de User-Agents y suplantación de cabeceras HTTP/TLS.

### Fase 2: Interfaz Gráfica Nativa Fluent Design (5 a 8)
*   **:** Maquetación en QML del panel principal, barra de herramientas y lista de categorías con soporte para tema oscuro.
*   **:** Creación de la tabla de descargas virtualizada de alto rendimiento optimizada para GPU.
*   **:** Implementación del componente de visualización gráfica de hilos/segmentos en tiempo real.
*   **:** Desarrollo de diálogos emergentes (Captura de nueva tarea, ajustes, preferencias de red).

### Fase 3: Integración con Navegadores y Captura Multimedia (9 a 11)
*   **:** Creación de la extensión Manifest V3 para Chrome, Edge y Firefox.
*   **:** Implementación del protocolo Native Messaging y servidor WebSocket local cifrado para IPC.
*   **:** Desarrollo del módulo de detección de transmisiones de video (HLS `.m3u8` y DASH `.mpd`) con widget flotante sobre el reproductor HTML5.

### Fase 4: Estabilización, Optimización y Despliegue (12 a 14)
*   **** Implementación del algoritmo de refresco automático de enlaces expirados (solución al error HTTP 403).
*   **:** Pruebas de rendimiento intensivas (descarga de archivos de más de 50 GB a velocidades de 1 Gbps) y perfilado de memoria con Valgrind/Visual Studio Profiler.
*   **:** Configuración de instaladores automatizados para Windows 10/11 (NSIS) y cierre de release ejecutable en GitHub.

---

## 14. CONSIDERACIONES DE SEGURIDAD, CERTIFICACIÓN Y MANTENIMIENTO

### 14.1 Prevención de Falsos Positivos en Antivirus
Dado que SDM intercepta el tráfico de red e interactúa directamente con el navegador mediante Sockets locales, los motores heurísticos de algunos antivirus (como Windows Defender) podrían emitir falsos positivos si el código no está empaquetado adecuadamente.
*   **Firma Digital de Código (Code Signing):** Todos los ejecutables y archivos `.DLL` producidos en el pipeline de CI/CD deben ser firmados digitalmente mediante un certificado EV (Extended Validation) de Sectigo o DigiCert.
*   **Inclusión en Registros de Windows:** Utilización de las APIs oficiales de integración del sistema operativo (*Windows Register Protocol*) para declarar la extensión del navegador sin alterar archivos de sistema confidenciales.

### 14.2 Mantenimiento Continuo de la Extensión
Debido a las frecuentes actualizaciones de seguridad de los navegadores basados en Chromium (que suelen modificar políticas de permisos en Manifest V3), la extensión debe mantener un canal de comunicación independiente mediante solicitudes API remotas para actualizar la lista de patrones de captura sin requerir una reinstalación completa de la aplicación de escritorio.

---
*Fin del Blueprint de Arquitectura Técnica - Proyecto S-Download Manager (SDM)*