# desktop_pet - 桌面宠状态推送 Skill

## 功能

将多 Agent 系统状态推送到桌面宠服务端，显示实时状态动画。

## 快速开始

```python
from skills.desktop_pet.push_helper import push

# 推送单一状态
push("太子", "working", "正在整理资料")
push("中书省", "idle", "待命中")
```

## 支持的状态

| 状态 | 动画 | 说明 |
|------|------|------|
| `idle` | sleep | 空闲/待命 |
| `researching` | thinking | 调研/思考 |
| `waiting` | look_around | 等待 |
| `working` | typing | 工作/处理 |
| `error` | alarm | 错误/报警 |

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `DESKTOP_PET_SERVER` | http://127.0.0.1:80 | 生产服务器 |
| `DESKTOP_PET_TEST_SERVER` | http://127.0.0.1:80 | 测试服务器 |

## API

### push(name, status, message="", use_test=False)

推送单一 agent 状态。

### push_multiple(agents, use_test=False)

批量推送多个 agent 状态。

**agents 格式：**
```python
[
    {"name": "太子", "status": "working", "message": "写代码中"},
    {"name": "中书省", "status": "idle", "message": "待命中"},
]
```

## 线程安全

多线程同时调用 push 不会冲突，使用 threading.Lock 保证安全。