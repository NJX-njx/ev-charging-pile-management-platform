#!/usr/bin/env bash
# 协议 v2.2 适配场景一键运行：每个场景用对应 CASE 启动状态化假服务端后跑 harness。
# 用法: run_scenarios.sh <harness_binary> [label]
# 依赖: offscreen 平台 + WebEngine no-sandbox（脚本内已设）。
set -u
BIN=${1:?usage: run_scenarios.sh <harness_binary> [label]}
LABEL=${2:-run}
DIR=$(cd "$(dirname "$0")" && pwd)
export QT_QPA_PLATFORM=offscreen
export QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu"
export XDG_CONFIG_HOME=/tmp/evcp-v22-harness-xdg
mkdir -p "$XDG_CONFIG_HOME"
FAKE_PORT=18892
PASS=0; FAIL=0

run() { # name case args...
  local name=$1 case=$2; shift 2
  local reqlog=/tmp/v22_reqlog_${name}.jsonl
  : > "$reqlog"
  echo "===== [$LABEL] $name (case=$case) ====="
  python3 "$DIR/fakeserver.py" $FAKE_PORT "$case" "$reqlog" >/tmp/v22_fakeserver_${name}.log 2>&1 &
  local fpid=$!
  sleep 0.5
  timeout 60 "$BIN" --scenario "$name" --host 127.0.0.1 --port $FAKE_PORT \
      --reqlog "$reqlog" "$@" 2>&1
  local rc=$?
  kill "$fpid" 2>/dev/null; wait "$fpid" 2>/dev/null
  if [ $rc -eq 0 ]; then PASS=$((PASS+1)); echo "----- $name: PASS"
  elif [ $rc -eq 124 ]; then FAIL=$((FAIL+1)); echo "----- $name: HARD-TIMEOUT (killed)"
  else FAIL=$((FAIL+1)); echo "----- $name: FAIL rc=$rc"; fi
}

run orders_list multi
run order_flow flow
run cost_tick multi
run profile_edit multi
run region_select multi
run reserve_then_card empty
run pwd_toggle multi
run ime_hints multi
run nav_url multi --deadline-ms 45000

echo "===== [$LABEL] summary: PASS=$PASS FAIL=$FAIL ====="
[ "$FAIL" -eq 0 ]
