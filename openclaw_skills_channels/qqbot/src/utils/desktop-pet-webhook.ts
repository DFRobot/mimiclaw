import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

/**
 * 向桌面宠物/状态服务上报：每次发送前读取 ~/.openclaw/runtime/desktop_pet.json
 * 中的 endpoint 字段作为 POST 地址。未配置或读取失败时不发起请求。
 */

const DESKTOP_PET_CONFIG_FILE = path.join(os.homedir(), ".openclaw", "runtime", "desktop_pet.json");
const MAX_MESSAGE_CHARS = 1800;

function truncateForWebhook(text: string): string {
  const s = text.replace(/\r\n/g, "\n").trim();
  if (s.length <= MAX_MESSAGE_CHARS) {
    return s;
  }
  return `${s.slice(0, MAX_MESSAGE_CHARS)}…`;
}

function resolveStatusUrl(): string | null {
  try {
    const raw = fs.readFileSync(DESKTOP_PET_CONFIG_FILE, "utf8");
    const parsed = JSON.parse(raw) as { endpoint?: unknown };
    const endpoint = typeof parsed.endpoint === "string" ? parsed.endpoint.trim() : "";
    if (!endpoint) {
      return null;
    }
    return endpoint;
  } catch {
    return null;
  }
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
