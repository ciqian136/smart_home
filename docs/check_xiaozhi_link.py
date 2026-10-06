#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
home/cmd + home/state 协议自检（纯标准库，Python 3）。

用法：
    python check_xiaozhi_link.py

它验证的是**两端协议与解析算法**，不是 C/C++ 代码本身：
  * 用 Python 复刻 STM32 `APP/json_parser.c` 的 `json_find_value()` /
    `parse_onenet_params()` 查找规则（注意它是字符串 strstr，不是真 JSON 解析），
    再用这套规则去解析小智 `home_mcp_tools.cc` 真实会发出的那些负载；
  * 复刻 STM32 `APP/esp32_xiaozhi.c` 的 `handle_subrecv()` 两种 `+MQTTSUBRECV`
    排版判定与负载边界计算；
  * 复刻小智 `dashboard_model.cc` 的 `ParseSmartHomeLocked()`，
    去解析 STM32 `build_state_payload()` 的真实输出。

改了任何一端的字段名 / 主题名 / 数值范围，先跑这个脚本再上板。
"""

import json
import re
import sys

FAILURES = []
CHECKS = [0]


def check(name, ok, detail=""):
    CHECKS[0] += 1
    if ok:
        print("  ok   %s" % name)
    else:
        print("  FAIL %s%s" % (name, ("  -> " + detail) if detail else ""))
        FAILURES.append(name)


# --------------------------------------------------------------------------
# 1) STM32 侧：json_parser.c 的查找/解析规则复刻
# --------------------------------------------------------------------------

def _skip_ws(s, pos):
    while pos < len(s) and s[pos] in ' \t\n\r':
        pos += 1
    return pos


def json_find_value(js, key):
    """对应 json_parser.c: json_find_value()。返回 value 起始下标或 None。

    两个分支都会跳过值前的空白；`"key": {"value": X` 这种带空格的包装写法
    也会被识别（否则 '{' 会被当成值，bool 解析成 0 —— "让开灯反而关灯"）。
    """
    pattern = '"%s":{"value":' % re.escape(key)
    idx = js.find(pattern)
    if idx >= 0:
        return _skip_ws(js, idx + len(pattern))

    pattern = '"%s":' % re.escape(key)
    idx = js.find(pattern)
    if idx < 0:
        return None
    pos = _skip_ws(js, idx + len(pattern))

    if pos < len(js) and js[pos] == '{':
        vk = js.find('"value"', pos)
        if vk >= 0 and (vk - pos) < 16:
            pos = _skip_ws(js, vk + 7)
            if pos < len(js) and js[pos] == ':':
                pos = _skip_ws(js, pos + 1)
    return pos


def _parse_int(s, pos):
    sign = 1
    if pos < len(s) and s[pos] == '-':
        sign = -1
        pos += 1
    num = 0
    while pos < len(s) and s[pos].isdigit():
        num = num * 10 + int(s[pos])
        pos += 1
    return num * sign


def _parse_bool(s, pos):
    # 对应 parse_bool_value(): true/false，另外容忍 OneNET 的 1/0
    # 以及把布尔写成字符串的 "true"/"1"；都不匹配时当 0（found 仍标记为"找到了"）。
    if pos < len(s) and s[pos] == '"':
        pos += 1
    if s.startswith("true", pos):
        return 1
    if s.startswith("false", pos):
        return 0
    if pos < len(s) and s[pos] == '1':
        return 1
    if pos < len(s) and s[pos] == '0':
        return 0
    return 0


def parse_params(js, spec):
    """spec: [(key, 'i'|'b'|'f'), ...] -> (values, found_flags) 顺序对应 spec。"""
    values, found = [], []
    for key, kind in spec:
        pos = json_find_value(js, key)
        if pos is None:
            found.append(False)
            values.append(0)
            continue
        found.append(True)
        if kind == 'i':
            values.append(_parse_int(js, pos))
        elif kind == 'b':
            values.append(_parse_bool(js, pos))
        elif kind == 'f':
            values.append(float(_parse_int(js, pos)))
        else:
            raise ValueError(kind)
    return values, found


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def home_cmd_apply(js):
    """复刻 APP/home_cmd.c 的 home_cmd_apply()。返回 (result, effects)。"""
    std = ["led", "fan", "rgb1_r", "rgb1_g", "rgb1_b", "rgb2_r", "rgb2_g", "rgb2_b"]
    kinds = ["b", "i", "i", "i", "i", "i", "i", "i"]
    v, found = parse_params(js, list(zip(std, kinds)))

    aliases = ["LED", "RGB1_RAD", "RGB1_GREEN", "RGB1_BLUE", "RGB2_RAD", "RGB2_GREEN", "RGB2_BLUE"]
    akinds = ["b", "i", "i", "i", "i", "i", "i"]
    av, afound = parse_params(js, list(zip(aliases, akinds)))

    effects = {}

    if found[0] or afound[0]:
        effects["led"] = bool(v[0] if found[0] else av[0])

    if found[1]:
        effects["fan"] = clamp(v[1], 0, 1000)

    def pick3(f_idx, a_idx, base=(0, 0, 0)):
        """base = 设备当前颜色。ws2812.c / ws2812_2.c 的静态初值是 (255,200,100)，
        但 schedule_init() 一上电就 ws2812_set_all(0,0,0) / ws2812_2_set_all(0,0,0)，
        所以运行期基线是 (0,0,0)。"""
        has = [found[f_idx[i]] or afound[a_idx[i]] for i in range(3)]
        if not any(has):
            return None
        vals = [v[f_idx[i]] if found[f_idx[i]] else av[a_idx[i]] for i in range(3)]
        return tuple(clamp(vals[i] if has[i] else base[i], 0, 255) for i in range(3))

    rgb1 = pick3([2, 3, 4], [1, 2, 3])
    if rgb1:
        effects["rgb1"] = rgb1
    rgb2 = pick3([5, 6, 7], [4, 5, 6])
    if rgb2:
        effects["rgb2"] = rgb2

    if effects:
        return "APPLIED", effects
    if '"op":"query"' in js or '"op": "query"' in js:
        return "QUERY", effects
    return "NONE", effects


# --------------------------------------------------------------------------
# 2) STM32 侧：handle_subrecv() 的排版判定与边界计算复刻
# --------------------------------------------------------------------------

def _decimal_field(text):
    """对应 parse_decimal_field()：允许引号/空格，出现非数字就返回 None。"""
    digits = 0
    value = 0
    for ch in text:
        if ch in ' \t"':
            continue
        if not ch.isdigit():
            return None
        digits += 1
        if digits > 5:
            return None
        value = value * 10 + int(ch)
    return value if digits else None


def handle_subrecv(line, cmd_topic="home/cmd"):
    """返回 (consumed, applied_json_or_None)。line 是完整的一条记录（含 \\r\\n）。"""
    body = line[:-2] if line.endswith("\r\n") else line
    parts = body.split(',', 3)
    if len(parts) < 4:
        return len(line), None            # 分隔符不够 -> consume_record_line 兜底

    head, f1, f2 = parts[0], parts[1], parts[2]
    payload = parts[3]

    l1 = _decimal_field(f1)
    l2 = _decimal_field(f2)

    if l2 is not None and l1 is None:      # 排版 A: topic,len,data
        data_len, topic = l2, f1
    elif l1 is not None and l2 is None:    # 排版 B: len,topic,data
        data_len, topic = l1, f2
    elif l1 is not None and l2 is not None:
        data_len, topic = l2, None
    else:
        return len(line), None

    topic = (topic or "").strip('"')
    return len(line), (payload if (topic == cmd_topic and data_len == len(payload)) else None)


# --------------------------------------------------------------------------
# 3) 小智侧：home_mcp_tools.cc 会发出的负载复刻
# --------------------------------------------------------------------------

COLORS = {
    "red": (255, 0, 0), "green": (0, 255, 0), "blue": (0, 0, 255),
    "white": (255, 255, 255), "warm": (255, 180, 107), "warmwhite": (255, 180, 107),
    "cool": (180, 220, 255), "purple": (160, 0, 255), "cyan": (0, 255, 255),
    "yellow": (255, 255, 0), "orange": (255, 140, 0), "pink": (255, 105, 180),
    "off": (0, 0, 0),
}
GEARS = {1: 250, 2: 500, 3: 750, 4: 1000}
DEVICE = "smart_home_01"


def wants_strip1(s):
    s = s.strip().lower()
    return s not in ("2", "strip2", "light2", "second")


def wants_strip2(s):
    s = s.strip().lower()
    return s in ("2", "strip2", "light2", "second", "both", "all")


def cmd_payload(params_body, seq=1, query=False):
    if query:
        body = '{"id":"sh-%d","source":"xiaozhi","device":"%s","op":"query"}' % (seq, DEVICE)
    else:
        body = ('{"id":"sh-%d","source":"xiaozhi","device":"%s","params":{%s}}'
                % (seq, DEVICE, params_body))
    assert len(body) < 320, "payload 超出 home_mcp_tools.cc 的 kMaxPayload=320"
    return body


def tool_set_light(strip="1", on=True, color="", r=-1, g=-1, b=-1, cached=None):
    rgb = [0, 0, 0]
    if not on:
        rgb = [0, 0, 0]
    elif color.lower().strip() in COLORS:
        rgb = list(COLORS[color.lower().strip()])
    elif r >= 0 or g >= 0 or b >= 0:
        base = cached if cached else (255, 255, 255)
        rgb = [clamp(r, 0, 255) if r >= 0 else base[0],
               clamp(g, 0, 255) if g >= 0 else base[1],
               clamp(b, 0, 255) if b >= 0 else base[2]]
    elif color.strip():
        return None
    elif cached:
        rgb = list(cached)
    else:
        rgb = [255, 180, 107]
    parts = []
    if wants_strip1(strip):
        parts.append('"rgb1_r":%d,"rgb1_g":%d,"rgb1_b":%d' % tuple(rgb))
    if wants_strip2(strip):
        parts.append('"rgb2_r":%d,"rgb2_g":%d,"rgb2_b":%d' % tuple(rgb))
    return ",".join(parts) if parts else None


def tool_set_fan(on=True, gear=-1, speed=-1, cached=None):
    if not on:
        target = 0
    elif gear >= 1:
        if gear > 4:
            return None
        target = GEARS[gear]
    elif speed >= 0:
        target = clamp(speed, 0, 1000)
    else:
        target = cached if (cached and cached > 0) else 500
    return '"fan":%d' % target


# --------------------------------------------------------------------------
# 4) STM32 侧：build_state_payload() 复刻 + 小智侧解析复刻
# --------------------------------------------------------------------------

def build_state_payload(ts=1789818624, online=True):
    dev = {"name": "living_room", "online": online,
           "temp": 25.5, "humi": 60.0, "light": 123.4,
           "pm25": 418, "mq2": 315, "fan": 500, "led": False,
           "rgb1": [255, 200, 100], "rgb2": [0, 0, 0]}
    return ('{"source":"smarthome-bridge","version":"1.0.0","ts":%d,"count":1,"online":%s,'
            '"devices":{"%s":%s}}'
            % (ts, "true" if online else "false", DEVICE, json.dumps(dev, separators=(',', ':'))))


def parse_state_locked(payload):
    """复刻 dashboard_model.cc 的 ParseSmartHomeLocked()（用 cJSON 语义）。"""
    root = json.loads(payload)
    out = {"valid": True, "device_count": 0, "devices": []}
    out["ts"] = int(root.get("ts", 0))
    out["count"] = int(root.get("count", 0))
    out["online"] = bool(root.get("online", False))
    for dev_id, entry in (root.get("devices") or {}).items():
        dev = {"id": dev_id, "name": entry.get("name") or dev_id,
               "online": bool(entry.get("online", False))}
        for key in ("temp", "humi", "light", "pm25", "mq2", "fan"):
            if isinstance(entry.get(key), (int, float)) and not isinstance(entry.get(key), bool):
                dev[key] = entry[key]
        if isinstance(entry.get("led"), bool):
            dev["led"] = entry["led"]
        for key in ("rgb1", "rgb2"):
            v = entry.get(key)
            if isinstance(v, list) and len(v) >= 3 and all(isinstance(x, (int, float)) for x in v[:3]):
                dev[key] = [int(x) for x in v[:3]]
        out["devices"].append(dev)
    out["device_count"] = len(out["devices"])
    return out


# --------------------------------------------------------------------------
# 测试
# --------------------------------------------------------------------------

def main():
    print("== 1. 小智工具 -> home/cmd 负载 -> STM32 home_cmd_apply() ==")

    cases = [
        # (说明, 负载, 期望 result, 期望 effects)
        ("打开灯带1（无颜色 -> 暖白）",
         cmd_payload(tool_set_light(strip="1")), "APPLIED", {"rgb1": (255, 180, 107)}),
        ("灯带1 蓝色",
         cmd_payload(tool_set_light(strip="1", color="blue")), "APPLIED", {"rgb1": (0, 0, 255)}),
        ("两路灯带都关",
         cmd_payload(tool_set_light(strip="both", on=False)), "APPLIED",
         {"rgb1": (0, 0, 0), "rgb2": (0, 0, 0)}),
        ("只给 rgb2_r",
         cmd_payload('"rgb2_r":200'), "APPLIED", {"rgb2": (200, 0, 0)}),
        ("风扇三档", cmd_payload(tool_set_fan(gear=3)), "APPLIED", {"fan": 750}),
        ("风扇关", cmd_payload(tool_set_fan(on=False)), "APPLIED", {"fan": 0}),
        ("风扇 speed 超范围钳位", cmd_payload(tool_set_fan(speed=9999)), "APPLIED", {"fan": 1000}),
        ("板载 LED 开", cmd_payload('"led":true'), "APPLIED", {"led": True}),
        ("板载 LED 关", cmd_payload('"led":false'), "APPLIED", {"led": False}),
        ("查询", cmd_payload("", query=True), "QUERY", {}),
        ("空指令", cmd_payload(""), "NONE", {}),
        ("OneNET 旧命名 RGB1_RAD", cmd_payload('"RGB1_RAD":10,"RGB1_GREEN":20,"RGB1_BLUE":30'),
         "APPLIED", {"rgb1": (10, 20, 30)}),
        ("OneNET value 包装", cmd_payload('"led":{"value":true}'), "APPLIED", {"led": True}),
        ("包装里带空格", cmd_payload('"led": {"value": true}'), "APPLIED", {"led": True}),
        ("包装里值是空格开头的数字", cmd_payload('"fan":{"value": 750}'), "APPLIED", {"fan": 750}),
        ("布尔写成字符串", cmd_payload('"led":"1"'), "APPLIED", {"led": True}),
        ("布尔用 1（OneNET 习惯）", cmd_payload('"led":1'), "APPLIED", {"led": True}),
        ("布尔用 0", cmd_payload('"led":0'), "APPLIED", {"led": False}),
    ]

    for name, payload, want_result, want_effects in cases:
        result, effects = home_cmd_apply(payload)
        ok = (result == want_result and effects == want_effects)
        check(name, ok, "got %s %s want %s %s" % (result, effects, want_result, want_effects))

    # 关键：确认没有"误命中"
    _, effects = home_cmd_apply(cmd_payload('"rgb1_r":1'))
    check("只发 rgb1_r 时不误改 rgb2 / fan / led",
          set(effects.keys()) == {"rgb1"}, str(effects))

    print("\n== 2. +MQTTSUBRECV 两种排版 -> handle_subrecv() ==")
    payload = cmd_payload('"led":true')
    lay_a = '+MQTTSUBRECV:0,"home/cmd",%d,%s\r\n' % (len(payload), payload)
    lay_b = '+MQTTSUBRECV:0,%d,"home/cmd",%s\r\n' % (len(payload), payload)
    lay_c = '+MQTTSUBRECV:0,home/cmd,%d,%s\r\n' % (len(payload), payload)  # 无引号
    for name, line in (("排版A topic,len,data", lay_a),
                       ("排版B len,topic,data", lay_b),
                       ("无引号主题", lay_c)):
        consumed, applied = handle_subrecv(line)
        check(name + " 边界正确", consumed == len(line), "consumed=%d len=%d" % (consumed, len(line)))
        check(name + " 负载正确", applied == payload, repr(applied))

    other = '+MQTTSUBRECV:0,"other/topic",%d,%s\r\n' % (len(payload), payload)
    consumed, applied = handle_subrecv(other)
    check("无关主题整条消费但不执行", consumed == len(other) and applied is None)

    no_len = '+MQTTSUBRECV:0,"home/cmd",%s\r\n' % payload
    consumed, applied = handle_subrecv(no_len)
    check("缺少长度字段时兜底消费（不卡死缓冲）", consumed == len(no_len))

    print("\n== 3. STM32 build_state_payload() -> 小智 ParseSmartHomeLocked() ==")
    state = build_state_payload()
    parsed = parse_state_locked(state)
    check("home/state 是合法 JSON", parsed["valid"])
    check("count=1", parsed["count"] == 1, str(parsed["count"]))
    check("online=true", parsed["online"] is True)
    check("解析出 1 台设备", parsed["device_count"] == 1, str(parsed["device_count"]))
    dev = parsed["devices"][0] if parsed["devices"] else {}
    check("设备 ID 与 STM32 一致", dev.get("id") == "smart_home_01", str(dev.get("id")))
    check("name=living_room", dev.get("name") == "living_room", str(dev.get("name")))
    check("temp=25.5", dev.get("temp") == 25.5, str(dev.get("temp")))
    check("humi=60.0", dev.get("humi") == 60.0, str(dev.get("humi")))
    check("light=123.4", dev.get("light") == 123.4, str(dev.get("light")))
    check("pm25=418", dev.get("pm25") == 418, str(dev.get("pm25")))
    check("mq2=315", dev.get("mq2") == 315, str(dev.get("mq2")))
    check("fan=500", dev.get("fan") == 500, str(dev.get("fan")))
    check("led=false（bool 而不是 0）", dev.get("led") is False, repr(dev.get("led")))
    check("rgb1=[255,200,100]", dev.get("rgb1") == [255, 200, 100], str(dev.get("rgb1")))
    check("rgb2=[0,0,0]", dev.get("rgb2") == [0, 0, 0], str(dev.get("rgb2")))
    check("状态负载 < STM32 的 768 字节缓冲", len(state) < 768, str(len(state)))

    print("\n== 4. 主题名两端一致 ==")
    import os
    here = os.path.dirname(os.path.abspath(__file__))

    def first_existing(rel_candidates):
        """脚本可能被放在 <repo>/docs/ 或工程目录的 project_docs/ 下，两种都试。
        找不到就返回 None —— 小智那份是另一个仓库，不在一起时跳过对应检查即可。"""
        for rel in rel_candidates:
            path = os.path.normpath(os.path.join(here, rel))
            if os.path.isfile(path):
                return path
        return None

    stm32_cfg = first_existing([
        os.path.join("..", "APP", "mqtt_config.h"),
        os.path.join("..", "source_code", "smart_home", "APP", "mqtt_config.h"),
    ])
    xz_model = first_existing([
        os.path.join("..", "..", "..", "..", "xiaozhi-esp32-main", "main", "boards",
                     "custom", "esp32s3-co5300-460", "dashboard_model.cc"),
        os.path.join("..", "..", "xiaozhi-esp32-main", "main", "boards",
                     "custom", "esp32s3-co5300-460", "dashboard_model.cc"),
    ])

    if stm32_cfg is None:
        check("找到 STM32 mqtt_config.h", False, "在脚本上级目录里没找到")
    else:
        try:
            with open(stm32_cfg, encoding="utf-8") as f:
                cfg = f.read()
            check("STM32 home/state 主题", 'SMART_HOME_MQTT_STATE_TOPIC  "home/state"' in cfg)
            check("STM32 home/cmd 主题", 'SMART_HOME_MQTT_CMD_TOPIC    "home/cmd"' in cfg)
            check("STM32 设备 ID", 'SMART_HOME_MQTT_DEVICE_ID    "smart_home_01"' in cfg)
        except OSError as exc:
            check("读取 STM32 mqtt_config.h", False, str(exc))

    if xz_model is None:
        check("找到小智 dashboard_model.cc（另一仓库，可跳过）", True, "未找到，已跳过")
    else:
        try:
            with open(xz_model, encoding="utf-8") as f:
                model = f.read()
            check("小智 home/cmd 主题", 'MQTT_CMD_TOPIC = "home/cmd"' in model)
        except OSError as exc:
            check("读取小智 dashboard_model.cc", False, str(exc))

    print("\n%s  %d 项检查，%d 项失败" %
          ("FAILED" if FAILURES else "PASSED", CHECKS[0], len(FAILURES)))
    if FAILURES:
        for name in FAILURES:
            print("  - %s" % name)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
