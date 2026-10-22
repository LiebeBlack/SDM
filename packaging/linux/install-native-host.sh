#!/usr/bin/env bash
# ============================================================================
#  S-DOWNLOAD MANAGER (SDM)
#  packaging/linux/install-native-host.sh
#
#  Registra el puente Native Messaging (com.sdm.bridge) para Chrome, Chromium,
#  Edge, Brave y Firefox en el perfil del usuario actual.
#
#  Uso:
#    ./install-native-host.sh [ID_EXTENSION] [RUTA_HOST]
#
#    ID_EXTENSION   ID de la extensión MV3 (chrome://extensions).  Opcional:
#                   se omite el registro Chrome si se deja vacío.
#    RUTA_HOST      Ruta absoluta de SDMNativeHost.  Por defecto:
#                   /usr/bin/SDMNativeHost
# ============================================================================
set -euo pipefail

EXT_ID="${1:-}"
HOST_BIN="${2:-/usr/bin/SDMNativeHost}"
HOST_NAME="com.sdm.bridge"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -x "$HOST_BIN" ]]; then
    echo "Aviso: no existe el binario del host en '$HOST_BIN' (se registrará igualmente)." >&2
fi

# --- Chrome / Chromium / Edge / Brave ---------------------------------------
if [[ -n "$EXT_ID" ]]; then
    for base in \
        "$HOME/.config/google-chrome" \
        "$HOME/.config/chromium" \
        "$HOME/.config/microsoft-edge" \
        "$HOME/.config/BraveSoftware/Brave-Browser"; do
        dir="$base/NativeMessagingHosts"
        mkdir -p "$dir"
        sed -e "s|@SDM_NATIVE_HOST_PATH@|$HOST_BIN|g" \
            -e "s|@EXTENSION_ID@|$EXT_ID|g" \
            "$SCRIPT_DIR/../native-host/com.sdm.bridge.chrome.json" \
            > "$dir/$HOST_NAME.json"
        echo "Registrado (Chrome/Chromium): $dir/$HOST_NAME.json"
    done
else
    echo "Sin ID de extensión: se omite el registro para Chrome/Edge/Brave." >&2
fi

# --- Firefox -----------------------------------------------------------------
FF_DIR="$HOME/.mozilla/native-messaging-hosts"
mkdir -p "$FF_DIR"
sed -e "s|@SDM_NATIVE_HOST_PATH@|$HOST_BIN|g" \
    -e "s|@FIREFOX_EXTENSION_ID@|sdm@sdm.local|g" \
    "$SCRIPT_DIR/../native-host/com.sdm.bridge.firefox.json" \
    > "$FF_DIR/$HOST_NAME.json"
echo "Registrado (Firefox): $FF_DIR/$HOST_NAME.json"

echo "Listo. Reinicie el navegador para que detecte el puente."
