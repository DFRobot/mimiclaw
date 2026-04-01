#!/usr/bin/env python3
"""
desktop_pet_skill.py - 多 Agent 系统状态监控桌宠推送 Skill

功能：
1. 监控每个 agent 状态变化，推送到桌宠服务端
2. 支持 5 种 agent 状态：idle/researching/waiting/working/error
3. 线程安全，支持多 agent 同时调用
4. POST 异常安全，不影响主程序

使用示例：
    from desktop_pet_skill import DesktopPet
    
    pet = DesktopPet()
    pet.push("太子", "working", "正在整理资料")
"""

import os
import json
import logging
import threading
from typing import Optional

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class DesktopPet:
    """桌宠状态推送类 - 监控多 Agent 系统状态变化"""
    
    # 状态动画映射表
    STATUS_ANIMATION_MAP = {
        "idle": "sleep",          # 空闲 -> 睡觉
        "researching": "thinking",  # 调研 -> 思考
        "waiting": "look_around",  # 等待 -> 环顾
        "working": "typing",       # 工作 -> 打字
        "error": "alarm",          # 错误 -> 警报
    }
    
    def __init__(self, server: Optional[str] = None, test_server: Optional[str] = None):
        """
        初始化桌宠推送类
        
        Args:
            server: 生产环境服务器地址，默认从环境变量读取
            test_server: 测试环境服务器地址，默认从环境变量读取
        """
        # 优先读取环境变量，支持动态变化
        self.server = server or os.environ.get("DESKTOP_PET_SERVER", "http://127.0.0.1:80")
        self.test_server = test_server or os.environ.get("DESKTOP_PET_TEST_SERVER", "http://127.0.0.1:80")
        
        # 线程锁，保证多线程安全
        self._lock = threading.Lock()
        
        # 当前使用的服务器
        self._current_server = self.server
        
        logger.info(f"DesktopPet 初始化: server={self.server}, test_server={self.test_server}")
    
    def set_server(self, server: str):
        """动态更新服务器地址"""
        with self._lock:
            self.server = server
            self._current_server = server
        logger.info(f"服务器地址已更新: {server}")
    
    def set_test_mode(self, enable: bool = True):
        """
        切换测试模式
        
        Args:
            enable: True 切换到测试服务器，False 切换到生产服务器
        """
        with self._lock:
            self._current_server = self.test_server if enable else self.server
        logger.info(f"已切换到{'测试' if enable else '生产'}服务器: {self._current_server}")
    
    def get_animation(self, status: str) -> str:
        """
        获取状态对应的动画名称
        
        Args:
            status: agent 状态
            
        Returns:
            桌宠动画名称
        """
        return self.STATUS_ANIMATION_MAP.get(status, "idle")
    
    def push(self, name: str, status: str, message: str = "") -> bool:
        """
        推送 agent 状态到桌宠服务端
        
        Args:
            name: agent 名称
            status: agent 状态 (idle/researching/waiting/working/error)
            message: 状态消息（可选）
            
        Returns:
            True 推送成功，False 推送失败
        """
        import requests
        
        # 构建请求数据
        payload = {
            "agents": [
                {
                    "name": name,
                    "status": status,
                    "message": message,
                    "animation": self.get_animation(status)
                }
            ]
        }
        
        url = f"{self._current_server}/status"
        
        with self._lock:
            try:
                response = requests.post(
                    url,
                    json=payload,
                    timeout=3,  # 3秒超时
                    headers={"Content-Type": "application/json"}
                )
                response.raise_for_status()
                logger.info(f"✅ 状态推送成功: {name} - {status} - {message}")
                return True
            except requests.exceptions.RequestException as e:
                logger.warning(f"❌ 状态推送失败: {name} - {status} - {e}")
                return False
            except Exception as e:
                logger.error(f"❌ 未知错误: {e}")
                return False
    
    def push_multiple(self, agents: list) -> bool:
        """
        批量推送多个 agent 状态
        
        Args:
            agents: agent 列表，每项为 {"name": xxx, "status": xxx, "message": xxx}
            
        Returns:
            True 全部成功，False 任一失败
        """
        import requests
        
        # 转换状态为动画
        for agent in agents:
            agent["animation"] = self.get_animation(agent.get("status", "idle"))
        
        payload = {"agents": agents}
        url = f"{self._current_server}/status"
        
        with self._lock:
            try:
                response = requests.post(
                    url,
                    json=payload,
                    timeout=3,
                    headers={"Content-Type": "application/json"}
                )
                response.raise_for_status()
                logger.info(f"✅ 批量推送成功: {len(agents)} 个 agent")
                return True
            except requests.exceptions.RequestException as e:
                logger.warning(f"❌ 批量推送失败: {e}")
                return False
            except Exception as e:
                logger.error(f"❌ 未知错误: {e}")
                return False


# ========== 示例调用 ==========

if __name__ == "__main__":
    # 创建桌宠实例
    pet = DesktopPet()
    
    # 示例1: 推送单一 agent 状态
    print("\n--- 示例1: 推送单一状态 ---")
    pet.push("太子", "working", "正在整理资料")
    
    # 示例2: 推送 idle 状态
    pet.push("中书省", "idle", "待命中")
    
    # 示例3: 推送 researching 状态
    pet.push("尚书省", "researching", "正在调研技术方案")
    
    # 示例4: 推送 waiting 状态
    pet.push("门下省", "waiting", "等待尚书省回复")
    
    # 示例5: 推送 error 状态
    pet.push("太子", "error", "任务执行失败")
    
    # 示例6: 批量推送
    print("\n--- 示例6: 批量推送 ---")
    pet.push_multiple([
        {"name": "太子", "status": "working", "message": "正在写代码"},
        {"name": "中书省", "status": "researching", "message": "正在调研"},
        {"name": "尚书省", "status": "waiting", "message": "等待审批"},
    ])
    
    # 示例7: 切换测试模式
    print("\n--- 示例7: 切换测试模式 ---")
    pet.set_test_mode(True)
    pet.push("测试", "working", "测试模式推送")
    pet.set_test_mode(False)
    
    # 示例8: 动态修改服务器
    print("\n--- 示例8: 动态修改服务器 ---")
    pet.set_server("http://192.168.1.100:80")
    pet.push("太子", "idle", "切换服务器测试")
    
    print("\n✅ 所有示例执行完成！")
