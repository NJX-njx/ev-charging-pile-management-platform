#!/usr/bin/env bash
# p7 改版场景一键运行：每个场景用全新状态的 mock_server_v23.py 驱动 harness。
# 用法: run_scenarios.sh <harness_binary>
# 依赖: offscreen 平台（脚本内已设）。
set -u
BIN=${1:?usage: run_scenarios.sh <harness_binary>}
DIR=$(cd "$(dirname "$0")" && pwd)
MOCK="$DIR/../../../tools/mock_server_v23.py"
export QT_QPA_PLATFORM=offscreen
export XDG_CONFIG_HOME=/tmp/evcp-p7-harness-xdg
mkdir -p "$XDG_CONFIG_HOME"
PORT=18893
PASS=0; FAIL=0

run() {
  local name=$1
  echo "===== $name ====="
  # 防止残留 mock 占用端口（状态化 mock 会污染后续场景）
  pkill -f "mock_server_v23.py $PORT" 2>/dev/null
  sleep 0.3
  python3 "$MOCK" $PORT >"/tmp/p7_mock_${name}.log" 2>&1 &
  local mpid=$!
  # 确认 mock 正常监听（端口被占用时进程会立即退出）
  sleep 0.5
  if ! kill -0 "$mpid" 2>/dev/null; then
    echo "----- $name: MOCK FAILED TO START (see /tmp/p7_mock_${name}.log)"
    FAIL=$((FAIL+1)); return
  fi
  timeout 120 "$BIN" --scenario "$name" --host 127.0.0.1 --port $PORT 2>&1
  local rc=$?
  kill "$mpid" 2>/dev/null; wait "$mpid" 2>/dev/null
  if [ $rc -eq 0 ]; then PASS=$((PASS+1)); echo "----- $name: PASS"
  elif [ $rc -eq 124 ]; then FAIL=$((FAIL+1)); echo "----- $name: HARD-TIMEOUT (killed)"
  else FAIL=$((FAIL+1)); echo "----- $name: FAIL rc=$rc"; fi
}

run nav
run merged
run stationops
run pileops
run filtersort

echo "===== summary: PASS=$PASS FAIL=$FAIL ====="
[ "$FAIL" -eq 0 ]
