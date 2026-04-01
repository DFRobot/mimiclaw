# Feishu Notice Message Management

Manage notice messages displayed on the local page.

## When to use
When user asks to:
- Add a new notice message to the local page
- Delete a specific notice message
- Clear all notice messages
- Show or update notification content

## How to use
1. **Add a new notice message**: use notice_add
   - Maximum 3 messages, oldest is removed when full
   - Example: notice_add {"message": "Meeting at 3pm"}

2. **Delete a notice message**: use notice_del
   - Position is 1-based (1=first, 2=second, 3=third)
   - Example: notice_del {"index": 1}

3. **Clear all messages**: use notice_clear
   - Removes all notice messages at once
   - Example: notice_clear {}

## Examples
User: "添加一条通知：下午3点开会"
→ notice_add {"message": "下午3点开会"}
→ "OK: added notice message (id: 1)"
→ "已添加通知：下午3点开会"

User: "删除第一条通知"
→ notice_del {"index": 1}
→ "OK: deleted notice message at position 1"
→ "已删除第1条通知"

User: "清空所有通知"
→ notice_clear {}
→ "OK: cleared all notice messages"
→ "已清空所有通知"

User: "添加多个通知"
→ notice_add {"message": "任务1：完成报告"}
→ notice_add {"message": "任务2：审核代码"}
→ notice_add {"message": "任务3：部署上线"}
→ "OK: added notice message (id: 1)"
→ "OK: added notice message (id: 2)"
→ "OK: added notice message (id: 3)"
→ "已添加3条通知，最多保留3条"
