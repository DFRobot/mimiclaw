# desktop_pet

桌面宠状态推送 Skill - 将多 Agent 系统状态推送到桌面宠服务端，显示实时状态动画。

## 功能

- 监控每个 agent 状态变化，推送到桌宠服务端
- 支持 5 种 agent 状态：idle/researching/waiting/working/error
- 线程安全，支持多 agent 同时调用
- POST 异常安全，不影响主程序

## 支持的状态

| 状态 | 动画 | 说明 |
|------|------|------|
| `idle` | sleep | 空闲/待命 |
| `researching` | thinking | 调研/思考 |
| `waiting` | look_around | 等待 |
| `working` | typing | 工作/处理 |
| `error` | alarm | 错误/报警 |

## 安装方法

### 方式一：通过 ClawHub 安装（推荐）

```bash
 clawhub install desktop_pet
```

### 方式二：手动安装

1. 将本目录复制到你的 OpenClaw skills 目录：

```bash
cp -r desktop_pet ~/.openclaw/skills/
```

2. 确保已安装依赖：

```bash
pip install requests
```

## 使用方法

### 方式一：作为 Skill 调用

在你的 Agent 系统中，将 `desktop_pet` 作为 skill 添加到配置中。

### 方式二：直接导入 Python 模块

```python
import sys
sys.path.insert(0, "~/.openclaw/skills/desktop_pet")

from desktop_pet_skill import DesktopPet

# 创建桌宠实例
pet = DesktopPet()

# 推送单一 agent 状态
pet.push("太子", "working", "正在整理资料")

# 批量推送多个 agent 状态
pet.push_multiple([
    {"name": "太子", "status": "working", "message": "正在写代码"},
    {"name": "中书省", "status": "researching", "message": "正在调研"},
])
```

### 方式三：通过 push_helper 模块

```python
import sys
sys.path.insert(0, "~/.openclaw/skills/desktop_pet")

from push_helper import push

# 推送单一状态
push("太子", "working", "正在整理资料")
push("中书省", "idle", "待命中")

# 批量推送
from push_helper import push_multiple
push_multiple([
    {"name": "太子", "status": "working", "message": "写代码中"},
    {"name": "中书省", "status": "idle", "message": "待命中"},
])
```

## 配置

### 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `DESKTOP_PET_SERVER` | http://127.0.0.1:80 | 生产服务器地址 |
| `DESKTOP_PET_TEST_SERVER` | http://127.0.0.1:80 | 测试服务器地址 |

### 服务器地址设置

```python
# 方式1：环境变量
import os
os.environ["DESKTOP_PET_SERVER"] = "http://192.168.1.100:80"

# 方式2：代码动态设置
pet = DesktopPet()
pet.set_server("http://192.168.1.100:80")
```

## API

### DesktopPet 类

#### `__init__(server=None, test_server=None)`

初始化桌宠推送类。

```python
pet = DesktopPet(server="http://192.168.1.100:80")
```

#### `push(name, status, message="") -> bool`

推送单一 agent 状态。

```python
pet.push("太子", "working", "正在整理资料")
```

#### `push_multiple(agents) -> bool`

批量推送多个 agent 状态。

```python
pet.push_multiple([
    {"name": "太子", "status": "working", "message": "写代码中"},
    {"name": "中书省", "status": "idle", "message": "待命中"},
])
```

#### `set_server(server)`

动态更新服务器地址。

```python
pet.set_server("http://192.168.1.100:80")
```

#### `set_test_mode(enable=True)`

切换测试模式。

```python
pet.set_test_mode(True)  # 切换到测试服务器
pet.set_test_mode(False)  # 切换回生产服务器
```

### push_helper 模块

#### `push(name, status, message="", use_test=False)`

推送单一状态。

```python
push("太子", "working", "正在整理资料")
```

#### `push_multiple(agents, use_test=False)`

批量推送多个状态。

```python
push_multiple([
    {"name": "太子", "status": "working", "message": "写代码中"},
])
```

## SOUL.md 修改方法

如果你的 Agent 系统使用了 SOUL.md 配置文件（如太子 Agent），需要在对应的 Agent SOUL.md 中添加 desktop_pet 状态同步规则。

### 示例：太子的 SOUL.md 修改

在 SOUL.md 的 `## Desktop Pet 状态同步规则` 部分添加：

```markdown
## Desktop Pet 状态同步规则

> 🚨 每次状态变化时，必须调用 push_helper 同步到桌宠！

### 调用方式
```bash
# 接到任务
python3 ~/.openclaw/skills/desktop_pet/push_helper.py 太子 working "正在整理旨意"

# 等待审批
python3 ~/.openclaw/skills/desktop_pet/push_helper.py 太子 waiting "等待门下省审批"

# 任务完成
python3 ~/.openclaw/skills/desktop_pet/push_helper.py 太子 idle "待命中"

# 报错
python3 ~/.openclaw/skills/desktop_pet/push_helper.py 太子 error "任务执行失败"
```

### 状态 → 动画映射

| 状态 | 桌宠动画 |
|------|---------|
| `idle` | sleep |
| `researching` | thinking |
| `waiting` | look_around |
| `working` | typing |
| `error` | alarm |
```

## HEARTBEAT.md 修改方法

心跳检查中集成 desktop_pet 状态监控，在 HEARTBEAT.md 中添加相应的监控任务。

### 示例：添加心跳监控任务

在 HEARTBEAT.md 中添加：

```markdown
## 定期任务

### Agent 状态监控

每 5 分钟检查所有 Agent 状态，如有异常推送到桌宠：

```bash
python3 ~/.openclaw/skills/desktop_pet/push_helper.py 太子 working "心跳检查"
```

## 文件结构

```
desktop_pet/
├── README.md              # 本说明文件
├── SKILL.md               # Skill 定义文件
├── desktop_pet_skill.py   # 主模块（DesktopPet 类）
└── push_helper.py         # 辅助模块（push/push_multiple 函数）
```

## 依赖

- Python 3.7+
- requests

## 许可证

MIT License