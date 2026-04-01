# C语言版GIF显示库使用指南

## 概述

已将C++版本的lvgl_gif转换为纯C语言实现，现在可以在C语言的board.c中直接使用。

## 文件结构

```
display/lvgl_display/gif/
├── gifdec.c/h           # GIF解码器（C语言）
├── lvgl_gif.c/h        # GIF动画控制（C语言版本）
└── lvgl_gif_example.c/h # 使用示例
```

## 主要变化

### C++版本 → C语言版本

| C++版本 | C语言版本 | 说明 |
|---------|-----------|------|
| `LvglGif` 类 | `lvgl_gif_t` 结构体 | GIF对象 |
| `new LvglGif()` | `lvgl_gif_create()` | 创建GIF对象 |
| `~LvglGif()` | `lvgl_gif_destroy()` | 销毁GIF对象 |
| `Start()` | `lvgl_gif_start()` | 启动动画 |
| `Stop()` | `lvgl_gif_stop()` | 停止动画 |
| `Pause()` | `lvgl_gif_pause()` | 暂停动画 |
| `Resume()` | `lvgl_gif_resume()` | 恢复动画 |
| `IsPlaying()` | `lvgl_gif_is_playing()` | 检查播放状态 |
| `IsLoaded()` | `lvgl_gif_is_loaded()` | 检查加载状态 |
| `image_dsc()` | `lvgl_gif_get_image_dsc()` | 获取图像描述符 |
| `width()` | `lvgl_gif_get_width()` | 获取宽度 |
| `height()` | `lvgl_gif_get_height()` | 获取高度 |

## 基本使用方法

### 1. 包含头文件

```c
#include "display/lvgl_display/gif/lvgl_gif.h"
```

### 2. 准备GIF数据

```c
// 方式1：使用嵌入式二进制数据
extern const uint8_t example_gif_start[] asm("_binary_example_gif_start");
extern const uint8_t example_gif_end[] asm("_binary_example_gif_end");

// 方式2：从文件读取（需要SPIFFS）
uint8_t* gif_data = read_file_from_spiffs("/spiffs/example.gif");
size_t gif_size = get_file_size("/spiffs/example.gif");
```

### 3. 创建GIF对象

```c
lv_img_dsc_t gif_dsc = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC },
    .data = example_gif_start,
    .data_size = example_gif_end - example_gif_start
};

lvgl_gif_t* gif = lvgl_gif_create(&gif_dsc);
if (!gif || !lvgl_gif_is_loaded(gif)) {
    ESP_LOGE("TAG", "Failed to load GIF");
    return;
}
```

### 4. 创建LVGL图像对象

```c
lv_obj_t* gif_obj = lv_image_create(parent);
lv_image_set_src(gif_obj, lvgl_gif_get_image_dsc(gif));
lv_obj_align(gif_obj, LV_ALIGN_CENTER, 0, 0);
```

### 5. 启动动画

```c
lvgl_gif_start(gif);
```

### 6. 控制动画

```c
// 暂停动画
lvgl_gif_pause(gif);

// 恢复动画
lvgl_gif_resume(gif);

// 停止动画
lvgl_gif_stop(gif);

// 检查播放状态
if (lvgl_gif_is_playing(gif)) {
    ESP_LOGI("TAG", "GIF is playing");
}
```

### 7. 清理资源

```c
// 删除LVGL对象
lv_obj_del(gif_obj);

// 销毁GIF对象
lvgl_gif_destroy(gif);
```

## 完整示例

### 在board.c中集成

```c
#include "display/lvgl_display/gif/lvgl_gif.h"

// 声明GIF数据
extern const uint8_t example_gif_start[] asm("_binary_example_gif_start");
extern const uint8_t example_gif_end[] asm("_binary_example_gif_end");

static lvgl_gif_t* current_gif = NULL;

static void create_demo_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);

    // 创建标题
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "K10 DISPLAY\nGIF Animation Demo");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x0000FF), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

    // 创建GIF
    lv_img_dsc_t gif_dsc = {
        .header = { .magic = LV_IMAGE_HEADER_MAGIC },
        .data = example_gif_start,
        .data_size = example_gif_end - example_gif_start
    };

    current_gif = lvgl_gif_create(&gif_dsc);
    if (current_gif && lvgl_gif_is_loaded(current_gif)) {
        lv_obj_t* gif_obj = lv_image_create(scr);
        lv_image_set_src(gif_obj, lvgl_gif_get_image_dsc(current_gif));
        lv_obj_align(gif_obj, LV_ALIGN_CENTER, 0, 0);
        
        // 启动动画
        lvgl_gif_start(current_gif);
        
        ESP_LOGI("DEMO", "GIF loaded: %dx%d", 
                  lvgl_gif_get_width(current_gif), 
                  lvgl_gif_get_height(current_gif));
    } else {
        ESP_LOGE("DEMO", "Failed to load GIF");
    }

    // 创建控制按钮
    lv_obj_t *btn_start = lv_button_create(scr);
    lv_obj_set_size(btn_start, 100, 40);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    
    lv_obj_t *btn_start_label = lv_label_create(btn_start);
    lv_label_set_text(btn_start_label, "Start");
    lv_obj_center(btn_start_label);

    lv_obj_t *btn_stop = lv_button_create(scr);
    lv_obj_set_size(btn_stop, 100, 40);
    lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    
    lv_obj_t *btn_stop_label = lv_label_create(btn_stop);
    lv_label_set_text(btn_stop_label, "Stop");
    lv_obj_center(btn_stop_label);
}

void lvgl_main(void)
{
    i2c_init();
    InitializeIoExpander();
    set_backlight(true);
    ili9341_init_spi();
    lvgl_setup();
    create_demo_ui();

    while (1)
    {
        uint32_t delay = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(delay > 5 ? delay : 5));
    }
}
```

## 高级功能

### 设置循环次数

```c
// 设置循环次数（0表示无限循环）
lvgl_gif_set_loop_count(gif, 3);  // 循环3次
```

### 设置循环延迟

```c
// 设置循环之间的延迟（毫秒）
lvgl_gif_set_loop_delay(gif, 1000);  // 每次循环间隔1秒
```

### 设置帧回调

```c
void frame_update_callback(void) {
    ESP_LOGI("TAG", "Frame updated");
}

// 设置回调函数
lvgl_gif_set_frame_callback(gif, frame_update_callback);
```

## 内存管理

### 内存需求

- GIF解码需要额外的内存用于帧缓冲
- 大尺寸GIF需要更多内存
- 建议使用PSRAM（如果可用）

### 内存优化

```c
// 检查GIF尺寸
uint16_t width = lvgl_gif_get_width(gif);
uint16_t height = lvgl_gif_get_height(gif);
ESP_LOGI("TAG", "GIF size: %dx%d", width, height);

// 如果GIF太大，考虑缩放或压缩
if (width > 320 || height > 240) {
    ESP_LOGW("TAG", "GIF too large, may cause memory issues");
}
```

## 调试技巧

### 检查加载状态

```c
if (!lvgl_gif_is_loaded(gif)) {
    ESP_LOGE("TAG", "GIF failed to load");
    // 检查数据是否有效
    ESP_LOGE("TAG", "GIF data: %p, size: %d", gif_data, gif_size);
}
```

### 监控播放状态

```c
void monitor_gif_status(void) {
    if (current_gif) {
        ESP_LOGI("TAG", "GIF playing: %s, loaded: %s",
                  lvgl_gif_is_playing(current_gif) ? "yes" : "no",
                  lvgl_gif_is_loaded(current_gif) ? "yes" : "no");
    }
}
```

## 常见问题

### 1. GIF不显示

**可能原因：**
- GIF数据无效
- 内存不足
- LVGL对象创建失败

**解决方法：**
```c
if (!gif || !lvgl_gif_is_loaded(gif)) {
    ESP_LOGE("TAG", "GIF load failed");
    // 检查数据指针和大小
}
```

### 2. 动画不播放

**可能原因：**
- 没有调用`lvgl_gif_start()`
- LVGL定时器未运行
- 内存不足

**解决方法：**
```c
// 确保启动了动画
lvgl_gif_start(gif);

// 确保LVGL定时器在运行
while (1) {
    uint32_t delay = lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(delay > 5 ? delay : 5));
}
```

### 3. 内存不足

**解决方法：**
- 使用更小的GIF文件
- 启用PSRAM
- 减少同时显示的GIF数量

## 编译配置

确保CMakeLists.txt包含必要的文件：

```cmake
idf_component_register(
    SRCS
        # ... 其他文件
        "display/lvgl_display/gif/gifdec.c"
        "display/lvgl_display/gif/lvgl_gif.c"
    INCLUDE_DIRS
        "."
        "display/lvgl_display/gif"
    REQUIRES
        # ... 其他依赖
        lvgl
)
```

## 总结

C语言版本的lvgl_gif提供了与C++版本相同的功能，但使用纯C语言实现，更适合在C语言项目中使用。所有API都遵循C语言命名规范，易于集成到现有代码中。