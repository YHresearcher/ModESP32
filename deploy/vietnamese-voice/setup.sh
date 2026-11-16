#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p data
if [[ -e data/.config.yaml ]]; then
  echo "data/.config.yaml already exists; refusing to overwrite your credentials."
  exit 1
fi
LAN_IP="${LAN_IP:-$(ipconfig getifaddr en0 2>/dev/null || true)}"
if [[ -z "$LAN_IP" ]]; then
  echo "No LAN_IP found. Set LAN_IP to your Mac's LAN IPv4 and retry."
  exit 1
fi
if [[ ! "$LAN_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid LAN_IP: must be IPv4." >&2
  exit 1
fi
if [[ -z "${GROQ_API_KEY:-}" ]]; then
  read -r -s -p "Enter Groq API key (hidden): " GROQ_API_KEY
  echo
fi
if [[ -z "$GROQ_API_KEY" ]]; then
  echo "Groq API key is required for Vietnamese ASR." >&2
  exit 1
fi
export LAN_IP GROQ_API_KEY
python3 - <<'PY'
import os, pathlib
p=pathlib.Path("config.override.template.yaml")
s=p.read_text(encoding="utf-8")
# JSON string quoting is also valid YAML double-quoted scalar syntax.
import json
key=json.dumps(os.environ["GROQ_API_KEY"])[1:-1]
for old,new in [("__LAN_IP__", os.environ["LAN_IP"]),("__GROQ_KEY__",key)]:
    s=s.replace(old,new)
out=pathlib.Path("data/.config.yaml")
with out.open("x", encoding="utf-8") as f:
    f.write(s)
out.chmod(0o600)
print("Created data/.config.yaml (mode 0600).")
print("LAN websocket:", "ws://"+os.environ["LAN_IP"]+":8000/xiaozhi/v1/")
PY
echo "Next: ollama pull qwen2.5:3b && docker compose up -d"
