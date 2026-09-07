#!/usr/bin/env bash
# v2.5 协议验证场景一键运行：validate 离线；addstation/importflow 每场景用全新状态的
# ../mock_server_v25.py（端口 8893）驱动 harness。截图输出到 shotdir（默认 /tmp）。
# 用法: run_scenarios.sh <harness_binary> [shotdir]
# 依赖: offscreen 平台（脚本内已设）。进程清理按 PID（禁用 pkill -f）。
set -u
BIN=${1:?usage: run_scenarios.sh <harness_binary> [shotdir]}
SHOTDIR=${2:-/tmp}
DIR=$(cd "$(dirname "$0")" && pwd)
MOCK="$DIR/../mock_server_v25.py"
export QT_QPA_PLATFORM=offscreen
export XDG_CONFIG_HOME=/tmp/evcp-v25-harness-xdg
mkdir -p "$XDG_CONFIG_HOME" "$SHOTDIR"
PORT=8893
PASS=0; FAIL=0

kill_port() {
  local pid
  for pid in $(ss -tlnp 2>/dev/null | grep ":$1 " | grep -oP 'pid=\K[0-9]+' | sort -u); do
    kill "$pid" 2>/dev/null
  done
}

run_mock_scenario() {
  local name=$1
  echo "===== $name ====="
  kill_port $PORT
  sleep 0.3
  python3 "$MOCK" $PORT >"/tmp/v25_mock_${name}.log" 2>&1 &
  local mpid=$!
  sleep 0.5
  if ! kill -0 "$mpid" 2>/dev/null; then
    echo "----- $name: MOCK FAILED TO START (see /tmp/v25_mock_${name}.log)"
    FAIL=$((FAIL+1)); return
  fi
  timeout 120 "$BIN" --scenario "$name" --host 127.0.0.1 --port $PORT --shotdir "$SHOTDIR" 2>&1
  local rc=$?
  kill "$mpid" 2>/dev/null; wait "$mpid" 2>/dev/null
  if [ $rc -eq 0 ]; then PASS=$((PASS+1)); echo "----- $name: PASS"
  elif [ $rc -eq 124 ]; then FAIL=$((FAIL+1)); echo "----- $name: HARD-TIMEOUT (killed)"
  else FAIL=$((FAIL+1)); echo "----- $name: FAIL rc=$rc"; fi
}

echo "===== validate ====="
timeout 60 "$BIN" --scenario validate --shotdir "$SHOTDIR" 2>&1
rc=$?
if [ $rc -eq 0 ]; then PASS=$((PASS+1)); echo "----- validate: PASS"
else FAIL=$((FAIL+1)); echo "----- validate: FAIL rc=$rc"; fi

run_mock_scenario addstation
run_mock_scenario importflow

echo "===== summary: PASS=$PASS FAIL=$FAIL ====="
[ "$FAIL" -eq 0 ]
