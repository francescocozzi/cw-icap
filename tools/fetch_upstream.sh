#!/usr/bin/env bash
# Scarica in ext/ (non versionato) i riferimenti upstream:
#  - icap-sdk: SDK ufficiale ICAP (icap.h, tools/app.ld, icap_start.S, mkapp.py), licenza MIT
#  - repo SQ6EMM (patcher + strumenti di reverse engineering, licenza MIT)
#  - il bundle delle app ICAP, spacchettato in ext/apps/705 e ext/apps/9700
# Nessun firmware Icom viene scaricato: il .dat ufficiale lo prendi tu da icom.co.jp
# e lo tieni in ext/firmware/ se vuoi analizzare il loader con Ghidra.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p ext/apps ext/firmware

clone_or_pull() {
  if [ -d "ext/$2/.git" ]; then git -C "ext/$2" pull --ff-only; else git clone --depth 1 "$1" "ext/$2"; fi
}
clone_or_pull https://github.com/sq6emm/icap-sdk.git icap-sdk
clone_or_pull https://github.com/sq6emm/icom-custom-firmware.git icom-custom-firmware
clone_or_pull https://github.com/sq6emm/ic705-ic9700-ic905-bring-your-own-firmware.git byo-firmware

for z in ext/icom-custom-firmware/docs/apps/ICAP-apps-*.zip; do
  case "$z" in
    *705*)  d=ext/apps/705 ;;
    *9700*) d=ext/apps/9700 ;;
    *) continue ;;
  esac
  rm -rf "$d"; mkdir -p "$d"
  unzip -q -o "$z" -d "$d"
  echo "app di riferimento: $d ($(basename "$z"))"
done

python3 tools/icap_tool.py info ext/apps/705/*.APP
echo
echo
if cmp -s app/icap.h ext/icap-sdk/icap.h; then
  echo "app/icap.h coincide con l'SDK ($(git -C ext/icap-sdk log -1 --format='%h %cs'))."
else
  echo "ATTENZIONE: l'icap.h dell'SDK è cambiato. Copialo in app/ e aggiorna SVC_LAYOUT in"
  echo "tools/icap_sim.py e docs/ICAP_ABI.md nello stesso commit (CLAUDE.md, regola 3)."
fi
