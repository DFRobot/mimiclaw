#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_lcd_ili9341.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "font.h"

#include "page_conf.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include "pic_decoder/gif/lvgl_gif.h"
#include "pic_decoder/jpg/jpeg_to_image.h"

static const char *TAG = "K10_DEMO";

/*********************
 * 配置常量
 *********************/
#define LCD_HOST SPI3_HOST
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define LCD_BPP 16
#define LCD_RES_HOR 240
#define LCD_RES_VER 320

#define PIN_NUM_MOSI GPIO_NUM_21
#define PIN_NUM_MISO GPIO_NUM_NC
#define PIN_NUM_CLK GPIO_NUM_12
#define PIN_NUM_CS GPIO_NUM_14
#define PIN_NUM_DC GPIO_NUM_13
#define PIN_NUM_RST GPIO_NUM_NC

#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_47
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_48
#define BACKLIGHT_IO_PIN IO_EXPANDER_PIN_NUM_0

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_io_expander_handle_t io_expander = NULL;
static i2c_master_bus_handle_t i2c_bus = NULL;

/*********************
 * 硬件初始化 (I2C & 扩展器)
 *********************/
static void i2c_init(void)
{
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = (i2c_port_t)1,
        .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));
}

static void InitializeIoExpander()
{
    esp_io_expander_new_i2c_tca95xx_16bit(i2c_bus, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000, &io_expander);
    esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, 1); // 开启背光和电源
}

static void set_backlight(bool enable)
{
    esp_io_expander_set_level(io_expander, BACKLIGHT_IO_PIN, enable ? 1 : 0);
}

/*********************
 * LCD 驱动初始化
 *********************/
static void ili9341_init_spi(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .max_transfer_sz = LCD_RES_HOR * LCD_RES_VER * 2, // 确保足够大
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .color_space = ESP_LCD_COLOR_SPACE_BGR, // 尝试切换为 RGB 修正颜色
        .bits_per_pixel = LCD_BPP,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // 修正方向和反色
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

/*********************
 * LVGL 刷新回调 (含字节交换)
 *********************/
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_width(area) * lv_area_get_height(area));

    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void lv_tick_task(void *arg)
{
    lv_tick_inc(5);
}

/*********************
 * LVGL 系统初始化
 *********************/
static void lvgl_setup(void)
{
    lv_init();

    // 分配缓冲区
    size_t buf_size = LCD_RES_HOR * 80 * sizeof(lv_color16_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    assert(buf1);

    lv_display_t *disp = lv_display_create(LCD_RES_HOR, LCD_RES_VER);
    lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_user_data(disp, panel_handle);

    // 定时器
    const esp_timer_create_args_t timer_args = {.callback = lv_tick_task, .name = "lv_tick"};
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 5000)); // 5ms
}

/*********************
 * UI & Main
 *********************/
// LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk);
// extern const lv_font_t lv_font_source_han_sans_sc_16_cjk;

static void lvgl_task(void *arg)
{
    ESP_LOGI("LVGL", "LVGL task started");
    
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void lvgl_main(void)
{    
    i2c_init();
    InitializeIoExpander();
    set_backlight(true);
    ili9341_init_spi();

    lvgl_setup();
    page_local_init();

#define MIMI_LVGL_POLL_STACK (10 * 1024)
#define MIMI_LVGL_POLL_PRIO 3
#define MIMI_LVGL_POLL_CORE 0
static TaskHandle_t s_lvgl_task = NULL;


    StackType_t *xStack = (StackType_t *)heap_caps_aligned_alloc(
        16, MIMI_LVGL_POLL_STACK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!xStack) {
        ESP_LOGE(TAG, "PSRAM stack alloc failed (%u bytes)", (unsigned)MIMI_LVGL_POLL_STACK);
        return;
    }

    static StaticTask_t xTaskBuffer;
    s_lvgl_task = xTaskCreateStaticPinnedToCore(
        lvgl_task,
        "lvgl",
        MIMI_LVGL_POLL_STACK,
        NULL,
        MIMI_LVGL_POLL_PRIO,
        xStack,
        &xTaskBuffer,
        MIMI_LVGL_POLL_CORE);

    if (s_lvgl_task == NULL) {
        heap_caps_free(xStack);
        ESP_LOGE(TAG, "xTaskCreateStaticPinnedToCore failed");
        return;
    }
}