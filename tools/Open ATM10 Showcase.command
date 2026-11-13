#!/bin/bash
set -euo pipefail
showcase_root=$(cd "$(dirname "$0")" && pwd)
if [[ $(/usr/bin/uname -m) == arm64 ]] || [[ $(/usr/sbin/sysctl -n hw.optional.arm64 2>/dev/null || true) == 1 ]]; then
    runtime_key=macos-arm64
else
    runtime_key=macos-x86_64
fi
pins="$showcase_root/tools/showcase_runtime_pins.json"
runtime_url=$(/usr/bin/plutil -extract "runtimes.$runtime_key.url" raw -o - "$pins")
runtime_sha=$(/usr/bin/plutil -extract "runtimes.$runtime_key.sha256" raw -o - "$pins")
runtime_size=$(/usr/bin/plutil -extract "runtimes.$runtime_key.size" raw -o - "$pins")
[[ "$runtime_url" == https://* && "$runtime_sha" =~ ^[0-9a-f]{64}$ ]] || { echo 'Invalid pinned Python runtime metadata' >&2; exit 1; }
runtime_base="$showcase_root/.showcase-runtime"
runtime="$runtime_base/$runtime_key-${runtime_sha:0:12}"
python="$runtime/python/bin/python3"
[[ ! -L "$runtime_base" && ! -L "$runtime" ]] || { echo 'Python runtime cache must not be a symlink' >&2; exit 1; }
stage=''
trap 'if [[ -n "$stage" && -d "$stage" ]]; then rm -rf "$stage"; fi' EXIT
if [[ ! -x "$python" ]]; then
    mkdir -p "$runtime_base"
    stage=$(mktemp -d "$runtime_base/.download.XXXXXX")
    echo 'Downloading showcase setup tools...'
    /usr/bin/curl --fail --location --proto '=https' --proto-redir '=https' --retry 2 --max-time 180 --max-filesize "$runtime_size" \
        "$runtime_url" --output "$stage/runtime.tar.gz"
    actual_sha=$(/usr/bin/shasum -a 256 "$stage/runtime.tar.gz" | /usr/bin/awk '{print $1}')
    [[ "$actual_sha" == "$runtime_sha" ]] || { echo 'Python runtime SHA-256 does not match its pin' >&2; exit 1; }
    /usr/bin/tar -xzf "$stage/runtime.tar.gz" -C "$stage"
    rm "$stage/runtime.tar.gz"
    [[ -x "$stage/python/bin/python3" ]] || { echo 'Python runtime archive is incomplete' >&2; exit 1; }
    if [[ ! -e "$runtime" ]]; then
        mv "$stage" "$runtime"
        stage=''
    fi
    [[ -x "$python" ]] || { echo 'Python runtime cache is incomplete' >&2; exit 1; }
fi
cd "$showcase_root"
export SSL_CERT_FILE=/etc/ssl/cert.pem
"$python" -E -s "$showcase_root/tools/prepare_atm10_showcase.py" "$@"
