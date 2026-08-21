#!/usr/bin/env bash

set -euo pipefail
umask 077

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
INVENTORY="$SCRIPT_DIR/prisma-runners.json"

usage() {
  echo "usage: install-prisma-runner.sh RUNNER_KEY /path/to/actions-runner-linux-x64.tar.gz" >&2
  echo "pipe a fresh organization runner registration token on stdin" >&2
  exit 2
}

[ "$#" -eq 2 ] || usage
KEY="$1"
TARBALL="$2"

for command in python3 sha256sum tar sudo systemctl; do
  command -v "$command" >/dev/null 2>&1 || {
    echo "missing required command: $command" >&2
    exit 1
  }
done

read_value() {
  python3 - "$INVENTORY" "$KEY" "$1" <<'PY'
import json
import sys

path, key, field = sys.argv[1:]
with open(path, encoding="utf-8") as handle:
    data = json.load(handle)
try:
    value = data["runners"][key][field]
except KeyError as exc:
    raise SystemExit(f"missing inventory value: {exc}")
if isinstance(value, bool):
    print("true" if value else "false")
elif isinstance(value, list):
    print(",".join(str(item) for item in value))
else:
    print(value)
PY
}

read_root_value() {
  python3 - "$INVENTORY" "$1" "$2" <<'PY'
import json
import sys

path, section, field = sys.argv[1:]
with open(path, encoding="utf-8") as handle:
    data = json.load(handle)
try:
    print(data[section][field])
except KeyError as exc:
    raise SystemExit(f"missing inventory value: {exc}")
PY
}

SCOPE="$(read_value scope)"
TARGET="$(read_value target)"
RUNNER_NAME="$(read_value runner_name)"
INSTALL_DIR="$(read_value install_dir)"
WORK_DIR="$(read_value work_dir)"
RUNNER_USER="$(read_value os_user)"
RUNNER_HOME="$(read_value home_dir)"
SERVICE_UNIT="$(read_value service_unit)"
LABELS="$(read_value labels)"
DEFAULT_LABELS="$(read_value default_labels)"
MEMORY_HIGH="$(read_value memory_high)"
MEMORY_MAX="$(read_value memory_max)"
MEMORY_SWAP_MAX="$(read_value memory_swap_max)"
EXPECTED_SHA256="$(read_root_value runner_package linux_x64_sha256)"

[ "$SCOPE" = "org" ] || {
  echo "unsupported runner scope: $SCOPE" >&2
  exit 1
}
[ -f "$TARBALL" ] || {
  echo "missing runner archive: $TARBALL" >&2
  exit 1
}

ACTUAL_SHA256="$(sha256sum "$TARBALL" | awk '{print $1}')"
[ "$ACTUAL_SHA256" = "$EXPECTED_SHA256" ] || {
  echo "runner archive digest mismatch" >&2
  echo "expected: $EXPECTED_SHA256" >&2
  echo "actual:   $ACTUAL_SHA256" >&2
  exit 1
}

[ ! -e "$INSTALL_DIR" ] || {
  echo "refusing to reuse existing install directory: $INSTALL_DIR" >&2
  exit 1
}

created_user=0
registered=0
service_installed=0
token_file=""

cleanup_token_file() {
  if [ -n "$token_file" ] && [ -e "$token_file" ]; then
    sudo rm -f -- "$token_file" 2>/dev/null || rm -f -- "$token_file" 2>/dev/null || true
  fi
}

write_token_file() {
  local token="$1"
  token_file="$(mktemp)"
  printf 'export ACTIONS_RUNNER_INPUT_TOKEN=%q\n' "$token" > "$token_file"
  sudo chown "$RUNNER_USER:$RUNNER_USER" "$token_file"
  sudo chmod 600 "$token_file"
}

run_config() {
  sudo -u "$RUNNER_USER" -H bash -c '
    set -euo pipefail
    umask 077
    source "$1"
    shift
    cd "$1"
    shift
    exec ./config.sh "$@"
  ' bash "$token_file" "$INSTALL_DIR" "$@"
}

rollback() {
  local rc="$1"
  trap - ERR INT TERM
  cleanup_token_file

  if [ "$service_installed" -eq 1 ]; then
    sudo systemctl disable --now "$SERVICE_UNIT" >/dev/null 2>&1 || true
    if [ -x "$INSTALL_DIR/svc.sh" ]; then
      (cd "$INSTALL_DIR" && sudo ./svc.sh uninstall) >/dev/null 2>&1 || true
    fi
  fi

  if [ "$registered" -eq 1 ] && command -v gh >/dev/null 2>&1; then
    local remove_token=""
    remove_token="$(gh api -X POST "orgs/$TARGET/actions/runners/remove-token" --jq .token 2>/dev/null || true)"
    if [ -n "$remove_token" ]; then
      write_token_file "$remove_token"
      run_config remove >/dev/null 2>&1 || true
      cleanup_token_file
      registered=0
    fi
  fi

  if [ "$registered" -eq 0 ]; then
    sudo rm -rf -- "$INSTALL_DIR" 2>/dev/null || true
    if [ "$created_user" -eq 1 ]; then
      sudo userdel -r "$RUNNER_USER" >/dev/null 2>&1 || true
      sudo groupdel "$RUNNER_USER" >/dev/null 2>&1 || true
    fi
  else
    echo "runner registration remains; remove it from GitHub before deleting $INSTALL_DIR" >&2
  fi

  exit "$rc"
}

trap 'rollback $?' ERR
trap 'rollback 130' INT TERM
trap cleanup_token_file EXIT

if id "$RUNNER_USER" >/dev/null 2>&1; then
  [ "$(id -u "$RUNNER_USER")" -ne 0 ] || {
    echo "runner user must not be root" >&2
    rollback 1
  }
else
  sudo useradd \
    --system \
    --user-group \
    --create-home \
    --home-dir "$RUNNER_HOME" \
    --shell /usr/sbin/nologin \
    "$RUNNER_USER"
  created_user=1
fi

for group in $(id -nG "$RUNNER_USER"); do
  case "$group" in
    sudo|wheel|docker|lxd|libvirt|disk)
      echo "refusing privileged runner user $RUNNER_USER: member of $group" >&2
      rollback 1
      ;;
  esac
done

sudo install -d -m 0700 -o "$RUNNER_USER" -g "$RUNNER_USER" "$RUNNER_HOME"
sudo install -d -m 0700 -o "$RUNNER_USER" -g "$RUNNER_USER" "$INSTALL_DIR"
sudo tar xzf "$TARBALL" -C "$INSTALL_DIR"
sudo chown -R "$RUNNER_USER:$RUNNER_USER" "$INSTALL_DIR"

[ -x "$INSTALL_DIR/config.sh" ] || {
  echo "runner archive is missing config.sh" >&2
  rollback 1
}

IFS= read -r TOKEN
[ -n "$TOKEN" ] || {
  echo "no registration token received on stdin" >&2
  rollback 1
}
write_token_file "$TOKEN"
unset TOKEN

config_args=(
  --url "https://github.com/$TARGET"
  --name "$RUNNER_NAME"
  --unattended
  --work "$WORK_DIR"
  --labels "$LABELS"
)
if [ "$DEFAULT_LABELS" = "false" ]; then
  config_args+=(--no-default-labels)
fi

run_config "${config_args[@]}"
registered=1
cleanup_token_file
token_file=""

(cd "$INSTALL_DIR" && sudo ./svc.sh install "$RUNNER_USER")
service_installed=1

DROPIN_DIR="/etc/systemd/system/$SERVICE_UNIT.d"
sudo install -d -m 0755 "$DROPIN_DIR"
printf '%s\n' \
  '[Service]' \
  'Slice=actions-runners.slice' \
  'Restart=always' \
  'RestartSec=10' \
  'PrivateTmp=yes' \
  'ProtectHome=yes' \
  'NoNewPrivileges=yes' \
  'RestrictSUIDSGID=yes' \
  'UMask=0077' \
  "MemoryHigh=$MEMORY_HIGH" \
  "MemoryMax=$MEMORY_MAX" \
  "MemorySwapMax=$MEMORY_SWAP_MAX" \
  | sudo tee "$DROPIN_DIR/20-prisma-light.conf" >/dev/null

sudo systemctl daemon-reload
sudo systemctl enable --now "$SERVICE_UNIT"

trap - ERR INT TERM
trap cleanup_token_file EXIT

echo "installed $RUNNER_NAME for $TARGET"
echo "service: $SERVICE_UNIT"
echo "label: $LABELS (default labels disabled)"
