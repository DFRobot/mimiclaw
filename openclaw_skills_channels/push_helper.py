#!/usr/bin/env python3
"""
桌面宠状态推送模块 - 供多 Agent 系统调用

用法：
    from skills.desktop_pet.push_helper import push
    
    # 直接调用
    push("太子", "working", "正在整理资料")
    push("中书省", "idle", "待命中")
"""

import os
import sys
import json
import logging
import threading
from typing import Optional

# 确保能找到 skills 模块
_WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _WORKSPACE not in sys.path:
    sys.path.insert(0, _WORKSPACE)

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# 配置文件路径
CONFIG_FILE = os.path.expanduser("~/.openclaw/runtime/desktop_pet.json")

# 状态动画映射
STATUS_ANIMATION_MAP = {
    "idle": "sleep",
    "researching": "thinking", 
    "waiting": "look_around",
    "working": "typing",
    "error": "alarm",
}


def _load_config() -> dict:
    """从 JSON 配置文件加载桌宠设置"""
    default_config = {
        "endpoint": "http://127.0.0.1:80/status"
    }
    try:
        if os.path.exists(CONFIG_FILE):
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                config = json.load(f)
                logger.info(f"[desktop_pet] 已加载配置: {config}")
                return config
        else:
            # 文件不存在，创建默认配置
            _save_config(default_config)
            return default_config
    except Exception as e:
        logger.warning(f"[desktop_pet] 配置加载失败: {e}，使用默认配置")
        return default_config


def _save_config(config: dict) -> bool:
    """保存配置到 JSON 文件"""
    try:
        os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2, ensure_ascii=False)
        logger.info(f"[desktop_pet] 配置已保存: {config}")
        return True
    except Exception as e:
        logger.error(f"[desktop_pet] 配置保存失败: {e}")
        return False


def set_endpoint(ip: str = None, port: int = None, full_endpoint: str = None) -> bool:
    """
    设置桌宠服务器端点
    
    Args:
        ip: 新的 IP 地址（如 192.168.0.113），可选
        port: 新的端口号（如 80），可选
        full_endpoint: 完整端点 URL（如 http://192.168.0.113:80/status），可选
        
    Returns:
        True 设置成功，False 失败
    """
    config = _load_config()
    old_endpoint = config.get("endpoint", "http://127.0.0.1:80/status")
    
    # 解析旧的端点，获取 path（通常是 /status）
    old_path = "/status"
    if "://" in old_endpoint:
        old_path = "/" + old_endpoint.split("/", 3)[3] if len(old_endpoint.split("/", 3)) > 3 else "/status"
    
    if full_endpoint:
        # 直接设置完整端点
        config["endpoint"] = full_endpoint
    elif ip or port:
        # 解析当前端点
        # 格式: http://192.168.0.113:80/status
        import re
        match = re.match(r'(https?://)([^:/]+)(:\d+)?(.*)', old_endpoint)
        if match:
            scheme = match.group(1) or "http://"
            old_ip = match.group(2)
            old_port = match.group(3)
            path = match.group(4) or old_path
            
            # 使用新值或默认值（保持原有端口格式，默认80）
            new_ip = ip if ip else old_ip
            if port:
                new_port = f":{port}"
            elif old_port:
                new_port = old_port
            else:
                new_port = ":80"  # 默认端口80
            
            config["endpoint"] = f"{scheme}{new_ip}{new_port}{path}"
    
    return _save_config(config)


def get_endpoint() -> str:
    """获取当前配置的端点"""
    config = _load_config()
    return config.get("endpoint", "http://127.0.0.1:80/status")


# 获取服务器地址（从配置文件读取）
def _get_server() -> str:
    """获取桌宠服务器地址，从配置文件读取"""
    config = _load_config()
    endpoint = config.get("endpoint", "http://127.0.0.1:80/status")
    # 提取基础 URL（去掉 /status 后缀）
    if endpoint.endswith("/status"):
        return endpoint[:-7]  # 去掉 /status
    elif endpoint.endswith("/status"):
        return endpoint.rsplit("/", 1)[0]
    return endpoint


def _get_test_server() -> str:
    """获取测试服务器地址（暂不支持，从配置读取）"""
    # 暂时返回空，后续可以扩展支持测试服务器
    config = _load_config()
    return config.get("test_endpoint", "")


# 线程锁
_push_lock = threading.Lock()


def push(name: str, status: str, message: str = "", use_test: bool = False) -> bool:
    """
    推送 agent 状态到桌宠服务端
    
    Args:
        name: agent 名称
        status: agent 状态 (idle/researching/waiting/working/error)
        message: 状态消息（可选）
        use_test: 是否使用测试服务器
        
    Returns:
        True 推送成功，False 推送失败
    """
    import requests
    
    animation = STATUS_ANIMATION_MAP.get(status, "idle")
    
    payload = {
        "agents": [
            {
                "name": name,
                "status": status,
                "message": message,
                "animation": animation
            }
        ]
    }
    
    success = True
    
    # 同时向生产服务器和测试服务器发送
    servers = [_get_server()]
    if _get_test_server():
        servers.append(_get_test_server())
    
    for server in servers:
        url = f"{server}/status"
        with _push_lock:
            try:
                response = requests.post(
                    url,
                    json=payload,
                    timeout=3,
                    headers={"Content-Type": "application/json"}
                )
                response.raise_for_status()
                logger.info(f"[desktop_pet] ✅ {name}: {status} - {message} -> {server}")
            except requests.exceptions.RequestException as e:
                logger.warning(f"[desktop_pet] ❌ {name}: {status} - {e} -> {server}")
                success = False
            except Exception as e:
                logger.error(f"[desktop_pet] ❌ 未知错误: {e} -> {server}")
                success = False
    
    return success


def push_multiple(agents: list, use_test: bool = False) -> bool:
    """
    批量推送多个 agent 状态
    
    Args:
        agents: [{"name": xxx, "status": xxx, "message": xxx}, ...]
        use_test: 是否使用测试服务器
        
    Returns:
        True 全部成功，False 任一失败
    """
    import requests
    
    # 添加动画字段
    for agent in agents:
        agent["animation"] = STATUS_ANIMATION_MAP.get(agent.get("status", "idle"), "idle")
    
    payload = {"agents": agents}
    
    success = True
    
    # 同时向生产服务器和测试服务器发送
    servers = [_get_server()]
    if _get_test_server():
        servers.append(_get_test_server())
    
    for server in servers:
        url = f"{server}/status"
        with _push_lock:
            try:
                response = requests.post(
                    url,
                    json=payload,
                    timeout=3,
                    headers={"Content-Type": "application/json"}
                )
                response.raise_for_status()
                logger.info(f"[desktop_pet] ✅ 批量推送 {len(agents)} 个 agent -> {server}")
            except requests.exceptions.RequestException as e:
                logger.warning(f"[desktop_pet] ❌ 批量推送失败: {e} -> {server}")
                success = False
            except Exception as e:
                logger.error(f"[desktop_pet] ❌ 未知错误: {e} -> {server}")
                success = False
    
    return success


# ========== 命令行入口 ==========

if __name__ == "__main__":
    import argparse
    
    # 自定义解析器，兼容太子SOUL.md中的调用方式: python3 push_helper.py 太子 working "消息"
    # 检测是否是旧调用方式（第一个参数不是 push/set/get）
    import sys
    if len(sys.argv) > 1 and sys.argv[1] not in ["push", "set", "get", "--help", "-h"]:
        # 旧方式: python3 push_helper.py 太子 working "消息"
        name = sys.argv[1] if len(sys.argv) > 1 else None
        status = sys.argv[2] if len(sys.argv) > 2 else None
        message = sys.argv[3] if len(sys.argv) > 3 else None
        if name and status:
            push(name, status, message or "")
            sys.exit(0)
    
    parser = argparse.ArgumentParser(description="桌面宠状态推送工具")
    parser.add_argument("action", nargs="?", choices=["push", "set", "get"], default="push",
                        help="操作: push(推送状态), set(设置端点), get(查看当前端点)")
    parser.add_argument("--name", "-n", help="Agent 名称")
    parser.add_argument("--status", "-s", choices=["idle", "researching", "waiting", "working", "error"],
                        help="Agent 状态")
    parser.add_argument("--message", "-m", help="状态消息")
    parser.add_argument("--ip", help="设置新的 IP 地址")
    parser.add_argument("--port", "-p", type=int, help="设置新的端口")
    parser.add_argument("--endpoint", "-e", help="设置完整端点 URL")
    
    args = parser.parse_args()
    
    if args.action == "get":
        # 查看当前端点
        endpoint = get_endpoint()
        print(f"当前端点: {endpoint}")
        
    elif args.action == "set":
        # 设置端点
        success = set_endpoint(ip=args.ip, port=args.port, full_endpoint=args.endpoint)
        if success:
            print(f"✅ 端点已更新: {get_endpoint()}")
        else:
            print("❌ 端点设置失败")
            
    elif args.action == "push":
        # 推送状态
        if args.name and args.status:
            push(args.name, args.status, args.message or "")
        else:
            print("--- 测试桌面宠推送 ---")
            
            # 测试单推
            print("\n1. 推送 working 状态")
            push("太子", "working", "正在整理资料")
            
            print("\n2. 推送 idle 状态")
            push("中书省", "idle", "待命中")
            
            print("\n3. 推送 researching 状态")
            push("尚书省", "researching", "正在调研方案")
            
            print("\n4. 批量推送")
            push_multiple([
                {"name": "太子", "status": "working", "message": "写代码中"},
                {"name": "中书省", "status": "researching", "message": "查资料中"},
            ])
            
            print("\n✅ 测试完成")
