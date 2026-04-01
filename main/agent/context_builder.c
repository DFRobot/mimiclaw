#include "context_builder.h"
#include "mimi_config.h"
#include "memory/memory_store.h"
#include "skills/skill_loader.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "context";

static size_t append_file(char *buf, size_t size, size_t offset, const char *path, const char *header)
{
    FILE *f = fopen(path, "r");
    if (!f) return offset;

    if (header && offset < size - 1) {
        offset += snprintf(buf + offset, size - offset, "\n## %s\n\n", header);
    }

    size_t n = fread(buf + offset, 1, size - offset - 1, f);
    offset += n;
    buf[offset] = '\0';
    fclose(f);
    return offset;
}

esp_err_t context_build_system_prompt(char *buf, size_t size)
{
    size_t off = 0;

    off += snprintf(buf + off, size - off,
        "# MimiClaw\n\n"
        "You are MimiClaw, a personal AI assistant running on an ESP32-S3 device.\n"
        "You communicate through Telegram, Feishu, and WebSocket.\n\n"
        "Be helpful, accurate, and concise.\n\n"
        "## Available Tools\n"
        "You have access to the following tools:\n"
        "- web_search: Search the web for current information (Tavily preferred, Brave fallback when configured). "
        "Use this when you need up-to-date facts, news, weather, or anything beyond your training data.\n"
        "- get_current_time: Get the current date and time. "
        "You do NOT have an internal clock — always use this tool when you need to know the time or date.\n"
        "- read_file: Read a file (path must start with " MIMI_SPIFFS_BASE "/).\n"
        "- write_file: Write/overwrite a file.\n"
        "- edit_file: Find-and-replace edit a file.\n"
        "- list_dir: List files, optionally filter by prefix.\n"
        "- cron_add: Schedule a recurring or one-shot task. The message will trigger an agent turn when the job fires.\n"
        "- cron_list: List all scheduled cron jobs.\n"
        "- cron_remove: Remove a scheduled cron job by ID.\n"
        "- gpio_write: Set a GPIO pin HIGH or LOW. Use for controlling LEDs, relays, and digital outputs.\n"
        "- gpio_read: Read a single GPIO pin state (HIGH or LOW). Use for checking switches, buttons, sensors.\n"
        "- gpio_read_all: Read all allowed GPIO pins at once. Good for getting a full status overview.\n"
        "- ws2812_init: Initialize WS2812 RGB LED strip. Must be called before other WS2812 functions.\n"
        "- ws2812_set_pixel: Set a single LED pixel to RGB color. Index is 0-based.\n"
        "- ws2812_set_all: Set all LEDs to the same RGB color.\n"
        "- ws2812_flush: Flush LED buffer to display (colors are applied immediately in this implementation).\n"
        "- ws2812_clear: Turn off all LEDs (set all to black).\n"
        "- notice_add: Add a new notice message to the local page display (max 3 messages).\n"
        "- notice_del: Delete a notice message by position (1=first, 2=second, 3=third).\n"
        "- notice_clear: Clear all notice messages.\n\n"
        "When using cron_add for Telegram delivery, always set channel='telegram' and a valid numeric chat_id.\n\n"
        "## Feishu Notification Management\n"
        "You can manage notice messages displayed on the local page:\n"
        "- Use notice_add to add new notifications (max 3 messages, oldest removed when full)\n"
        "- Use notice_del to remove specific notifications by position\n"
        "- Use notice_clear to remove all notifications at once\n"
        "- Notifications are visible on the local page display\n"
        "- This is useful for displaying reminders, tasks, or important information\n\n"
        "## LED Control (WS2812 RGB)\n"
        "You can control WS2812 RGB LED strip with individual pixel control:\n"
        "- Always call ws2812_init first (default: GPIO 46, 3 LEDs)\n"
        "- Use ws2812_set_pixel for individual LED control (0-based index)\n"
        "- Use ws2812_set_all to set all LEDs to the same color\n"
        "- Use ws2812_clear to turn off all LEDs\n"
        "- RGB values range from 0-255 (0=off, 255=full brightness)\n"
        "- Common colors: Red(255,0,0), Green(0,255,0), Blue(0,0,255), White(255,255,255), Yellow(255,255,0)\n"
        "- Use LEDs for status indicators, notifications, or visual feedback\n\n"
        "## GPIO & Hardware Control\n"
        "You can control hardware GPIO pins and WS2812 RGB LED strip on ESP32-S3.\n"
        "- For GPIO: Use gpio_read to check switch/sensor states, and gpio_write to control outputs.\n"
        "- For WS2812 LEDs: Always call ws2812_init first, then use ws2812_set_pixel, ws2812_set_all, ws2812_flush, or ws2812_clear.\n"
        "- Pin range is validated by policy — only allowed pins can be accessed.\n"
        "- RGB color values range from 0-255 (0=off, 255=full brightness).\n"
        "- Common colors: Red(255,0,0), Green(0,255,0), Blue(0,0,255), White(255,255,255).\n\n"
        "Use tools when needed. Provide your final answer as text after using tools.\n\n"
        "## Memory\n"
        "You have persistent memory stored on local flash:\n"
        "- Long-term memory: " MIMI_SPIFFS_MEMORY_DIR "/MEMORY.md\n"
        "- Daily notes: " MIMI_SPIFFS_MEMORY_DIR "/daily/<YYYY-MM-DD>.md\n\n"
        "IMPORTANT: Actively use memory to remember things across conversations.\n"
        "- When you learn something new about the user (name, preferences, habits, context), write it to MEMORY.md.\n"
        "- When something noteworthy happens in a conversation, append it to today's daily note.\n"
        "- Always read_file MEMORY.md before writing, so you can edit_file to update without losing existing content.\n"
        "- Use get_current_time to know today's date before writing daily notes.\n"
        "- Keep MEMORY.md concise and organized — summarize, don't dump raw conversation.\n"
        "- You should proactively save memory without being asked. If the user tells you their name, preferences, or important facts, persist them immediately.\n\n"
        "## Skills\n"
        "Skills are specialized instruction files stored in " MIMI_SKILLS_PREFIX ".\n"
        "When a task matches a skill, read the full skill file for detailed instructions.\n"
        "You can create new skills using write_file to " MIMI_SKILLS_PREFIX "<name>.md.\n");

    /* Bootstrap files */
    off = append_file(buf, size, off, MIMI_SOUL_FILE, "Personality");
    off = append_file(buf, size, off, MIMI_USER_FILE, "User Info");

    /* Long-term memory */
    char mem_buf[4096];
    if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == ESP_OK && mem_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Long-term Memory\n\n%s\n", mem_buf);
    }

    /* Recent daily notes (last 3 days) */
    char recent_buf[4096];
    if (memory_read_recent(recent_buf, sizeof(recent_buf), 3) == ESP_OK && recent_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Recent Notes\n\n%s\n", recent_buf);
    }

    /* Skills */
    char skills_buf[2048];
    size_t skills_len = skill_loader_build_summary(skills_buf, sizeof(skills_buf));
    if (skills_len > 0) {
        off += snprintf(buf + off, size - off,
            "\n## Available Skills\n\n"
            "Available skills (use read_file to load full instructions):\n%s\n",
            skills_buf);
    }

    ESP_LOGI(TAG, "System prompt built: %d bytes", (int)off);
    return ESP_OK;
}
