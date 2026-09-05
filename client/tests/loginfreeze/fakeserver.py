#!/usr/bin/env python3
"""loginfreeze harness 用的可控假服务端（协议 v2 子集）。

用法：fakeserver.py PORT CASE
CASE：
  ok       user_login 成功（老用户，hasPassword=true）
  newuser  user_login 成功（新用户 isNew=true, hasPassword=false）
  err1001 / err1002 / err1005  user_login 返回对应错误码
所有 case 都应答 ping、user_code_request（固定 483920）、user_password_update、
以及登录后首页会发的 active_order_get / nearby_station_list / user_profile_get /
user_order_list / station_list（空数据），未知 type 返回 3001。
"""

import json
import socketserver
import sys

PORT = int(sys.argv[1])
CASE = sys.argv[2] if len(sys.argv) > 2 else "ok"


def user(phone, has_password):
    return {"userId": 1, "phone": phone, "nickname": "测试用户", "balance": 50.0,
            "regTime": "2026-09-01T10:20:30+08:00", "status": "normal",
            "hasPassword": has_password, "avatar": None}


def handle(req):
    t = req.get("type", "")
    p = req.get("payload") or {}
    phone = p.get("phone", "13800001111")
    if t == "ping":
        return 0, "ok", {}
    if t == "user_login":
        if CASE == "ok":
            return 0, "ok", {"isNew": False, "user": user(phone, True)}
        if CASE == "newuser":
            return 0, "ok", {"isNew": True, "user": user(phone, False)}
        if CASE == "err1001":
            return 1001, "密码或验证码错误", None
        if CASE == "err1002":
            return 1002, "账号已冻结", None
        if CASE == "err1005":
            return 1005, "账号已注销", None
        return 3001, "unknown case", None
    if t == "user_code_request":
        return 0, "ok", {"code": "483920", "validSec": 300}
    if t == "user_password_update":
        return 0, "ok", {"hasPassword": True}
    if t == "user_profile_get":
        return 0, "ok", {"user": user(phone, True)}
    if t == "active_order_get":
        return 0, "ok", {"order": None}
    if t == "user_order_list":
        return 0, "ok", {"orders": [], "total": 0, "page": 1, "pageSize": 10}
    if t in ("nearby_station_list", "station_list"):
        return 0, "ok", {"stations": [], "total": 0, "page": 1, "pageSize": 10}
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
            code, msg, data = handle(req)
            resp = {"seq": req.get("seq", -1), "type": req.get("type", ""),
                    "code": code, "msg": msg, "data": data}
            self.wfile.write((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    with Server(("127.0.0.1", PORT), Handler) as srv:
        print("fakeserver listening on %d case=%s" % (PORT, CASE), flush=True)
        srv.serve_forever()
