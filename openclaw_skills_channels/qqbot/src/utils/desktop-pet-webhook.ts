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
