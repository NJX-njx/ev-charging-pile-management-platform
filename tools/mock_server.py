#!/usr/bin/env python3
"""开发用模拟服务端。

按 docs/protocol.md 的约定接收并响应 JSON 消息（TCP，\\n 分包，UTF-8），
返回内存假数据，供 Qt 管理端/用户端在真实服务端完成前联调界面。

用法：
    python3 tools/mock_server.py [端口]     # 默认 8888

仅用于开发调试，不属于交付模块。
"""

import datetime
import json
import socketserver
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8888

STATIONS = [
    {"stationId": 1, "name": "星海广场站", "address": "沙河口区星海广场",
     "lng": 121.594, "lat": 38.881, "pileTotal": 3, "onlineRate": 1.0},
    {"stationId": 2, "name": "高新园区站", "address": "黄浦路100号",
     "lng": 121.520, "lat": 38.860, "pileTotal": 3, "onlineRate": 0.67},
]

PILES = [
    {"pileId": 101, "code": "P-0101", "stationId": 1, "stationName": "星海广场站",
     "type": "fast", "powerKw": 60, "status": "idle", "chargeCount": 152, "chargeMinutes": 6040},
    {"pileId": 102, "code": "P-0102", "stationId": 1, "stationName": "星海广场站",
     "type": "slow", "powerKw": 7, "status": "in_use", "chargeCount": 98, "chargeMinutes": 8820},
    {"pileId": 103, "code": "P-0103", "stationId": 1, "stationName": "星海广场站",
     "type": "fast", "powerKw": 120, "status": "idle", "chargeCount": 76, "chargeMinutes": 2280},
    {"pileId": 201, "code": "P-0201", "stationId": 2, "stationName": "高新园区站",
     "type": "fast", "powerKw": 60, "status": "fault", "chargeCount": 210, "chargeMinutes": 9450},
    {"pileId": 202, "code": "P-0202", "stationId": 2, "stationName": "高新园区站",
     "type": "slow", "powerKw": 7, "status": "idle", "chargeCount": 45, "chargeMinutes": 4050},
    {"pileId": 203, "code": "P-0203", "stationId": 2, "stationName": "高新园区站",
     "type": "slow", "powerKw": 7, "status": "idle", "chargeCount": 12, "chargeMinutes": 1080},
]

USERS = [
    {"userId": 1, "phone": "13800001234", "nickname": "用户1234",
     "balance": 86.5, "regTime": "2026-09-01 10:20:30", "status": "normal"},
    {"userId": 2, "phone": "13911115678", "nickname": "用户5678",
     "balance": 12.0, "regTime": "2026-09-02 14:05:11", "status": "frozen"},
    {"userId": 3, "phone": "13722229999", "nickname": "用户9999",
     "balance": 230.8, "regTime": "2026-09-03 09:41:52", "status": "normal"},
]

NEXT_STATION_ID = 3


def ok(data):
    return 0, "ok", data


def err(code, msg):
    return code, msg, None


def handle(req):
    """返回 (code, msg, data)。"""
    t = req.get("type")
    p = req.get("payload") or {}

    if t == "ping":
        return ok({})

    if t == "admin_login":
        if p.get("username") == "admin" and p.get("password") == "123456":
            return ok({"adminId": 1, "username": "admin"})
        return err(1001, "用户名或密码错误")

    if t == "revenue_summary":
        return ok({"today": 356.5, "month": 8230.0, "total": 45210.8})

    if t == "revenue_trend":
        r = p.get("range")
        if r not in (7, 30):
            return err(2001, "range 必须为 7 或 30")
        today = datetime.date.today()
        points = []
        for i in range(r):
            day = today - datetime.timedelta(days=r - 1 - i)
            amount = round(100 + (i * 37 % 250) + (i % 3) * 12.5, 2)
            points.append({"date": day.isoformat(), "amount": amount})
        return ok({"points": points})

    if t == "pile_status_overview":
        data = {"idle": 0, "inUse": 0, "fault": 0}
        key = {"idle": "idle", "in_use": "inUse", "fault": "fault"}
        for pile in PILES:
            data[key[pile["status"]]] += 1
        return ok(data)

    if t == "pile_list":
        sid = p.get("stationId", 0)
        piles = [x for x in PILES if sid == 0 or x["stationId"] == sid]
        return ok({"piles": piles})

    if t == "pile_restart":
        pid = p.get("pileId")
        for pile in PILES:
            if pile["pileId"] == pid:
                if pile["status"] != "fault":
                    return err(3002, "电桩不是故障状态，不能重启")
                pile["status"] = "idle"
                return ok({"pileId": pid, "status": "idle"})
        return err(2002, "电桩不存在")

    if t == "station_list":
        return ok({"stations": STATIONS})

    if t == "station_add":
        global NEXT_STATION_ID
        name = (p.get("name") or "").strip()
        count = p.get("pileCount")
        if not name or not isinstance(count, int) or count <= 0:
            return err(2001, "参数缺失或非法")
        station = {"stationId": NEXT_STATION_ID, "name": name,
                   "address": p.get("address", ""), "lng": p.get("lng", 0),
                   "lat": p.get("lat", 0), "pileTotal": count, "onlineRate": 1.0}
        NEXT_STATION_ID += 1
        STATIONS.append(station)
        return ok({"stationId": station["stationId"]})

    if t == "user_list":
        kw = p.get("phoneKeyword", "")
        users = [u for u in USERS if kw in u["phone"]]
        return ok({"users": users})

    if t == "user_set_status":
        uid = p.get("userId")
        status = p.get("status")
        if status not in ("normal", "frozen"):
            return err(2001, "status 必须为 normal 或 frozen")
        for u in USERS:
            if u["userId"] == uid:
                u["status"] = status
                return ok({"userId": uid, "status": status})
        return err(2002, "用户不存在")

    return err(3001, "未知消息类型: %s" % t)


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        while True:
            line = self.rfile.readline()
            if not line:
                return
            line = line.strip()
            if not line:
                continue
            try:
                req = json.loads(line.decode("utf-8"))
            except (ValueError, UnicodeDecodeError):
                resp = {"seq": -1, "type": "", "code": 3001,
                        "msg": "消息无法解析", "data": None}
            else:
                code, msg, data = handle(req)
                resp = {"seq": req.get("seq", -1), "type": req.get("type", ""),
                        "code": code, "msg": msg, "data": data}
            self.wfile.write((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    with Server(("0.0.0.0", PORT), Handler) as srv:
        print("mock server listening on %d" % PORT, flush=True)
        srv.serve_forever()
