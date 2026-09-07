#!/usr/bin/env python3
"""协议 v2.5 有状态假服务端（供 admin 模块 offscreen 联调验证，不入交付）。

基于 tools/mock_server_v23.py 扩展：
- station_add 升级为 v2.5：piles 显式电桩清单（破坏性变更，pileCount 不再支持）；
- 补齐 v2.4 语义：pile_list 附 occupancy（reserved/charging）；charge_stop 即释放电桩；
  pile_restart/pile_disable 放开到 in_use 并强制终结占用订单（响应带
  affectedOrderId/affectedOrderStatus）；user_detail/user_update（头像/余额）；
  admin_order_cancel/admin_order_stop；
- 补齐用户侧流程：user_login（自动注册）/ wallet_recharge /
  charge_reserve / charge_start / charge_stop / charge_settle / charge_cancel，
  供 v24 harness 以用户身份自建订单数据（按连接维持会话）。

用法：python3 mock_server_v25.py [端口]   # 默认 18898
"""

import base64
import datetime
import json
import re
import socketserver
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18898

TZ = datetime.timezone(datetime.timedelta(hours=8))
SIM_ENERGY_KWH = 20.0   # 模拟充电电量（服务端口径，客户端不可提交）
SIM_CHARGE_MINUTES = 30


def now_iso():
    return datetime.datetime.now(TZ).isoformat(timespec="seconds")


STATE = {
    "admins": [{"adminId": 1, "username": "admin", "password": "123456"}],
    "nextAdminId": 2,
    "stations": [
        {"stationId": 1, "name": "良乡大学城北站", "address": "房山区良乡高教园区",
         "lng": 116.123, "lat": 39.732, "pricePerKwh": 1.20, "deleted": False},
        {"stationId": 2, "name": "长阳地铁站充电站", "address": "房山区长阳镇京良路",
         "lng": 116.212, "lat": 39.764, "pricePerKwh": 1.50, "deleted": False},
    ],
    "nextStationId": 3,
    "piles": [
        {"pileId": 101, "code": "P-0101", "stationId": 1, "type": "fast", "powerKw": 60.0,
         "status": "idle", "chargeCount": 10, "chargeMinutes": 600, "deleted": False},
        {"pileId": 102, "code": "P-0102", "stationId": 1, "type": "slow", "powerKw": 7.0,
         "status": "in_use", "chargeCount": 5, "chargeMinutes": 300, "deleted": False},
        {"pileId": 103, "code": "P-0103", "stationId": 1, "type": "fast", "powerKw": 120.0,
         "status": "fault", "chargeCount": 3, "chargeMinutes": 120, "deleted": False},
        {"pileId": 201, "code": "P-0201", "stationId": 2, "type": "fast", "powerKw": 90.0,
         "status": "idle", "chargeCount": 20, "chargeMinutes": 900, "deleted": False},
        {"pileId": 202, "code": "P-0202", "stationId": 2, "type": "slow", "powerKw": 7.0,
         "status": "idle", "chargeCount": 8, "chargeMinutes": 480, "deleted": False},
    ],
    "nextPileId": 203,
    "users": [
        {"userId": 1, "phone": "13800001234", "nickname": "测试用户", "balance": 100.0,
         "regTime": "2026-09-01T10:00:00+08:00", "status": "normal", "hasPassword": True,
         "password": "abc123", "avatar": None, "deleted": False},
        {"userId": 2, "phone": "13900005678", "nickname": "冻结用户", "balance": 25.5,
         "regTime": "2026-09-02T11:00:00+08:00", "status": "frozen", "hasPassword": True,
         "password": "abc123", "avatar": None, "deleted": False},
        {"userId": 3, "phone": "13700009999", "nickname": "已删用户", "balance": 0.0,
         "regTime": "2026-09-03T12:00:00+08:00", "status": "normal", "hasPassword": True,
         "password": "abc123", "avatar": None, "deleted": True},
    ],
    "nextUserId": 4,
    "orders": [
        # 102 号桩的占用订单（充电中）
        {"orderId": 10001, "userPhone": "13800001234", "stationId": 1, "pileId": 102,
         "status": "charging",
         "reservedAt": "2026-09-05T10:00:00+08:00",
         "startTime": "2026-09-05T10:02:00+08:00", "endTime": None, "settledAt": None,
         "energyKwh": None, "unitPrice": 1.20, "amount": None},
        {"orderId": 10002, "userPhone": "13900005678", "stationId": 2, "pileId": 201,
         "status": "completed",
         "reservedAt": "2026-09-04T09:00:00+08:00",
         "startTime": "2026-09-04T09:02:00+08:00", "endTime": "2026-09-04T10:02:00+08:00",
         "settledAt": "2026-09-04T10:03:00+08:00",
         "energyKwh": 45.5, "unitPrice": 1.50, "amount": 68.25},
        {"orderId": 10003, "userPhone": "13800001234", "stationId": 1, "pileId": 101,
         "status": "cancelled",
         "reservedAt": "2026-09-03T08:00:00+08:00",
         "startTime": None, "endTime": None, "settledAt": None,
         "energyKwh": None, "unitPrice": 1.20, "amount": None},
    ],
    "nextOrderId": 10004,
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


def active_order_of(pile_id):
    return next((o for o in STATE["orders"]
                 if o["pileId"] == pile_id and o["status"] in ("reserved", "charging")), None)


def pile_obj(p):
    st = next((s for s in STATE["stations"] if s["stationId"] == p["stationId"]), None)
    active = active_order_of(p["pileId"])
    # v2.4：in_use 附占用类型（reserved/charging），无占用为 null（待结算已释放）
    occupancy = active["status"] if active and p["status"] == "in_use" else None
    out = {"pileId": p["pileId"], "code": p["code"], "stationId": p["stationId"],
           "stationName": st["name"] if st else "", "type": p["type"],
           "powerKw": p["powerKw"], "status": p["status"], "occupancy": occupancy,
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


def user_obj(u, with_avatar=True):
    out = {"userId": u["userId"], "phone": u["phone"], "nickname": u["nickname"],
           "balance": round(u["balance"], 2), "regTime": u["regTime"], "status": u["status"],
           "hasPassword": u["hasPassword"]}
    if with_avatar:
        out["avatar"] = u.get("avatar")
    if u.get("deleted"):
        out["deleted"] = True
    return out


def find_pile(pile_id):
    return next((p for p in STATE["piles"] if p["pileId"] == pile_id and not p["deleted"]), None)


def find_order(order_id):
    return next((o for o in STATE["orders"] if o["orderId"] == order_id), None)


def find_user(user_id):
    return next((u for u in STATE["users"] if u["userId"] == user_id and not u["deleted"]), None)


def bill_order(o):
    """按 charge_stop 规则计费：模拟电量 × 单价，订单转 pending_payment。"""
    o["energyKwh"] = SIM_ENERGY_KWH
    o["amount"] = round(SIM_ENERGY_KWH * o["unitPrice"], 2)
    o["endTime"] = now_iso()
    o["status"] = "pending_payment"


def release_pile_after_bill(pile):
    """v2.4：停止充电即释放电桩为 idle 并累计次数与时长。"""
    pile["status"] = "idle"
    pile["chargeCount"] += 1
    pile["chargeMinutes"] += SIM_CHARGE_MINUTES


def force_end_active_order(pile):
    """v2.4 pile_restart/pile_disable 共用：强制终结占用订单，返回受影响订单。"""
    order = active_order_of(pile["pileId"])
    if not order:
        return None, None
    if order["status"] == "reserved":
        order["status"] = "cancelled"
    else:
        bill_order(order)
        release_pile_after_bill(pile)
    return order["orderId"], order["status"]


def ok(data):
    return 0, "ok", data


def err(code, msg):
    return code, msg, None


def handle(req, session):
    t = req.get("type")
    p = req.get("payload") or {}

    if t == "ping":
        return ok({"serverTime": now_iso()})

    # ---------- 用户侧（按连接会话，供 v24 harness 自建数据） ----------

    if t == "user_login":
        phone = (p.get("phone") or "").strip()
        if not re.fullmatch(r"1[3-9]\d{9}", phone):
            return err(2001, "手机号格式非法")
        u = next((x for x in STATE["users"] if x["phone"] == phone and not x["deleted"]), None)
        is_new = False
        if u is None:
            # 自动注册：默认昵称「用户+后4位」，余额 0，密码方式直接保存密码
            u = {"userId": STATE["nextUserId"], "phone": phone,
                 "nickname": "用户" + phone[-4:], "balance": 0.0,
                 "regTime": now_iso(), "status": "normal",
                 "hasPassword": bool(p.get("password")), "password": p.get("password"),
                 "avatar": None, "deleted": False}
            STATE["nextUserId"] += 1
            STATE["users"].append(u)
            is_new = True
        else:
            if u["status"] == "frozen":
                return err(1002, "账号已冻结")
            if p.get("password") is not None and u.get("password") != p.get("password"):
                return err(1001, "密码错误")
        session["userId"] = u["userId"]
        return ok({"isNew": is_new, "user": user_obj(u)})

    if t == "wallet_recharge":
        u = find_user(session.get("userId"))
        if not u:
            return err(1003, "未登录")
        try:
            amount = round(float(p.get("amount")), 2)
        except (TypeError, ValueError):
            return err(2001, "参数非法")
        if amount <= 0 or amount > 10000:
            return err(2001, "充值金额非法")
        u["balance"] = round(u["balance"] + amount, 2)
        return ok({"amount": amount, "balance": u["balance"]})

    if t == "charge_reserve":
        u = find_user(session.get("userId"))
        if not u:
            return err(1003, "未登录")
        pile = find_pile(p.get("pileId"))
        if not pile:
            return err(2002, "电桩不存在")
        if pile["status"] != "idle" or active_order_of(pile["pileId"]):
            return err(3003, "电桩不在空闲状态")
        if u["balance"] <= 0:
            return err(3004, "余额不足")
        st = next(s for s in STATE["stations"] if s["stationId"] == pile["stationId"])
        o = {"orderId": STATE["nextOrderId"], "userPhone": u["phone"],
             "stationId": pile["stationId"], "pileId": pile["pileId"], "status": "reserved",
             "reservedAt": now_iso(), "startTime": None, "endTime": None, "settledAt": None,
             "energyKwh": None, "unitPrice": st["pricePerKwh"], "amount": None}
        STATE["nextOrderId"] += 1
        STATE["orders"].append(o)
        pile["status"] = "in_use"
        return ok({"order": order_obj(o)})

    if t == "charge_start":
        u = find_user(session.get("userId"))
        o = find_order(p.get("orderId"))
        if not u or not o or o["userPhone"] != u["phone"]:
            return err(2002, "订单不存在")
        if o["status"] != "reserved":
            return err(3002, "订单状态不允许开始充电")
        o["status"] = "charging"
        o["startTime"] = now_iso()
        return ok({"order": order_obj(o)})

    if t == "charge_stop":
        u = find_user(session.get("userId"))
        o = find_order(p.get("orderId"))
        if not u or not o or o["userPhone"] != u["phone"]:
            return err(2002, "订单不存在")
        if o["status"] != "charging":
            return err(3002, "订单状态不允许停止")
        bill_order(o)
        release_pile_after_bill(next(x for x in STATE["piles"] if x["pileId"] == o["pileId"]))
        return ok({"order": order_obj(o)})

    if t == "charge_settle":
        u = find_user(session.get("userId"))
        o = find_order(p.get("orderId"))
        if not u or not o or o["userPhone"] != u["phone"]:
            return err(2002, "订单不存在")
        if o["status"] != "pending_payment":
            return err(3002, "订单状态不允许结算")
        if u["balance"] < o["amount"]:
            return err(3004, "余额不足")
        u["balance"] = round(u["balance"] - o["amount"], 2)
        o["status"] = "completed"
        o["settledAt"] = now_iso()
        return ok({"order": order_obj(o), "balance": u["balance"]})

    if t == "charge_cancel":
        u = find_user(session.get("userId"))
        o = find_order(p.get("orderId"))
        if not u or not o or o["userPhone"] != u["phone"]:
            return err(2002, "订单不存在")
        if o["status"] != "reserved":
            return err(3002, "订单状态不允许取消")
        o["status"] = "cancelled"
        pile = next(x for x in STATE["piles"] if x["pileId"] == o["pileId"])
        pile["status"] = "idle"
        return ok({"orderId": o["orderId"], "status": "cancelled",
                   "pileId": pile["pileId"], "pileStatus": "idle"})

    # ---------- 管理侧 ----------

    if t == "admin_login":
        a = next((x for x in STATE["admins"] if x["username"] == p.get("username")), None)
        if not a or a["password"] != p.get("password"):
            return err(1001, "用户名或密码错误")
        return ok({"adminId": a["adminId"], "username": a["username"]})

    if t == "admin_password_update":
        a = next((x for x in STATE["admins"] if x["username"] == p.get("username")), None)
        if not a or a["password"] != p.get("oldPassword"):
            return err(1001, "原密码错误")
        a["password"] = p.get("newPassword")
        return ok({"updated": True})

    if t == "admin_list":
        return ok({"admins": [{"adminId": a["adminId"], "username": a["username"]}
                              for a in STATE["admins"]]})

    if t == "admin_add":
        username = (p.get("username") or "").strip()
        password = p.get("password") or ""
        if not username or len(password) < 6:
            return err(2001, "参数非法")
        if any(a["username"] == username for a in STATE["admins"]):
            return err(2001, "用户名已存在")
        a = {"adminId": STATE["nextAdminId"], "username": username, "password": password}
        STATE["nextAdminId"] += 1
        STATE["admins"].append(a)
        return ok({"adminId": a["adminId"], "username": username})

    if t == "admin_delete":
        aid = p.get("adminId")
        a = next((x for x in STATE["admins"] if x["adminId"] == aid), None)
        if not a:
            return err(2002, "管理员不存在")
        if a["username"] == p.get("username") or a["adminId"] == 1:
            return err(3001, "不能删除自己")
        STATE["admins"].remove(a)
        return ok({"deleted": True})

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
        station_id = int(p.get("stationId") or 0)
        piles = [x for x in STATE["piles"] if include_deleted or not x["deleted"]]
        # 协议 7.5：stationId 省略或为 0 表示全部站点，正整数按站点筛选
        if station_id > 0:
            piles = [x for x in piles if x["stationId"] == station_id]
        return ok({"piles": [pile_obj(x) for x in sorted(piles, key=lambda x: x["pileId"])]})

    if t == "pile_restart":
        # v2.4：任意状态可重启；占用订单强制终结（reserved→cancelled，charging→计费待结算）
        pile = find_pile(p.get("pileId"))
        if not pile:
            return err(2002, "电桩不存在")
        order_id, order_status = force_end_active_order(pile)
        pile["status"] = "idle"
        return ok({"pileId": pile["pileId"], "status": "idle",
                   "affectedOrderId": order_id, "affectedOrderStatus": order_status})

    if t == "pile_disable":
        # v2.4：idle/in_use 可禁用；占用订单强制终结后电桩置 fault
        pile = find_pile(p.get("pileId"))
        if not pile:
            return err(2002, "电桩不存在")
        if pile["status"] == "fault":
            return err(3002, "电桩已是故障状态")
        order_id, order_status = force_end_active_order(pile)
        pile["status"] = "fault"
        return ok({"pileId": pile["pileId"], "status": "fault",
                   "affectedOrderId": order_id, "affectedOrderStatus": order_status})

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
        # v2.5：piles 显式电桩清单（pileCount 已废弃），逐桩校验、全局编号唯一
        name = (p.get("name") or "").strip()
        address = (p.get("address") or "").strip()
        try:
            price = round(float(p.get("pricePerKwh")), 2)
        except (TypeError, ValueError):
            return err(2001, "参数非法")
        piles_in = p.get("piles")
        if not name or not address or price <= 0:
            return err(2001, "参数非法")
        if not isinstance(piles_in, list) or not (1 <= len(piles_in) <= 100):
            return err(2001, "piles 必须为 1 至 100 条的数组")
        seen = set()
        new_piles = []
        for item in piles_in:
            if not isinstance(item, dict):
                return err(2001, "电桩条目必须为对象")
            code = (item.get("code") or "").strip()
            ptype = item.get("type")
            try:
                power = float(item.get("powerKw"))
            except (TypeError, ValueError):
                return err(2001, "powerKw 必须大于 0")
            if not (1 <= len(code) <= 20) or ptype not in ("fast", "slow") or power <= 0:
                return err(2001, "电桩参数非法")
            if code in seen or any(x["code"] == code for x in STATE["piles"]):
                return err(2001, "电桩编号 %s 已存在" % code)
            seen.add(code)
            new_piles.append({"code": code, "type": ptype, "powerKw": power})
        s = {"stationId": STATE["nextStationId"], "name": name, "address": address,
             "lng": p.get("lng"), "lat": p.get("lat"), "pricePerKwh": price, "deleted": False}
        STATE["nextStationId"] += 1
        STATE["stations"].append(s)
        for item in new_piles:
            STATE["piles"].append(
                {"pileId": STATE["nextPileId"], "code": item["code"],
                 "stationId": s["stationId"], "type": item["type"],
                 "powerKw": item["powerKw"], "status": "idle",
                 "chargeCount": 0, "chargeMinutes": 0, "deleted": False})
            STATE["nextPileId"] += 1
        return ok({"station": station_summary(s), "createdPileCount": len(new_piles)})

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
        return ok({"users": [user_obj(u, with_avatar=False) for u in users]})

    if t == "user_detail":
        # v2.4：含头像的完整资料，供编辑对话框使用
        u = find_user(p.get("userId"))
        if not u:
            return err(2002, "用户不存在")
        return ok({"user": user_obj(u)})

    if t == "user_set_status":
        u = next((x for x in STATE["users"] if x["userId"] == p.get("userId")), None)
        if not u:
            return err(2002, "用户不存在")
        u["status"] = p.get("status", u["status"])
        return ok({"userId": u["userId"], "status": u["status"]})

    if t == "user_update":
        # v2.4：支持 phone/nickname/avatar/balance，返回更新后的完整资料
        u = find_user(p.get("userId"))
        if not u:
            return err(2002, "用户不存在")
        if "phone" in p:
            phone = (p.get("phone") or "").strip()
            if not re.fullmatch(r"1[3-9]\d{9}", phone):
                return err(2001, "手机号格式非法")
            if any(x is not u and not x["deleted"] and x["phone"] == phone
                   for x in STATE["users"]):
                return err(2001, "手机号已被使用")
            u["phone"] = phone
        if "nickname" in p:
            nickname = (p.get("nickname") or "").strip()
            if not (1 <= len(nickname) <= 20):
                return err(2001, "昵称长度须为 1 至 20")
            u["nickname"] = nickname
        if "balance" in p:
            try:
                balance = round(float(p.get("balance")), 2)
            except (TypeError, ValueError):
                return err(2001, "余额非法")
            if balance < 0 or balance > 1000000:
                return err(2001, "余额非法")
            u["balance"] = balance
        if "avatar" in p:
            avatar = p.get("avatar")
            if avatar is None:
                u["avatar"] = None  # 显式 null 表示清除头像
            else:
                mime = avatar.get("mime")
                try:
                    raw = base64.b64decode(avatar.get("base64") or "")
                except (TypeError, ValueError):
                    return err(2001, "头像非法")
                if mime not in ("image/jpeg", "image/png") or len(raw) > 512 * 1024:
                    return err(2001, "头像非法")
                u["avatar"] = {"mime": mime, "base64": avatar.get("base64")}
        return ok({"user": user_obj(u)})

    if t in ("user_add", "user_reset_password", "user_delete"):
        return err(2001, "假服务端未实现该消息")

    if t == "admin_order_list":
        keyword = (p.get("phoneKeyword") or "").strip()
        status = (p.get("status") or "").strip()
        orders = STATE["orders"]
        if keyword:
            orders = [o for o in orders if keyword in o["userPhone"]]
        if status:
            orders = [o for o in orders if o["status"] == status]
        page = int(p.get("page", 1))
        size = int(p.get("pageSize", 20))
        return ok({"page": page, "pageSize": size, "total": len(orders),
                   "orders": [order_obj(o) for o in orders[(page - 1) * size: page * size]]})

    if t == "admin_order_detail":
        o = find_order(p.get("orderId"))
        if not o:
            return err(2002, "订单不存在")
        u = next((x for x in STATE["users"] if x["phone"] == o["userPhone"]), STATE["users"][0])
        s = next((x for x in STATE["stations"] if x["stationId"] == o["stationId"]),
                 STATE["stations"][0])
        pile = next((x for x in STATE["piles"] if x["pileId"] == o["pileId"]),
                    STATE["piles"][0])
        return ok({"order": order_obj(o),
                   "user": user_obj(u, with_avatar=False),
                   "station": station_summary(s), "pile": pile_obj(pile)})

    if t == "admin_order_cancel":
        # v2.4：仅 reserved 可取消，事务内取消订单并释放电桩
        o = find_order(p.get("orderId"))
        if not o:
            return err(2002, "订单不存在")
        if o["status"] != "reserved":
            return err(3002, "订单状态不允许取消")
        o["status"] = "cancelled"
        pile = next(x for x in STATE["piles"] if x["pileId"] == o["pileId"])
        pile["status"] = "idle"
        return ok({"order": order_obj(o)})

    if t == "admin_order_stop":
        # v2.4：仅 charging 可停止，计费转待结算并释放电桩
        o = find_order(p.get("orderId"))
        if not o:
            return err(2002, "订单不存在")
        if o["status"] != "charging":
            return err(3002, "订单状态不允许停止")
        bill_order(o)
        release_pile_after_bill(next(x for x in STATE["piles"] if x["pileId"] == o["pileId"]))
        return ok({"order": order_obj(o)})

    return err(1002, "未知消息类型")


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        session = {}  # 按连接维持用户会话（user_login 写入 userId）
        print("== 客户端连接 ==")
        for line in self.rfile:
            try:
                req = json.loads(line.decode("utf-8"))
            except ValueError:
                continue
            code, msg, data = handle(req, session)
            resp = {"seq": req.get("seq"), "type": req.get("type"),
                    "code": code, "msg": msg, "data": data}
            print("<- %s code=%s %s" % (req.get("type"), code, msg if code != 0 else ""))
            self.wfile.write((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))


socketserver.ThreadingTCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(("0.0.0.0", PORT), Handler) as srv:
    print("mock v2.5 listening on %d" % PORT)
    srv.serve_forever()
