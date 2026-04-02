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

## 配置文件

配置文件可能会改变，在python脚本中要读取一下这个文件，使用最新的post端点。 配置存储在 `~/.openclaw/runtime/desktop_pet.json`：

```json
{
  "endpoint": "http://192.168.0.113/status"
}
```

### 命令行管理

```bash
# 查看当前端点
python3 ~/.openclaw/skills/desktop_pet/push_helper.py get

# 只改IP（保留 /status 端点）
python3 ~/.openclaw/skills/desktop_pet/push_helper.py set --ip 192.168.1.138

# 改IP+端口
python3 ~/.openclaw/skills/desktop_pet/push_helper.py set --ip 192.168.1.138 --port 8080

# 直接设置完整端点
python3 ~/.openclaw/skills/desktop_pet/push_helper.py set --endpoint http://192.168.1.138:80/status
```

## 支持的状态

| 状态 | 动画 | 说明 |
|------|------|------|
| `idle` | sleep | 空闲/待命 |
| `researching` | thinking | 调研/思考 |
| `waiting` | look_around | 等待 |
| `working` | typing | 工作/处理 |
| `error` | alarm | 错误/报警 |

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

### set_endpoint(ip=None, port=None, full_endpoint=None)

设置桌宠服务器端点。

### get_endpoint()

获取当前配置的端点。

## 线程安全

多线程同时调用 push 不会冲突，使用 threading.Lock 保证安全。