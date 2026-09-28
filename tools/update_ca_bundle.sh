#!/bin/sh
# Regenerate data/cert/x509_crt_bundle — the root CAs the heartbeat uses to
# verify HTTPS servers — from Mozilla's list (via the certifi package).
#
# Needs: pip install certifi cryptography
#        PlatformIO's Arduino-ESP32 framework (run `pio run` once), or set
#        GEN_CRT_BUNDLE to the path of gen_crt_bundle.py.
set -e
cd "$(dirname "$0")/.."

GEN="${GEN_CRT_BUNDLE:-$HOME/.platformio/packages/framework-arduinoespressif32/tools/gen_crt_bundle.py}"
CERTS="$(python3 -c 'import certifi; print(certifi.where())')"

cd data/cert
python3 "$GEN" -i "$CERTS"
echo "Updated data/cert/x509_crt_bundle from $CERTS"
