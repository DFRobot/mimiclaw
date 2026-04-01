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

# 状态动画映射
STATUS_ANIMATION_MAP = {
    "idle": "sleep",
    "researching": "thinking", 
    "waiting": "look_around",
    "working": "typing",
    "error": "alarm",
}

# 获取服务器地址
def _get_server() -> str:
    """获取桌宠服务器地址，支持环境变量"""
    server = os.environ.get("DESKTOP_PET_SERVER", "http://127.0.0.1:80")
    # 自动补全 http:// 前缀
    if not server.startswith("http"):
        server = "http://" + server
    return server


def _get_test_server() -> str:
    """获取测试服务器地址"""
    server = os.environ.get("DESKTOP_PET_TEST_SERVER", "http://127.0.0.1:80")
    if not server.startswith("http"):
        server = "http://" + server
    return server


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


# ========== 测试入口 ==========

if __name__ == "__main__":
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
