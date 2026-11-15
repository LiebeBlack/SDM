# SDM — Lista de producción (calidad empresarial)

Checklist de salida a producción para S-Download Manager. Cada bloque debe
estar ✅ antes de publicar una versión estable.

## 1. Compilación y CI

- [ ] `cmake --preset release` compila sin warnings (`-Wall -Wextra -Werror` en GCC/Clang, `/W4` en MSVC)
- [ ] CI verde en ambas plataformas: `.github/workflows/build-windows.yml` y `build-linux.yml`
- [ ] `lint.yml` (clang-format) sin diferencias
- [ ] Versión sincronizada en: `CMakeLists.txt`, `main.cpp` (`setApplicationVersion`), `GuiManager::registerContext` (`appVersion`), `extension/manifest.json`
- [ ] Build reproducible: dependencias fijadas por hash en `vcpkg.json` / vcpkg-baseline
- [ ] Símbolos PDB/DWARF separados del binario de release (depuración de campo)

## 2. Pruebas

- [x] Tests unitarios del core verdes (`tests/`): segmentación (cobertura exacta/contigüidad), `sanitizeFileName`, `fileNameFromUrl`, `looksLikeHtmlDocument`, `isHtmlLikeFileName`, formateadores y persistencia SQLite (round-trip, cascadas, refresco de URL)
- [ ] Test de reanudación byte exacta tras corte (integración con servidor real)
- [ ] Tests de integración: pausa/reanudación con corte de red, ETag cambiante (reinicio limpio), servidor sin `Range` (degradación a 1 hilo), HTTP 403 → refresco de enlace
- [ ] Test anti-descarga-de-web: respuesta HTML con 200 no se guarda nunca como archivo; primer strike refresca enlace, segundo strike falla con mensaje
- [ ] Test de volumen: 10.000 tareas en la tabla virtualizada sin degradación de UI
- [ ] Test de estrés: 16 hilos × 3 tareas simultáneas durante 24 h sin fuga de memoria ni corrupción
- [ ] Prueba manual de aceptación (UAT) en Windows 10/11 y Ubuntu 22.04+
- [ ] Prueba de la extensión en Chrome, Edge, Brave y Firefox (native messaging + fallback WS)

## 3. Seguridad

- [x] Token del puente WebSocket: ≥16 bytes aleatorios (CSPRNG), solo `127.0.0.1`, validado en cada mensaje, comparación en tiempo constante
- [x] Contrapresión del puente: frames ≤ 1 MiB, handshake con timeout de 5 s y tope de conexiones sin autenticar
- [ ] `com.sdm.bridge` / `com.sdm.nativehost`: `allowed_origins` con ID de extensión real (nunca placeholder en instalador)
- [ ] Sin ejecución de binarios de rutas de usuario en `ffmpegPath`/`ytDlpPath` sin confirmación
- [ ] Validación de URL (solo http/https/ftp) y sanitización de nombres de archivo (traversal `../`, caracteres reservados Windows)
- [ ] TLS ≥ 1.2 forzado; verificación de certificado activa (`CURLOPT_SSL_VERIFYPEER`)
- [ ] Cookies/credenciales nunca persistidas en claro: se guardan solo en la BD local con permisos restrictivos
- [ ] Dependencias auditadas (libcurl, Qt, SQLite, OpenSSL) sin CVEs críticas abiertas
- [ ] Firma de código del binario y del instalador (certificado de firma válido)

## 4. Empaquetado e instalación

- [ ] Instalador NSIS (`packaging/windows/installer.nsi`) probado en máquina limpia: instala, actualiza y desinstala sin residuos
- [ ] Paquete `.deb`/`.desktop` válido en Linux (`packaging/linux/`)
- [ ] Manifiesto nativo se escribe con la ruta e ID correctos en la instalación
- [ ] Desinstalación limpia: no borra datos de usuario salvo que se pida
- [ ] Instalación silenciosa (`/S`) verificada para despliegue corporativo (SCCM/Intune/GPO)

## 5. Rendimiento y recursos

- [ ] UI a 60 fps sostenidos con 50.000 filas (tabla virtualizada + `reuseItems`)
- [ ] Memoria RSS estable < 150 MB en reposo; sin crecimiento en 24 h de descarga continua
- [ ] Render: MSAA 4x + stencil configurados antes de crear la ventana; sin overdraw en la lista
- [ ] Muestreo de telemetría acotado: 1 s (gráfico), 0.5 s (segmentos), 2 s (stats); sin timers huérfanos al cerrar
- [ ] Uso de CPU < 2 % en reposo (sin animaciones infinitas activas cuando la ventana está oculta)
- [ ] Hilos de descarga limitados por configuración; cola FIFO sin bucles de espera activa

## 6. Funcionalidad (regresión completa)

- [ ] Alta de tarea manual, desde portapapeles, extensión y menú contextual
- [ ] Pausa/reanudación byte exacta tras cierre y reapertura de la app
- [ ] HLS/DASH: selección de calidad con timeout automático al mejor variante
- [ ] Refresco de enlaces caducados (403/401) con reintento vía navegador
- [ ] Anti-HTML: nunca se guarda una web como archivo (ver §2)
- [ ] Programación de inicio: varias tareas programadas coexisten y arrancan a su hora; sobreviven al cierre del diálogo
- [ ] Cambio de hilos, destino, proxy y tema en caliente
- [ ] Instancia única: segundo arranque eleva la ventana existente
- [ ] Modo `--minimized` y CLI (`--port`, `--dir`)

## 7. UX y accesibilidad

- [ ] Tema oscuro/claro completos sin colores hard-coded fuera de `Theme.qml`
- [x] Animaciones con `Behavior`/`Transition` coherentes (120–320 ms, `OutCubic`); tipo de animación coincidente con el tipo de propiedad en todos los `Behavior`
- [x] Atajos documentados: Ctrl+N, Ctrl+O, Ctrl+,, Ctrl+Delete, Ctrl+Shift+P/R
- [x] Estados vacíos, de error y de carga con mensajes accionables
- [ ] Texto legible a 100/150/200 % de escala del sistema
- [x] Tooltips en todas las acciones de la barra y la tabla
- [x] API QML verificada contra la documentación oficial de Qt 6: sin tipos/propiedades inexistentes (`Dialog.titleItem`, `TimeEdit`, `FolderDialog.selectedFolder`), sin invocaciones no invocables (`rowCount` desde QML) y con `qmldir` de singletons registrado en `resources.qrc`
- [x] Diálogos alineados con design tokens (cabecera/pie/contenido con `Style.padding*`; sin márgenes mágicos) y programación de inicio con cola múltiple (varias tareas programadas coexisten)

## 8. Observabilidad y soporte

- [ ] Log rotado en `%APPDATA%/SDM` (~/.local/share/SDM), sin datos sensibles (cookies/tokens enmascarados)
- [ ] Nivel de log configurable (info/debug) sin recompilar
- [ ] Informe de diagnóstico exportable (versión, SO, ajustes, últimas tareas y errores)
- [ ] Mensajes de error al usuario en lenguaje claro + código de error correlacionable con el log

## 9. Documentación y cumplimiento

- [ ] README, PROTOCOL.md y OSDM.md actualizados con cada cambio de protocolo
- [ ] Licencias de terceros (Qt LGPL/GPL, libcurl, SQLite, OpenSSL) incluidas en el instalador
- [ ] Política de privacidad: qué datos locales se almacenan y cómo se eliminan
- [ ] Registro de cambios (CHANGELOG) por versión con semántica semver

## 10. Despliegue y rollback

- [ ] Canal beta (GitHub pre-release) validado antes del canal estable
- [ ] Plan de rollback probado: instalador de la versión anterior disponible
- [ ] Migración de BD versionada (esquema `user_version`) con rollback seguro
- [ ] Actualización sin pérdida de cola de descargas en curso
