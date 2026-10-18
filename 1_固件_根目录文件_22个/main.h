/* V15.2 STM32F103C8T6 interfaces. ARMCC5, 72 MHz. No new SysTick handler.
 * Buzzer circuit: PA8 -> R5 (10k) -> Q1 PNP base; Q1 emitter -> 3V3,
 * Q1 collector -> active buzzer(+); active buzzer(-) -> GND.
 * PNP is INVERTING: PA8 HIGH = OFF, PA8 LOW = ON.
 * At reset PA8 is high impedance. Add a base-to-3V3 pull-up resistor for
 * guaranteed silence before firmware starts. TIM1/AF is not used. */
#ifndef APP_MAIN_H
#define APP_MAIN_H
#include "stm32f10x.h"
#include <stdint.h>
/* V29: OLED 驱动头文件的**正确拼写**是 OLED.h (曾经被写成 OELD.h, 导致 Keil
 * 里 10 个 .c 全部报 "cannot open source input file")。
 *
 * 文件名契约 (非常重要):
 *   - 这个头文件必须叫 OLED.h, 与 OLED.c 放在同一个目录;
 *   - 该目录要加进 Keil 的 Options for Target -> C/C++ -> Include Paths;
 *   - Windows/Keil 的文件系统**不区分大小写**, 所以老工程里若磁盘上仍叫
 *     OELD.h, 下面这行 #include "OLED.h" 一样能命中它 —— 不改名也能编过。
 *
 * 下面这段 __has_include 探测: 支持的编译器 (gcc/clang/ARMCC6) 在头文件缺失时
 * 会直接给出中文提示; ARMCC5 不认识这个运算符, 自动走 #else 的普通包含,
 * 行为和以前完全一致。 */
#if defined(__has_include)
#  if __has_include("OLED.h")
#    include "OLED.h"
#  else
#    error "找不到 OLED.h —— 请把 OLED.h 与 OLED.c 放在同一目录, 并把该目录加入 Keil 的 Include Paths (老工程里叫 OELD.h 也可以, Windows 不区分大小写)"
#    define APP_OLED_MISSING 1u
#  endif
#else
#  include "OLED.h"            /* ARMCC5 路径 (与历史行为一致) */
#endif
#if !defined(APP_OLED_H) && !defined(APP_OLED_MISSING)
#error "OLED.h 里没有 APP_OLED_H 护栏 —— 这个文件不是本工程的 OLED 驱动头文件, 请用仓库里的 OLED.h"
#endif
#define TRANS_MS 1000u
#define KEY_PORT GPIOA
#define KEY_PIN_PA4 ((uint16_t)0x0010)
#define KEY_PIN_PA5 ((uint16_t)0x0020)
#define KEY_PIN_PA6 ((uint16_t)0x0040)
#define KEY_PIN_PA7 ((uint16_t)0x0080)
#define KEY_ALL_PINS ((uint16_t)0x00F0)
#define KEY_EVENT_PA4 0x01u
#define KEY_EVENT_PA5 0x02u
#define KEY_EVENT_PA6 0x04u
#define KEY_EVENT_PA7 0x08u
#define KEY_FIRST_PIN 4u
#define KEY_COUNT 4u
#define KEY_RCC_MASK RCC_APB2ENR_IOPAEN
#define KEY_DEBOUNCE_MS 20u
#define KEY_REPEAT_DELAY_MS 2000u
#define KEY_REPEAT_RATE_MS 100u
/* V19: 两级快速连按 —— 按住满 2 秒后 100ms/步, 再持续 2 秒(共 4 秒)后提速到 40ms/步 */
#define KEY_REPEAT_FAST_DELAY_MS 4000u
#define KEY_REPEAT_FAST_RATE_MS 40u
#define KEY_LONG_MS 2000u        /* V18:长按判定 2 秒, PA4 用它返回时间界面 */
#define PA5_BLOCK_MS 1000u       /* 取消倒计时后 1 秒内 PA5 防误触 */
#define PA4_BLOCK_MS 1000u       /* 回到时间界面后 1 秒内 PA4 防误触 */
/* V21 stopwatch (秒表界面) */
#define SW_MAX_MS 5999999u        /* 99:59.999 —— 分最高 99, 到顶自动停住 */
#define SW_LAP_MAX 100u           /* 环形缓冲 100 圈: 满 100 后覆盖最旧一圈 */
#define SW_LAP_KEEP_MS 3000u      /* 翻找历史后 3 秒无操作, 光标回到"跟随最新" */
#define SW_LAP_REPEAT_MS 450u     /* 长按 PA7: 450ms 后开始连翻 */
#define SW_LAP_REPEAT_RATE 150u   /* 连翻间隔 */
#define KEY_ACTIVE_HIGH 1u
#if KEY_ACTIVE_HIGH
#define KEY_PRESSED_LEVEL 1u
#define KEY_RELEASED_LEVEL 0u
#else
#define KEY_PRESSED_LEVEL 0u
#define KEY_RELEASED_LEVEL 1u
#endif
#define DS_PORT GPIOA
#define DS_PIN ((uint16_t)0x0008)
#define DS_PIN_INDEX 3u
#define DS_RCC_MASK RCC_APB2ENR_IOPAEN
#define DS_CYC_PER_US 72u
#define DS_CONV_MS 1000u
#define DS_WAIT_MS 800u
#define SCENE_CX 89
#define SCENE_CY 31
#define SCENE_COS 250
#define SCENE_SIN 56
#define SCENE_RADIUS_Q 256
#define SCENE_DIG_W 18
#define SCENE_DIG_H 24
#define SCENE_DIG_PITCH 20
#define SCENE_HOUR_X -13
#define SCENE_HOUR_Y -24
#define SCENE_MIN_X -7
#define SCENE_MIN_Y 4
#define SCENE_SEC_X -39
#define SCENE_SEC_Y -8
#define SCENE_INFO_W 43
#define SCENE_INFO_H 48
#define LED_PORT GPIOC
#define LED_PIN ((uint16_t)0x2000)
#define LED_PIN_INDEX 13u
#define LED_RCC_MASK RCC_APB2ENR_IOPCEN
#define BUZZER_PORT GPIOA
#define BUZZER_PIN ((uint16_t)0x0100)
#define BUZZER_PIN_INDEX 8u
#define BUZZER_RCC_MASK RCC_APB2ENR_IOPAEN
#define BUZZER_MODE_PWM 0u           /* Active buzzer via PNP; PA8 stays GPIO */
#define BUZZER_ACTIVE_HIGH 0u        /* PNP base LOW = sound, HIGH = silence */
#define BUZZER_TIM_PSC 71u           /* Only for passive-buzzer PWM builds */
#define BUZZER_SILENT_LEVEL_IS_LOW 0u /* This circuit needs HIGH for silence */
#define ALERT_BLINK_HALF_MS 250u
#define ALERT_LAUNCH_MS 800u         /* 烟花升空哨声时长 */
#define ALERT_BOOM_MS 1000u          /* 烟花爆炸噪声时长, 之后进入旋律循环 */
#if (BUZZER_MODE_PWM > 1) || (BUZZER_ACTIVE_HIGH > 1)
#error "Buzzer configuration must be 0 or 1"
#endif

/* Animation interfaces share this already-configured project include path.
 * Keep one definition here; animation.h and yuexin_cat_frames.h are wrappers.
 * The extern declarations do not allocate the animation data. */
#define YCAT_WIDTH         56u
#define YCAT_HEIGHT        56u
#define YCAT_BITMAP_BYTES  392u
#define YCAT_FRAME_COUNT   396u
#define YCAT_STREAM_BYTES  31871u
#define YCAT_DANCE_MS      10000u
#define YCAT_ROLL_MS        5000u
#define YCAT_ROLL_ENABLED  1u /* yuexin_cat_roll.c 提供95帧真实打滚视频帧 */
#define YCAT_ANGRY_ENABLED 1u /* yuexin_cat_angry.c 提供149帧真实生气视频 */
/* Roll and angry play REAL recorded frames of the same YuexinCat character
 * (re-packed into YcatVideoAsset), so their outline and line weight always
 * match the dance stage. Roll: 95 x 82x64 frames from the user's uploaded
 * reference player, drawn 1:1 centred at x=23 (see animation.c). */
#ifndef YCAT_ROLL_VIDEO_ENABLED
#define YCAT_ROLL_VIDEO_ENABLED 1u /* REAL 95-frame 82x64 animation from the reference code */
#endif
#ifndef YCAT_ANGRY_VIDEO_ENABLED
#define YCAT_ANGRY_VIDEO_ENABLED 1u /* REAL 149-frame 64x64 animation from the reference code */
#endif
#define YCAT_DRAW_X        36u
#define YCAT_DRAW_Y         4u
#define YCAT_BASE_MS       33u
#define YCAT_PING_PONG      1u
#define YCAT_MAX_CATCHUP   30u
extern const uint8_t  g_cat_frame0[YCAT_BITMAP_BYTES];
extern const uint16_t g_delta_offsets[YCAT_FRAME_COUNT + 1u];
extern const uint8_t  g_delta_counts[YCAT_FRAME_COUNT];
extern const uint8_t  g_delta_stream[YCAT_STREAM_BYTES];
/* V24 静态上下文算术编码 (见 animation.c 的 ArBit):
 *   P(bit==0) 的 8 位定标 (1..255), 由 tools/gen_ycat.py 对三段动画的增量流
 *   统计得到并烧进 Flash; 解码只查表, 没有自适应状态, 因此任意一帧都能单独
 *   解码 (倒放/乒乓不改变任何输出)。三份动画共享同一组表 (2158 B)。 */
#define YCAT_M_CLASSES 16u
extern const uint8_t g_ycat_m_lead[10];
extern const uint8_t g_ycat_m_suf[100];
extern const uint8_t g_ycat_m_val[YCAT_M_CLASSES * 256u];
/* Compact video assets use the same page-major 1-bit frames as the dance
 * decoder. Original videos and generated C/H are kept in the project folder. */
typedef struct {
    uint16_t frameCount;
    uint16_t streamBytes;
    uint16_t bitmapBytes;
    uint8_t width;
    uint8_t height;
    const uint8_t *frame0;
    const uint16_t *offsets;
    /* counts[] is uint8_t when countsWide==0, uint16_t when countsWide==1. */
    const void *counts;
    const uint8_t *stream;
    /* 0 = counts[] are uint8_t (max 255 changed bytes per frame);
     * 1 = counts[] are uint16_t, for 64x64 sources that can exceed 255. */
    uint16_t countsWide;
} YcatVideoAsset;

/* Legacy optional angry fallback pose type. */
typedef struct { uint16_t angle; uint16_t scale; int8_t dy; uint8_t mark; } YcatPose;
#if YCAT_ROLL_ENABLED
extern const uint16_t g_ycat_roll_frame_count;
#if YCAT_ROLL_VIDEO_ENABLED
extern const YcatVideoAsset g_ycat_roll_video;
#else
extern const YcatPose g_ycat_roll_pose[];
#endif
#endif
#if YCAT_ANGRY_ENABLED
extern const uint16_t g_ycat_angry_frame_count;
#if YCAT_ANGRY_VIDEO_ENABLED
extern const YcatVideoAsset g_ycat_angry_video;
#else
extern const YcatPose g_ycat_angry_pose[];
extern const uint8_t  g_ycat_angry_base[YCAT_BITMAP_BYTES];
extern const uint8_t  g_ycat_angry_marks[4][YCAT_BITMAP_BYTES];
#endif
#endif
void     CatAnim_Reset(uint32_t nowMs);
uint8_t  CatAnim_Tick(uint32_t nowMs);
void     CatAnim_Draw(void);
uint16_t CatAnim_FrameIndex(void);

typedef struct {
    uint16_t year;
    uint8_t month, day, weekday;
    int16_t tempTenths;
    uint8_t tempValid;
} SceneInfo;
/* V21: one recorded lap of the stopwatch (秒表分圈) */
typedef struct {
    uint32_t lapMs;        /* 本圈用时, 毫秒 */
    uint32_t totalMs;      /* 按下分圈那一刻的总用时, 毫秒 */
    uint16_t index;        /* 圈号: 1,2,3,... (环形缓冲里保留原编号) */
} SwLapT;
extern volatile uint32_t system_tick;
void Clock_SetTime(uint8_t h, uint8_t m, uint8_t s);
/* V26: date + time in one tick, so midnight can never rewrite the new date. */
void Clock_SetDateTime(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s);
void Key_Init(void);
uint8_t Key_Scan(uint32_t nowMs);
uint8_t Key_Repeat(void);
uint8_t Key_Long(void);          /* V18: 每个按键的长按(2s)事件, 每次按住只报一次 */
uint8_t Key_Release(void);       /* V18: 短按抬起事件(按住不足 2s 才产生) */
uint8_t Key_IsDown(void);
uint8_t Key_Level(void);
void LED_Init(void);
void LED_On(void);
void LED_Off(void);
void Buzzer_On(void);
void Buzzer_Off(void);
void Buzzer_Tone(uint16_t hz);
void Alert_Start(uint32_t nowMs);
void Alert_Stop(void);
void Alert_Update(uint32_t nowMs);
void DS18B20_Init(void);
void DS18B20_Update(uint32_t nowMs);
uint8_t DS18B20_IsValid(void);
int16_t DS18B20_GetTenths(void);
int32_t Sin14(uint16_t a);
int32_t Cos14(uint16_t a);
void Scene_BlackHole(uint32_t tMs, uint32_t msOfDay, const SceneInfo *info);
/* V18 settings screen: two icons only (铃声 left, 秒表 right) with continuous
 * rounded-square borders; the selected border blinks, the icon never blinks. */
void Scene_Menu(uint32_t tMs, uint8_t sel);
/* V19 countdown panel (铃声界面): 4 continuous rounded thin frames
 * [HH]:[MM]:[SS] [Start], small live clock top-right, selected frame blinks. */
void Scene_TimerPanelReset(void);   /* V20: snap the drum digits without a roll */
void Scene_TimerPanel(uint32_t tMs, uint32_t msOfDay, uint8_t h, uint8_t m, uint8_t s, uint8_t selected);
void Scene_TimerRunning(uint32_t msOfDay, uint32_t remainMs, uint32_t pct);
/* V21 stopwatch screen: big dial on the left, MM:SS.mmm on the top right and
 * the recorded laps (① ② ③ ... + time) underneath it. lapView = how many laps
 * the user scrolled back with PA7 (0 = following the newest lap). */
void Scene_StopwatchReset(void);   /* V21: 秒表场景下次绘制先整屏重画 */
void Scene_Stopwatch(uint32_t tMs, uint32_t swMs, const SwLapT *laps, uint16_t lapCount,
                     uint16_t lapHead, uint16_t lapView, uint8_t running);
void Scene_Compose(uint8_t a[OLED_H/8][OLED_W], uint8_t b[OLED_H/8][OLED_W], uint16_t p);
void Scene_AlarmBegin(uint32_t nowMs);
uint8_t Scene_Alarm(uint32_t nowMs); /* 1 = framebuffer changed */
/* Black-hole stroke font: tilted, collision-safe moving hour, paced by nowMs. */
/* ---- V23: 设置2(计算器/游戏) + 计算器界面 ---------------------------------
 * 键盘布局表定义在 main.c, 界面(Scene.c)与输入(main.c)共用同一张表 ——
 * 只有一处真值, 不会出现"画的和按的不是同一个键"。 */
#define CALC_EXPR_MAX 24
#define CK_NONE  0xFFu
#define CK_DOT   10u
#define CK_ADD   11u
#define CK_SUB   12u
#define CK_MUL   13u
#define CK_DIV   14u
#define CK_EQ    15u
#define CK_CLEAR 16u
#define CK_AC    17u
extern const uint8_t s_calcKeys[6][4];
void Scene_Menu2(uint32_t tMs, uint8_t sel);
/* V30 游戏: 选择界面 + 俄罗斯方块 */
void Scene_GameMenu(uint32_t tMs, uint8_t sel);
void Scene_TetrisReset(void);
void Scene_TetrisOvlReset(void);
void Scene_GameOverBegin(uint32_t nowMs);
void Scene_GameOver(uint32_t nowMs,uint32_t score,uint8_t sel);
void Scene_BigDigit(int32_t x,int32_t y,uint8_t d,uint8_t s);
void Scene_Tetris(uint32_t tMs, const uint16_t *board,
                  const uint8_t *cells, uint8_t ncell,
                  uint32_t score, const uint8_t *next, uint8_t nnext,
                  uint8_t paused, uint8_t count);
/* V26: 设置3 = 齿轮(年月日时间设置) / 访问(后续开发) */
#define MENU3_SEL_GEAR 0u
#define MENU3_SEL_MAG  1u
void Scene_SetMenu(uint32_t tMs, uint8_t sel);
/* V26 日期/时间编辑器: sel 0=年 1=月 2=日 3=时 4=分 5=秒 6=星期 */
void Scene_DateEditor(uint32_t tMs, uint16_t y, uint8_t mo, uint8_t d, uint8_t wd,
                      uint8_t hh, uint8_t mi, uint8_t ss, uint8_t sel);
void Scene_DateEditorReset(void);              /* 设置2: 计算器/游戏 */
void Scene_CalcReset(void);                              /* 首帧整屏重画 */
void Scene_Calc(uint32_t tMs, const char *expr, uint8_t cur,
                uint8_t selR, uint8_t selC, uint8_t isResult);
void Scene_DrawStrokeNum(int32_t cx, int32_t cy, uint8_t v, uint32_t nowMs);
#endif
