; ============================================================================
;  S-DOWNLOAD MANAGER (SDM)
;  packaging/windows/installer.nsi
;
;  Instalador NSIS (Unicode, per-user sin elevation):
;   - Binarios: SDM.exe + SDMNativeHost.exe + runtime de Qt (windeployqt)
;   - Extensión del navegador desempaquetable (carpeta extension)
;   - Registro del puente Native Messaging para Chrome / Edge / Brave / Firefox
;   - Menú Inicio, escritorio y desinstalador
;
;  Uso (CI):
;    makensis packaging/windows/installer.nsi
;
;  Los binarios se esperan en ..\..\dist\windows (cmake --install + windeployqt)
;  y la extensión en ..\..\extension.
; ============================================================================

; NOTA: guardar este fichero como UTF-8 CON BOM (combinado con 'Unicode true').
; makensis lee el BOM y convierte los acentos/simbolos a Unicode; sin BOM
; interpreta el script como ANSI (ACP) y el texto sale corrupto.
Unicode true
; Declaracion de compatibilidad en el manifiesto del instalador (Win10/11 usa
; el GUID de Win10). Equivale a los supportedOS de packaging/windows/app.manifest.
ManifestSupportedOS Win8.1 Win10

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "nsDialogs.nsh"
!include "WinMessages.nsh"

!define PRODUCT_NAME "S-Download Manager"
!define PRODUCT_NAME_SHORT "SDM"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "SDM Project"
!define PRODUCT_URL "https://sdownload.local"
!define PRODUCT_EXE "$INSTDIR\bin\SDM.exe"
!define NATIVE_HOST_NAME "com.sdm.bridge"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "..\..\dist\SDM-Setup-${PRODUCT_VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\SDM"
InstallDirRegKey HKCU "Software\${PRODUCT_NAME}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUnInstDetails show

; ---------------------------------------------------------------------------
; Puente nativo: SIN configuracion manual.
;  - Firefox usa un id de extension fijo (sdm@sdm.local) y se registra aqui.
;  - Chrome/Chromium/Edge/Brave/Vivaldi/Opera se registran aqui y el propio SDM
;    rellena `allowed_origins` al arrancar: detecta el ID de la extension en los
;    perfiles del navegador (src/bridge/NativeBridgeRegistrar.cpp) y, si aun no
;    esta cargada, reintenta cada minuto hasta encontrarla.
; ---------------------------------------------------------------------------

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; Utilidad: duplica barras invertidas de $R9 en $R8 (JSON exige \\ )
; ---------------------------------------------------------------------------
Function EscapeJsonPath
    ; Entrada: $R9  Salida: $R8
    StrCpy $R8 ""
    StrCpy $R7 0
    ${Do}
        StrCpy $R6 "$R9" 1 $R7
        ${If} $R6 == ""
            ${Break}
        ${EndIf}
        ${If} $R6 == "\"
            StrCpy $R8 "$R8\\"
        ${Else}
            StrCpy $R8 "$R8$R6"
        ${EndIf}
        IntOp $R7 $R7 + 1
    ${Loop}
FunctionEnd

Section "Programa (obligatorio)" SecMain
    SectionIn RO

    SetOutPath "$INSTDIR"
    File /r "..\..\dist\windows\*.*"

    ; --- Comprobacion de dependencias dinamicas ---------------------------
    ; Si el paquete se genero sin las DLLs de vcpkg (libcurl/sqlite3/fmt), la
    ; app moriria al arrancar con un error opaco de Windows. Mejor avisar aqui.
    StrCpy $R1 0
    ${IfNot} ${FileExists} "$INSTDIR\bin\libcurl.dll"
        StrCpy $R1 1
    ${EndIf}
    ${IfNot} ${FileExists} "$INSTDIR\bin\sqlite3.dll"
        StrCpy $R1 1
    ${EndIf}
    ${IfNot} ${FileExists} "$INSTDIR\bin\fmt.dll"
        StrCpy $R1 1
    ${EndIf}
    ${If} $R1 == 1
        MessageBox MB_ICONEXCLAMATION|MB_OK "El instalador no encontro las librerias de dependencias (libcurl, sqlite3, fmt) en $INSTDIR\bin.$\r$\n$\r$\nSDM no arrancara. Descargue de nuevo el instalador desde la pagina de releases: el paquete esta incompleto (el runtime de MSVC ya va incluido en $INSTDIR\bin, no necesita el redistribuible de Visual C++)."
    ${EndIf}

    ; --- Menú Inicio -------------------------------------------------------
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "${PRODUCT_EXE}"
    CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\Desinstalar.lnk" "$INSTDIR\uninstall.exe"

    ; --- Escritorio (opcional via .onInit checkbox no incluido: siempre) ---
    CreateShortcut "$DESKTOP\${PRODUCT_NAME}.lnk" "${PRODUCT_EXE}"

    ; --- Extension del navegador (cargarla como desempaquetada) ------------
    SetOutPath "$INSTDIR\extension"
    File /r "..\..\extension\*.*"
    FileOpen $0 "$INSTDIR\extension\INSTALAR.txt" w
    FileWrite $0 "S-Download Manager - instalacion de la extension$\r$\n$\r$\n"
    FileWrite $0 "1. Abra chrome://extensions (o edge://extensions).$\r$\n"
    FileWrite $0 "2. Active el 'Modo de desarrollador'.$\r$\n"
    FileWrite $0 "3. 'Cargar desempaquetada' y seleccione esta carpeta:$\r$\n"
    FileWrite $0 "    $INSTDIR\extension$\r$\n"
    FileWrite $0 "4. Copie el ID mostrado y vuelva a ejecutar el instalador$\r$\n"
    FileWrite $0 "   (o use SDM -> Ajustes -> Puente) para registrarlo.$\r$\n"
    FileWrite $0 "Firefox: anada browser_specific_settings.gecko.id = sdm@sdm.local$\r$\n"
    FileWrite $0 "al manifest.json antes de cargarlo como complemento temporario.$\r$\n"
    FileClose $0

    ; --- Registro de desinstalador ----------------------------------------
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "${PRODUCT_URL}"
    WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "${PRODUCT_EXE}"
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1

    WriteRegStr HKCU "Software\${PRODUCT_NAME}" "InstallDir" "$INSTDIR"

    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Puente nativo (Native Messaging)" SecNativeHost
    ; --- JSON para Chrome / Edge / Brave / Vivaldi / Opera --------------
    StrCpy $R9 "$INSTDIR\bin\SDMNativeHost.exe"
    Call EscapeJsonPath
    ; $R8 = ruta escapada para JSON

    ; `allowed_origins` se deja vacio a proposito: SDM lo rellena al arrancar con
    ; el ID real que detecta en el perfil del navegador (auto-configuracion).
    SetOutPath "$INSTDIR"
    FileOpen $0 "$INSTDIR\com.sdm.bridge.chrome.json" w
    FileWrite $0 '{$\n'
    FileWrite $0 '  "name": "${NATIVE_HOST_NAME}",$\n'
    FileWrite $0 '  "description": "Puente nativo de S-Download Manager (SDM)",$\n'
    FileWrite $0 '  "path": "$R8",$\n'
    FileWrite $0 '  "type": "stdio",$\n'
    FileWrite $0 '  "allowed_origins": [$\n'
    FileWrite $0 '  ]$\n'
    FileWrite $0 '}$\n'
    FileClose $0

    ; --- JSON para Firefox -------------------------------------------------
    FileOpen $0 "$INSTDIR\com.sdm.bridge.firefox.json" w
    FileWrite $0 '{$\n'
    FileWrite $0 '  "name": "${NATIVE_HOST_NAME}",$\n'
    FileWrite $0 '  "description": "Puente nativo de S-Download Manager (SDM)",$\n'
    FileWrite $0 '  "path": "$R8",$\n'
    FileWrite $0 '  "type": "stdio",$\n'
    FileWrite $0 '  "allowed_extensions": [$\n'
    FileWrite $0 '    "sdm@sdm.local"$\n'
    FileWrite $0 '  ]$\n'
    FileWrite $0 '}$\n'
    FileClose $0

    ; --- Registro por navegador (HKCU, sin administrador) -----------------
    WriteRegStr HKCU "Software\Google\Chrome\NativeMessagingHosts\${NATIVE_HOST_NAME}" "" "$INSTDIR\com.sdm.bridge.chrome.json"
    WriteRegStr HKCU "Software\Chromium\NativeMessagingHosts\${NATIVE_HOST_NAME}" "" "$INSTDIR\com.sdm.bridge.chrome.json"
    WriteRegStr HKCU "Software\Microsoft\Edge\NativeMessagingHosts\${NATIVE_HOST_NAME}" "" "$INSTDIR\com.sdm.bridge.chrome.json"
    WriteRegStr HKCU "Software\BraveSoftware\Brave-Browser\NativeMessagingHosts\${NATIVE_HOST_NAME}" "" "$INSTDIR\com.sdm.bridge.chrome.json"
    WriteRegStr HKCU "Software\Vivaldi\NativeMessagingHosts\${NATIVE_HOST_NAME}" "" "$INSTDIR\com.sdm.bridge.chrome.json"
    WriteRegStr HKCU "Software\Opera Software\NativeMessagingHosts\${NATIVE_HOST_NAME}" "" "$INSTDIR\com.sdm.bridge.chrome.json"
    WriteRegStr HKCU "Software\Mozilla\NativeMessagingHosts\${NATIVE_HOST_NAME}" "" "$INSTDIR\com.sdm.bridge.firefox.json"
SectionEnd

Section "Asociaciones de archivos (.sdm)" SecAssoc
    ; Gestionador de descargas: archivo de tareas exportado
    WriteRegStr HKCU "Software\Classes\.sdm" "" "SDM.TaskFile"
    WriteRegStr HKCU "Software\Classes\SDM.TaskFile" "" "Tarea de S-Download Manager"
    WriteRegStr HKCU "Software\Classes\SDM.TaskFile\DefaultIcon" "" "${PRODUCT_EXE},0"
    WriteRegStr HKCU "Software\Classes\SDM.TaskFile\shell\open\command" "" '"$INSTDIR\bin\SDM.exe" "%1"'
SectionEnd

Section Uninstall
    ; Asociaciones
    DeleteRegKey HKCU "Software\Classes\.sdm"
    DeleteRegKey HKCU "Software\Classes\SDM.TaskFile"

    ; Puente nativo
    DeleteRegKey HKCU "Software\Google\Chrome\NativeMessagingHosts\${NATIVE_HOST_NAME}"
    DeleteRegKey HKCU "Software\Chromium\NativeMessagingHosts\${NATIVE_HOST_NAME}"
    DeleteRegKey HKCU "Software\Microsoft\Edge\NativeMessagingHosts\${NATIVE_HOST_NAME}"
    DeleteRegKey HKCU "Software\BraveSoftware\Brave-Browser\NativeMessagingHosts\${NATIVE_HOST_NAME}"
    DeleteRegKey HKCU "Software\Vivaldi\NativeMessagingHosts\${NATIVE_HOST_NAME}"
    DeleteRegKey HKCU "Software\Opera Software\NativeMessagingHosts\${NATIVE_HOST_NAME}"
    DeleteRegKey HKCU "Software\Mozilla\NativeMessagingHosts\${NATIVE_HOST_NAME}"
    Delete "$INSTDIR\com.sdm.bridge.chrome.json"
    Delete "$INSTDIR\com.sdm.bridge.firefox.json"

    ; Accesos directos
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\Desinstalar.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

    ; Binarios y desinstalador
    Delete "$INSTDIR\uninstall.exe"
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\extension"
    RMDir /r "$INSTDIR\qml" ; puede no existir: RMDir falla sin mas si no esta
    RMDir "$INSTDIR"

    DeleteRegKey HKCU "Software\${PRODUCT_NAME}"
    DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd
