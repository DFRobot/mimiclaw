# desktop_pet

桌面宠状态推送 Skill - 将多 Agent 系统状态推送到桌面宠服务端，显示实时状态动画。

## 功能

- 监控每个 agent 状态变化，推送到桌宠服务端
- 支持 5 种 agent 状态：idle/researching/waiting/working/error
- 线程安全，支持多 agent 同时调用
- POST 异常安全，不影响主程序

## 支持的状态

| 状态          | 动画        | 说明      |
| ------------- | ----------- | --------- |
| `idle`        | sleep       | 空闲/待命 |
| `researching` | thinking    | 调研/思考 |
| `waiting`     | look_around | 等待      |
| `working`     | typing      | 工作/处理 |
| `error`       | alarm       | 错误/报警 |

## 安装方法

### 通过 下载文件 安装

```bash
 mkdir ~/.openclaw/skills/desktop_pet
 cd ~/.openclaw/skills/desktop_pet
 wget https://github.com/DFRobot/mimiclaw/blob/main/openclaw_skills_channels/SKILL.md
 wget https://github.com/DFRobot/mimiclaw/blob/main/openclaw_skills_channels/desktop_pet_skill.py
 wget https://github.com/DFRobot/mimiclaw/blob/main/openclaw_skills_channels/push_helper.py
```

确保已安装依赖：

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

| 变量                      | 默认值              | 说明           |
| ------------------------- | ------------------- | -------------- |
| `DESKTOP_PET_SERVER`      | http://127.0.0.1:80 | 生产服务器地址 |
| `DESKTOP_PET_TEST_SERVER` | http://127.0.0.1:80 | 测试服务器地址 |

### 服务器地址设置

```python
# 方式1：环境变量
import os
os.environ["DESKTOP_PET_SERVER"] = "http://192.168.1.100"

# 方式2：代码动态设置
pet = DesktopPet()
pet.set_server("http://192.168.1.100")
```

## API

### DesktopPet 类

#### `__init__(server=None, test_server=None)`

初始化桌宠推送类。

```python
pet = DesktopPet(server="http://192.168.1.100")
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
pet.set_server("http://192.168.1.100")
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

| 状态          | 桌宠动画    |
| ------------- | ----------- |
| `idle`        | sleep       |
| `researching` | thinking    |
| `waiting`     | look_around |
| `working`     | typing      |
| `error`       | alarm       |

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


---

## QQBot Webhook 集成（可选）

如果需要让 QQBot 插件在收到/发送消息时推送到桌面宠，可以在原始 qqbot 插件中集成 webhook。

### 改动概览

| 文件 | 改动类型 | 说明 |
|------|----------|------|
| `src/utils/desktop-pet-webhook.ts` | 新增 | webhook 通知模块 |
| `src/channel.ts` | 修改 | 发送消息后通知 |
| `src/gateway.ts` | 修改 | 上线/离线/收/发消息通知 |
| `src/proactive.ts` | 修改 | 主动消息通知 |

### 步骤 1：创建 webhook 模块

新建文件 `src/utils/desktop-pet-webhook.ts`：

```typescript
/**
 * 向桌面宠物/状态服务上报：读取环境变量 DESKTOP_PET_SERVER（主机或完整 URL，不含路径），
 * POST 到 {base}/status，Content-Type: application/json。
 * 未设置环境变量时不发起请求。
 */

const ENV_DESKTOP_PET_SERVER = "DESKTOP_PET_SERVER";
const MAX_MESSAGE_CHARS = 1800;

function truncateForWebhook(text: string): string {
  const s = text.replace(/\r\n/g, "\n").trim();
  if (s.length <= MAX_MESSAGE_CHARS) {
    return s;
  }
  return `${s.slice(0, MAX_MESSAGE_CHARS)}…`;
}

function resolveStatusUrl(): string | null {
  const raw = process.env[ENV_DESKTOP_PET_SERVER]?.trim();
  if (!raw) {
    return null;
  }
  let base = raw.replace(/\/+$/, "");
  if (!/^https?:\/\//i.test(base)) {
    base = `http://${base}`;
  }
  return `${base}/status`;
}

type DesktopPetChannelEntry =
  | { name: string; status: "online" | "offline" }
  | { name: string; in_message: string }
  | { name: string; out_message: string };

function postDesktopPet(body: { channels: DesktopPetChannelEntry[] }): void {
  const url = resolveStatusUrl();
  if (!url) {
    return;
  }

  void fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }).catch((err) => {
    console.debug(`[qqbot:desktop-pet] POST ${url} failed:`, err);
  });
}

export function notifyDesktopPetQQOnline(): void {
  postDesktopPet({ channels: [{ name: "QQ", status: "online" }] });
}

export function notifyDesktopPetQQOffline(): void {
  postDesktopPet({ channels: [{ name: "QQ", status: "offline" }] });
}

/** 收到 QQ 消息 → JSON 字段 in_message（仅用户可见正文） */
export function notifyDesktopPetQQInMessage(inMessage: string): void {
  postDesktopPet({ channels: [{ name: "QQ", in_message: truncateForWebhook(inMessage) }] });
}

/** 机器人发往 QQ 的文案 → JSON 字段 out_message（无地址前缀） */
export function notifyDesktopPetQQOutbound(outMessage: string): void {
  postDesktopPet({ channels: [{ name: "QQ", out_message: truncateForWebhook(outMessage) }] });
}
```

### 步骤 2：修改 channel.ts

在文件开头导入部分（约第 15 行）添加：

```typescript
import { notifyDesktopPetQQOutbound } from "./utils/desktop-pet-webhook.js";
```

找到 `onSend` 方法内发送文本后的位置（约 254 行），添加：

```typescript
if (!result.error) {
  notifyDesktopPetQQOutbound(text ?? "");
}
```

找到发送媒体后的位置（约 271 行），添加：

```typescript
if (!result.error) {
  notifyDesktopPetQQOutbound(
    [text?.trim() && `说明:${text}`, mediaUrl && `媒体:${mediaUrl}`].filter(Boolean).join(" ") || "[媒体]",
  );
}
```

### 步骤 3：修改 gateway.ts

在文件开头导入部分（约第 16-21 行）添加：

```typescript
import {
  notifyDesktopPetQQOnline,
  notifyDesktopPetQQOffline,
  notifyDesktopPetQQInMessage,
  notifyDesktopPetQQOutbound,
} from "./utils/desktop-pet-webhook.js";
```

在以下位置添加调用：

| 位置 | 调用的函数 | 说明 |
|------|------------|------|
| 约 503 行 `startListening` | `notifyDesktopPetQQOffline()` | 启动失败时通知离线 |
| 约 879 行收到消息后 | `notifyDesktopPetQQInMessage(userContent)` | 收到用户消息 |
| 约 1403-1407 行发送队列表 | `notifyDesktopPetQQOutbound(sendQueueSummary)` | 发送队列内容 |
| 约 1454 行确认发送 | `notifyDesktopPetQQOutbound(confirmText)` | 确认文本 |
| 约 1685-1687 行结构化载荷 | `notifyDesktopPetQQOutbound([结构化载荷:...])` | 结构化消息 |
| 约 1991-1998 行回复 | `notifyDesktopPetQQOutbound(outboundDesc)` | 最终回复 |
| 约 2144 行成功登录 | `notifyDesktopPetQQOnline()` | 登录成功 |

### 步骤 4：修改 proactive.ts

在文件开头导入部分（约第 76 行）添加：

```typescript
import { notifyDesktopPetQQOutbound } from "./utils/desktop-pet-webhook.js";
```

在以下位置添加调用（约 356-358 行）：

```typescript
notifyDesktopPetQQOutbound(imageUrl ? `[图片] ${text}` : text);
```

在约 502-504 行添加：

```typescript
notifyDesktopPetQQOutbound(text);
```

### 环境变量配置

启动 OpenClaw 时设置环境变量：

```bash
export DESKTOP_PET_SERVER=http://192.168.1.100:80
# 或
export DESKTOP_PET_SERVER=http://your-server-ip
```

### 通知 JSON 格式

推送到桌面宠的 JSON 格式：

```json
{
  "channels": [
    { "name": "QQ", "status": "online" },
    { "name": "QQ", "status": "offline" },
    { "name": "QQ", "in_message": "用户发送的消息" },
    { "name": "QQ", "out_message": "机器人回复的消息" }
  ]
}
```


## 许可证

MIT License