#!/usr/bin/env bash
# v2.4 协议验证场景一键运行：直连 v2.4 真实服务端（harness 每场景自建站点/用户/订单，
# 联调库会累积自测数据，属预期）。截图输出到 shotdir（默认 /tmp）。
# 用法: run_scenarios.sh <harness_binary> [host] [port] [shotdir]
# 依赖: offscreen 平台（脚本内已设）。
set -u
BIN=${1:?usage: run_scenarios.sh <harness_binary> [host] [port] [shotdir]}
HOST=${2:-127.0.0.1}
PORT=${3:-8888}
SHOTDIR=${4:-/tmp}
export QT_QPA_PLATFORM=offscreen
export XDG_CONFIG_HOME=/tmp/evcp-v24-harness-xdg
mkdir -p "$XDG_CONFIG_HOME" "$SHOTDIR"
PASS=0; FAIL=0

run() {
  local name=$1
  echo "===== $name ====="
  timeout 180 "$BIN" --scenario "$name" --host "$HOST" --port "$PORT" --shotdir "$SHOTDIR" 2>&1
  local rc=$?
  if [ $rc -eq 0 ]; then PASS=$((PASS+1)); echo "----- $name: PASS"
  elif [ $rc -eq 124 ]; then FAIL=$((FAIL+1)); echo "----- $name: HARD-TIMEOUT (killed)"
  else FAIL=$((FAIL+1)); echo "----- $name: FAIL rc=$rc"; fi
}

run pileocc
run useredit
run orderops

echo "===== summary: PASS=$PASS FAIL=$FAIL ====="
[ "$FAIL" -eq 0 ]
