#!/usr/bin/env bash
# 登录卡死回归场景一键运行：离线（拒绝/超时）+ 假服务端在线场景。
# 用法: run_scenarios.sh <harness_binary> [label]
# 依赖: QT_QPA_PLATFORM=offscreen（脚本内已设）；黑洞地址可用 BLACKHOLE_HOST 覆盖。
set -u
BIN=${1:?usage: run_scenarios.sh <harness_binary> [label]}
LABEL=${2:-run}
DIR=$(cd "$(dirname "$0")" && pwd)
export QT_QPA_PLATFORM=offscreen
export XDG_CONFIG_HOME=/tmp/evcp-harness-xdg
mkdir -p "$XDG_CONFIG_HOME"
FAKE_PORT=18891
BLACKHOLE_HOST=${BLACKHOLE_HOST:-192.168.44.222}
PASS=0; FAIL=0

run() { # name args...
  local name=$1; shift
  echo "===== [$LABEL] $name ====="
  timeout 40 "$BIN" "$@" 2>&1
  local rc=$?
  if [ $rc -eq 0 ]; then PASS=$((PASS+1)); echo "----- $name: PASS"
  elif [ $rc -eq 124 ]; then FAIL=$((FAIL+1)); echo "----- $name: HARD-FREEZE (killed by timeout)"
  else FAIL=$((FAIL+1)); echo "----- $name: FAIL rc=$rc"; fi
}

start_fake() {
  python3 "$DIR/fakeserver.py" $FAKE_PORT "$1" >/tmp/fakeserver.log 2>&1 &
  FAKE_PID=$!
  sleep 0.5
}
stop_fake() { kill "$FAKE_PID" 2>/dev/null; wait "$FAKE_PID" 2>/dev/null; }

# ---- 离线场景（服务端不在线） ----
run refused --scenario refused --host 127.0.0.1 --port 45999 --expect-modal 无法连接服务器
EVCP_CONNECT_TIMEOUT_MS=1500 run connect_timeout --scenario timeout --host "$BLACKHOLE_HOST" --port 8888 --expect-modal 无法连接服务器 --deadline-ms 12000
run smscode_refused --scenario smscode_refused --host 127.0.0.1 --port 45999 --expect-modal 无法连接服务器
run cancel_connect --scenario cancel_connect --host "$BLACKHOLE_HOST" --port 8888 --deadline-ms 8000

# ---- 假服务端在线场景 ----
start_fake ok
run login_ok --scenario login --host 127.0.0.1 --port $FAKE_PORT
run smscode_login --scenario smscode_login --host 127.0.0.1 --port $FAKE_PORT
stop_fake

start_fake newuser
run login_newuser_setpwd --scenario login --host 127.0.0.1 --port $FAKE_PORT --phone 13800002222 --dialog-action set
stop_fake

for c in err1001 err1002 err1005; do
  start_fake "$c"
  case $c in
    err1001) EXP=密码或验证码错误;;
    err1002) EXP=冻结;;
    err1005) EXP=注销;;
  esac
  run "login_$c" --scenario login_err --host 127.0.0.1 --port $FAKE_PORT --expect-modal "$EXP"
  stop_fake
done

# ---- 断线重连链路：登录后杀服务端 → 横幅出现；重启服务端 → 自动重连重登 → 横幅消失 ----
echo "===== [$LABEL] reconnect ====="
start_fake ok
timeout 45 "$BIN" --scenario reconnect --host 127.0.0.1 --port $FAKE_PORT --deadline-ms 35000 &
HPID=$!
sleep 4
stop_fake           # 服务端宕机
sleep 3
start_fake ok       # 服务端恢复
wait "$HPID"; rc=$?
stop_fake
if [ $rc -eq 0 ]; then PASS=$((PASS+1)); echo "----- reconnect: PASS"
elif [ $rc -eq 124 ]; then FAIL=$((FAIL+1)); echo "----- reconnect: HARD-FREEZE (killed by timeout)"
else FAIL=$((FAIL+1)); echo "----- reconnect: FAIL rc=$rc"; fi

echo "===== [$LABEL] summary: PASS=$PASS FAIL=$FAIL ====="
[ "$FAIL" -eq 0 ]
