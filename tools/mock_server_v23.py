#!/usr/bin/env python3
"""协议 v2.3 有状态假服务端（供 admin 模块 offscreen 联调验证）。

实现 admin 端用到的消息子集：
ping / admin_login / admin_password_update / admin_list / admin_add / admin_delete /
revenue_summary / revenue_trend / pile_status_overview /
pile_list / pile_restart / pile_disable / pile_active_order / pile_add / pile_update / pile_delete /
station_list / station_add / station_update / station_delete / station_detail /
user_list / user_set_status / user_add / user_update / user_reset_password / user_delete /
admin_order_list / admin_order_detail

用法：python3 mock_server_v23.py [端口]   # 默认 18888
"""

import datetime
import json
import socketserver
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18888

TZ = datetime.timezone(datetime.timedelta(hours=8))


def now_iso():
    return datetime.datetime.now(TZ).isoformat(timespec="seconds")


STATE = {
    "admins": [{"adminId": 1, "username": "admin", "password": "123456"}],
    "nextAdminId": 2,
    "stations": [
        {"stationId": 1, "name": "良乡大学城北站", "address": "房山区良乡高教园区",
         "lng": 116.123, "lat": 39.732, "pricePerKwh": 1.20, "deleted": False},
    ],
    "nextStationId": 2,
    "piles": [
        {"pileId": 101, "code": "P-0101", "stationId": 1, "type": "fast", "powerKw": 60.0,
         "status": "idle", "chargeCount": 10, "chargeMinutes": 600, "deleted": False},
        {"pileId": 102, "code": "P-0102", "stationId": 1, "type": "slow", "powerKw": 7.0,
         "status": "in_use", "chargeCount": 5, "chargeMinutes": 300, "deleted": False},
        {"pileId": 103, "code": "P-0103", "stationId": 1, "type": "fast", "powerKw": 120.0,
         "status": "fault", "chargeCount": 3, "chargeMinutes": 120, "deleted": False},
    ],
    "nextPileId": 104,
    "users": [
        {"userId": 1, "phone": "13800001234", "nickname": "测试用户", "balance": 100.0,
         "regTime": "2026-09-01T10:00:00+08:00", "status": "normal", "hasPassword": True,
         "deleted": False},
    ],
    "orders": [
        # 102 号桩的占用订单（充电中）
        {"orderId": 10001, "userPhone": "13800001234", "stationId": 1, "pileId": 102,
         "status": "charging",
         "reservedAt": "2026-09-05T10:00:00+08:00",
         "startTime": "2026-09-05T10:02:00+08:00", "endTime": None, "settledAt": None,
         "energyKwh": None, "unitPrice": 1.20, "amount": None},
    ],
}


def station_summary(s):
    piles = [p for p in STATE["piles"] if p["stationId"] == s["stationId"] and not p["deleted"]]
    total = len(piles)
    idle = len([p for p in piles if p["status"] == "idle"])
    in_use = len([p for p in piles if p["status"] == "in_use"])
    rate = (idle + in_use) / total if total else 0.0
    out = {"stationId": s["stationId"], "name": s["name"], "address": s["address"],
           "lng": s["lng"], "lat": s["lat"], "pricePerKwh": s["pricePerKwh"],
           "pileTotal": total, "pileIdle": idle, "onlineRate": round(rate, 2)}
    if s.get("deleted"):
        out["deleted"] = True
    return out


def pile_obj(p):
    st = next((s for s in STATE["stations"] if s["stationId"] == p["stationId"]), None)
    out = {"pileId": p["pileId"], "code": p["code"], "stationId": p["stationId"],
           "stationName": st["name"] if st else "", "type": p["type"],
           "powerKw": p["powerKw"], "status": p["status"],
           "chargeCount": p["chargeCount"], "chargeMinutes": p["chargeMinutes"]}
    if p.get("deleted"):
        out["deleted"] = True
    return out


def order_obj(o):
    st = next((s for s in STATE["stations"] if s["stationId"] == o["stationId"]), None)
    p = next((x for x in STATE["piles"] if x["pileId"] == o["pileId"]), None)
    return {"orderId": o["orderId"], "userPhone": o["userPhone"],
            "stationId": o["stationId"], "stationName": st["name"] if st else "",
            "pileId": o["pileId"], "pileCode": p["code"] if p else "",
            "powerKw": p["powerKw"] if p else 0.0, "status": o["status"],
            "reservedAt": o["reservedAt"], "startTime": o["startTime"],
            "endTime": o["endTime"], "settledAt": o["settledAt"],
            "energyKwh": o["energyKwh"], "unitPrice": o["unitPrice"], "amount": o["amount"]}


def find_pile(pile_id):
    return next((p for p in STATE["piles"] if p["pileId"] == pile_id and not p["deleted"]), None)


def active_order_of(pile_id):
    return next((o for o in STATE["orders"]
                 if o["pileId"] == pile_id and o["status"] in ("reserved", "charging")), None)


def ok(data):
    return 0, "ok", data


def err(code, msg):
    return code, msg, None


def handle(req):
    t = req.get("type")
    p = req.get("payload") or {}

    if t == "ping":
        return ok({"serverTime": now_iso()})

    if t == "admin_login":
        a = next((x for x in STATE["admins"]
                  if x["username"] == p.get("username") and x["password"] == p.get("password")), None)
        if a:
            return ok({"adminId": a["adminId"], "username": a["username"]})
        return err(1001, "用户名或密码错误")

    if t == "admin_password_update":
        # 简化：会话视为 adminId=1（假服务端单连接单管理员）
        a = STATE["admins"][0]
        if a["password"] != p.get("oldPassword"):
            return err(1001, "原密码错误")
        np = p.get("newPassword") or ""
        if not (6 <= len(np) <= 20) or any(ch.isspace() for ch in np):
            return err(2001, "新密码须为 6 至 20 位且不含空白字符")
        a["password"] = np
        return ok({"adminId": a["adminId"]})

    if t == "admin_list":
        return ok({"admins": [{"adminId": a["adminId"], "username": a["username"]}
                              for a in STATE["admins"]]})

    if t == "admin_add":
        name = (p.get("username") or "").strip()
        pwd = p.get("password") or ""
        if not (1 <= len(name) <= 20):
            return err(2001, "用户名长度须为 1 至 20")
        if any(a["username"] == name for a in STATE["admins"]):
            return err(2001, "用户名已存在")
        if not (6 <= len(pwd) <= 20) or any(ch.isspace() for ch in pwd):
            return err(2001, "密码须为 6 至 20 位且不含空白字符")
        a = {"adminId": STATE["nextAdminId"], "username": name, "password": pwd}
        STATE["nextAdminId"] += 1
        STATE["admins"].append(a)
        return ok({"adminId": a["adminId"], "username": name})

    if t == "admin_delete":
        target = next((a for a in STATE["admins"] if a["adminId"] == p.get("adminId")), None)
        if not target:
            return err(2002, "管理员不存在")
        if target["adminId"] == STATE["admins"][0]["adminId"] and target["username"] == "admin":
            # 假服务端约定 admin 为当前登录账号
            return err(3002, "不能删除当前登录的本人账号")
        if len(STATE["admins"]) == 1:
            return err(3002, "不能删除最后一个管理员账号")
        STATE["admins"].remove(target)
        return ok({"adminId": target["adminId"], "deleted": True})

    if t == "revenue_summary":
        return ok({"today": 12.5, "month": 345.6, "total": 7890.1})
    if t == "revenue_trend":
        return ok({"points": [{"date": "2026-09-0%d" % d, "amount": 10.0 * d}
                              for d in range(1, 8)]})
    if t == "pile_status_overview":
        piles = [x for x in STATE["piles"] if not x["deleted"]]
        return ok({"total": len(piles),
                   "idle": len([x for x in piles if x["status"] == "idle"]),
                   "inUse": len([x for x in piles if x["status"] == "in_use"]),
                   "fault": len([x for x in piles if x["status"] == "fault"])})

    if t == "pile_list":
        include_deleted = bool(p.get("includeDeleted"))
        piles = [x for x in STATE["piles"] if include_deleted or not x["deleted"]]
        return ok({"piles": [pile_obj(x) for x in sorted(piles, key=lambda x: x["pileId"])]})

    if t == "pile_restart":
        pile = find_pile(p.get("pileId"))
        if not pile:
            return err(2002, "电桩不存在")
        if active_order_of(pile["pileId"]):
            return err(3002, "电桩正在使用中，无法重启")
        pile["status"] = "idle"
        return ok({"pileId": pile["pileId"], "status": "idle"})

    if t == "pile_disable":
        pile = find_pile(p.get("pileId"))
        if not pile:
            return err(2002, "电桩不存在")
        if pile["status"] != "idle" or active_order_of(pile["pileId"]):
            return err(3002, "仅空闲电桩可禁用")
        pile["status"] = "fault"
        return ok({"pileId": pile["pileId"], "status": "fault"})

    if t == "pile_active_order":
        pile = find_pile(p.get("pileId"))
        if not pile:
            return err(2002, "电桩不存在")
        order = active_order_of(pile["pileId"])
        return ok({"order": order_obj(order) if order else None})

    if t == "pile_add":
        code = (p.get("code") or "").strip()
        if not code or any(x["code"] == code for x in STATE["piles"]):
            return err(2001, "电桩编号为空或已存在")
        pile = {"pileId": STATE["nextPileId"], "code": code,
                "stationId": p.get("stationId"), "type": p.get("type", "fast"),
                "powerKw": p.get("powerKw", 60.0), "status": "idle",
                "chargeCount": 0, "chargeMinutes": 0, "deleted": False}
        STATE["nextPileId"] += 1
        STATE["piles"].append(pile)
        return ok({"pile": pile_obj(pile)})

    if t == "pile_update":
        pile = find_pile(p.get("pileId"))
        if not pile:
            return err(2002, "电桩不存在")
        if active_order_of(pile["pileId"]):
            return err(3002, "电桩正在使用中，无法修改")
        pile["type"] = p.get("type", pile["type"])
        pile["powerKw"] = p.get("powerKw", pile["powerKw"])
        return ok({"pile": pile_obj(pile)})

    if t == "pile_delete":
        pile = find_pile(p.get("pileId"))
        if not pile:
            return err(2002, "电桩不存在")
        if active_order_of(pile["pileId"]):
            return err(3002, "电桩正在使用中，无法删除")
        pile["deleted"] = True
        return ok({"pileId": pile["pileId"], "deleted": True})

    if t == "station_list":
        include_deleted = bool(p.get("includeDeleted"))
        keyword = (p.get("nameKeyword") or "").strip()
        page = int(p.get("page", 1))
        size = int(p.get("pageSize", 20))
        rows = [s for s in STATE["stations"] if include_deleted or not s["deleted"]]
        if keyword:
            rows = [s for s in rows if keyword in s["name"]]
        rows.sort(key=lambda s: s["stationId"])
        total = len(rows)
        rows = rows[(page - 1) * size: page * size]
        return ok({"page": page, "pageSize": size, "total": total,
                   "stations": [station_summary(s) for s in rows]})

    if t == "station_add":
        name = (p.get("name") or "").strip()
        address = (p.get("address") or "").strip()
        try:
            price = round(float(p.get("pricePerKwh")), 2)
            count = int(p.get("pileCount"))
        except (TypeError, ValueError):
            return err(2001, "参数非法")
        if not name or not address or price <= 0 or not (1 <= count <= 100):
            return err(2001, "参数非法")
        s = {"stationId": STATE["nextStationId"], "name": name, "address": address,
             "lng": p.get("lng"), "lat": p.get("lat"), "pricePerKwh": price, "deleted": False}
        STATE["nextStationId"] += 1
        STATE["stations"].append(s)
        for i in range(count):
            STATE["piles"].append(
                {"pileId": STATE["nextPileId"], "code": "P-%04d" % STATE["nextPileId"],
                 "stationId": s["stationId"],
                 "type": "fast" if i % 2 == 0 else "slow",
                 "powerKw": 60.0 if i % 2 == 0 else 7.0, "status": "idle",
                 "chargeCount": 0, "chargeMinutes": 0, "deleted": False})
            STATE["nextPileId"] += 1
        return ok({"station": station_summary(s), "createdPileCount": count})

    if t == "station_update":
        s = next((x for x in STATE["stations"]
                  if x["stationId"] == p.get("stationId") and not x["deleted"]), None)
        if not s:
            return err(2002, "站点不存在")
        s["name"] = (p.get("name") or s["name"]).strip()
        s["address"] = (p.get("address") or s["address"]).strip()
        if p.get("pricePerKwh"):
            s["pricePerKwh"] = float(p["pricePerKwh"])
        return ok({"station": station_summary(s)})

    if t == "station_delete":
        s = next((x for x in STATE["stations"]
                  if x["stationId"] == p.get("stationId") and not x["deleted"]), None)
        if not s:
            return err(2002, "站点不存在")
        if any(x["stationId"] == s["stationId"] and not x["deleted"]
               and x["status"] == "in_use" for x in STATE["piles"]):
            return err(3002, "站内有占用中的电桩，无法删除")
        s["deleted"] = True
        removed = 0
        for x in STATE["piles"]:
            if x["stationId"] == s["stationId"] and not x["deleted"]:
                x["deleted"] = True
                removed += 1
        return ok({"stationId": s["stationId"], "deleted": True, "removedPileCount": removed})

    if t == "station_detail":
        s = next((x for x in STATE["stations"]
                  if x["stationId"] == p.get("stationId") and not x["deleted"]), None)
        if not s:
            return err(2002, "站点不存在")
        piles = [x for x in STATE["piles"] if x["stationId"] == s["stationId"] and not x["deleted"]]
        return ok({"station": station_summary(s), "piles": [pile_obj(x) for x in piles]})

    if t == "user_list":
        include_deleted = bool(p.get("includeDeleted"))
        keyword = (p.get("phoneKeyword") or "").strip()
        users = [u for u in STATE["users"] if include_deleted or not u["deleted"]]
        if keyword:
            users = [u for u in users if keyword in u["phone"]]
        return ok({"users": [{k: u[k] for k in
                              ("userId", "phone", "nickname", "balance", "regTime", "status", "hasPassword")}
                             for u in users]})

    if t == "user_set_status":
        u = next((x for x in STATE["users"] if x["userId"] == p.get("userId")), None)
        if not u:
            return err(2002, "用户不存在")
        u["status"] = p.get("status", u["status"])
        return ok({"userId": u["userId"], "status": u["status"]})

    if t in ("user_add", "user_update", "user_reset_password", "user_delete"):
        return err(2001, "假服务端未实现该消息")

    if t == "admin_order_list":
        return ok({"page": 1, "pageSize": 20, "total": len(STATE["orders"]),
                   "orders": [order_obj(o) for o in STATE["orders"]]})

    if t == "admin_order_detail":
        o = next((x for x in STATE["orders"] if x["orderId"] == p.get("orderId")), None)
        if not o:
            return err(2002, "订单不存在")
        u = STATE["users"][0]
        s = STATE["stations"][0]
        pile = STATE["piles"][0]
        return ok({"order": order_obj(o),
                   "user": {k: u[k] for k in ("userId", "phone", "nickname", "balance",
                                              "regTime", "status", "hasPassword")},
                   "station": station_summary(s), "pile": pile_obj(pile)})

    return err(1002, "未知消息类型")


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        print("== 客户端连接 ==")
        for line in self.rfile:
            try:
                req = json.loads(line.decode("utf-8"))
            except ValueError:
                continue
            code, msg, data = handle(req)
            resp = {"seq": req.get("seq"), "type": req.get("type"),
                    "code": code, "msg": msg, "data": data}
            print("<- %s code=%s %s" % (req.get("type"), code, msg if code != 0 else ""))
            self.wfile.write((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))


socketserver.ThreadingTCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(("0.0.0.0", PORT), Handler) as srv:
    print("mock v2.3 listening on %d" % PORT)
    srv.serve_forever()
