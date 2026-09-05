#!/usr/bin/env python3
"""v22 harness 用的状态化假服务端（协议 v2.2 子集）。

用法：fakeserver.py PORT CASE [REQLOG]
CASE：
  multi  预置 3 个未完成订单：reserved / charging（开始于约 65 秒前）/ pending_payment
  flow   预置 3 个未完成订单：reserved / charging / reserved（状态流转场景）
  empty  无未完成订单（预约场景）
REQLOG：每收到一条请求，追加一行 JSON（含 type 与 payload）到该文件，供 harness 断言报文。
"""

import json
import socketserver
import sys
from datetime import datetime, timedelta, timezone

PORT = int(sys.argv[1])
CASE = sys.argv[2] if len(sys.argv) > 2 else "multi"
REQLOG = sys.argv[3] if len(sys.argv) > 3 else None

CST = timezone(timedelta(hours=8))

NICKNAME = ["测试用户"]
BALANCE = [100.0]
ORDERS = {}
NEXT_OID = [10010]
PILES = {101: "idle", 102: "in_use"}
PILE_META = {101: ("P-1001", "fast", 60.0), 102: ("P-1002", "slow", 7.0)}


def iso(dt):
    return dt.isoformat(timespec="seconds")


def now():
    return datetime.now(CST)


def user():
    return {"userId": 1, "phone": "13800001111", "nickname": NICKNAME[0],
            "balance": BALANCE[0], "regTime": "2026-09-01T10:20:30+08:00",
            "status": "normal", "hasPassword": True, "avatar": None}


def make_order(oid, pile_id, code, status, reserved_at, start=None, end=None,
               energy=None, amount=None, power=60.0, price=1.20):
    return {"orderId": oid, "stationId": 1, "stationName": "星海广场站",
            "pileId": pile_id, "pileCode": code, "powerKw": power, "status": status,
            "reservedAt": iso(reserved_at),
            "startTime": iso(start) if start else None,
            "endTime": iso(end) if end else None,
            "settledAt": None,
            "energyKwh": energy, "unitPrice": price, "amount": amount}


def seed():
    t = now()
    if CASE == "multi":
        items = [
            make_order(10001, 101, "P-0001", "reserved", t - timedelta(seconds=600)),
            make_order(10002, 102, "P-0002", "charging", t - timedelta(seconds=900),
                       start=t - timedelta(seconds=65)),
            make_order(10003, 103, "P-0003", "pending_payment", t - timedelta(seconds=3600),
                       start=t - timedelta(seconds=3500), end=t - timedelta(seconds=1800),
                       energy=3.500, amount=4.20, power=7.0),
        ]
    elif CASE == "flow":
        items = [
            make_order(10001, 101, "P-0001", "reserved", t - timedelta(seconds=120)),
            make_order(10002, 102, "P-0002", "charging", t - timedelta(seconds=300),
                       start=t - timedelta(seconds=30)),
            make_order(10003, 103, "P-0003", "reserved", t - timedelta(seconds=60)),
        ]
    else:
        return
    for o in items:
        ORDERS[o["orderId"]] = o


def active_orders():
    items = [o for o in ORDERS.values()
             if o["status"] in ("reserved", "charging", "pending_payment")]
    items.sort(key=lambda o: (o["reservedAt"], o["orderId"]), reverse=True)
    return items


def handle(req):
    t = req.get("type", "")
    p = req.get("payload") or {}
    if t == "ping":
        return 0, "ok", {}
    if t == "user_login":
        return 0, "ok", {"isNew": False, "user": user()}
    if t == "user_code_request":
        return 0, "ok", {"code": "483920", "validSec": 300}
    if t == "user_profile_get":
        return 0, "ok", {"user": user()}
    if t == "user_profile_update":
        nick = p.get("nickname")
        if nick is not None:
            NICKNAME[0] = nick
        return 0, "ok", {"user": user()}
    if t == "active_order_get":
        return 0, "ok", {"orders": active_orders()}
    if t == "charge_reserve":
        pile_id = int(p.get("pileId", 0))
        if BALANCE[0] <= 0:
            return 3004, "balance must be positive", None
        if pile_id not in PILES:
            return 2002, "pile not found", None
        if PILES[pile_id] != "idle":
            return 3003, "pile not idle", None
        PILES[pile_id] = "in_use"
        code, _typ, power = PILE_META[pile_id]
        o = make_order(NEXT_OID[0], pile_id, code, "reserved", now(), power=power)
        NEXT_OID[0] += 1
        ORDERS[o["orderId"]] = o
        return 0, "ok", {"order": o}
    if t == "charge_start":
        o = ORDERS.get(int(p.get("orderId", 0)))
        if not o:
            return 2002, "order not found", None
        if o["status"] != "reserved":
            return 3002, "order status must be reserved", None
        o["status"] = "charging"
        o["startTime"] = iso(now())
        return 0, "ok", {"order": o}
    if t == "charge_stop":
        o = ORDERS.get(int(p.get("orderId", 0)))
        if not o:
            return 2002, "order not found", None
        if o["status"] != "charging":
            return 3002, "order status must be charging", None
        start = datetime.fromisoformat(o["startTime"])
        hours = max((now() - start).total_seconds(), 1.0) / 3600.0
        energy = round(o["powerKw"] * hours, 3)
        o["status"] = "pending_payment"
        o["endTime"] = iso(now())
        o["energyKwh"] = energy
        o["amount"] = round(energy * o["unitPrice"], 2)
        return 0, "ok", {"order": o}
    if t == "charge_settle":
        o = ORDERS.get(int(p.get("orderId", 0)))
        if not o:
            return 2002, "order not found", None
        if o["status"] != "pending_payment":
            return 3002, "order status must be pending_payment", None
        if BALANCE[0] < o["amount"]:
            return 3004, "insufficient balance", None
        BALANCE[0] = round(BALANCE[0] - o["amount"], 2)
        o["status"] = "completed"
        o["settledAt"] = iso(now())
        return 0, "ok", {"order": o, "balance": BALANCE[0]}
    if t == "charge_cancel":
        o = ORDERS.get(int(p.get("orderId", 0)))
        if not o:
            return 2002, "order not found", None
        if o["status"] != "reserved":
            return 3002, "order status must be reserved", None
        o["status"] = "cancelled"
        if o["pileId"] in PILES:
            PILES[o["pileId"]] = "idle"
        return 0, "ok", {"orderId": o["orderId"], "status": "cancelled",
                         "pileId": o["pileId"], "pileStatus": "idle"}
    if t == "nearby_station_list":
        return 0, "ok", {"stations": [
            {"stationId": 1, "name": "星海广场站", "address": "沙河口区星海广场",
             "lng": 121.596, "lat": 38.883, "pricePerKwh": 1.20,
             "pileTotal": 4, "pileIdle": 2, "onlineRate": 0.9, "distanceKm": 0.3},
            {"stationId": 2, "name": "青泥洼桥站", "address": "中山区青泥洼桥",
             "lng": 121.633, "lat": 38.917, "pricePerKwh": 1.50,
             "pileTotal": 6, "pileIdle": 3, "onlineRate": 0.95, "distanceKm": 4.2},
        ]}
    if t == "station_detail":
        return 0, "ok", {
            "station": {"stationId": 1, "name": "星海广场站", "address": "沙河口区星海广场",
                        "lng": 121.596, "lat": 38.883, "pricePerKwh": 1.20,
                        "pileTotal": 4, "pileIdle": 2, "onlineRate": 0.9},
            "piles": [
                {"pileId": pid, "code": PILE_META[pid][0], "stationId": 1,
                 "stationName": "星海广场站", "type": PILE_META[pid][1],
                 "powerKw": PILE_META[pid][2], "status": PILES[pid],
                 "chargeCount": 10, "chargeMinutes": 100}
                for pid in (101, 102)
            ]}
    if t == "user_order_list":
        return 0, "ok", {"orders": [], "total": 0, "page": 1, "pageSize": 10}
    return 3001, "unsupported type", None


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        for line in self.rfile:
            line = line.strip()
            if not line:
                continue
            try:
                req = json.loads(line.decode("utf-8"))
            except (ValueError, UnicodeDecodeError):
                continue
            if REQLOG:
                with open(REQLOG, "a", encoding="utf-8") as f:
                    f.write(json.dumps({"type": req.get("type", ""),
                                        "payload": req.get("payload") or {}},
                                       ensure_ascii=False) + "\n")
            code, msg, data = handle(req)
            resp = {"seq": req.get("seq", -1), "type": req.get("type", ""),
                    "code": code, "msg": msg, "data": data}
            self.wfile.write((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    seed()
    with Server(("127.0.0.1", PORT), Handler) as srv:
        print("fakeserver listening on %d case=%s" % (PORT, CASE), flush=True)
        srv.serve_forever()
