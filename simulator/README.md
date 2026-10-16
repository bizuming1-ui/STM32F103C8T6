# 浏览器模拟器（Browser Simulator）

在网页里运行 **本工程同一份 C 代码**（编译成 WebAssembly），显示与实物一致。

## 立即使用
```bash
python3 -m http.server 8000 --directory simulator
# 浏览器打开 http://127.0.0.1:8000/index.html
```

## 目录
| 路径 | 说明 |
|---|---|
| `index.html` | 页面（开发板、按键、LED、蜂鸣器、状态栏） |
| `ui.js` | 页面逻辑：120 Hz 步进固件、OLED 显存直读、按键/音频 |
| `sim.js` | 硬件模型：TIM2 tick、GPIOA IDR、LED/蜂鸣器引脚、DS18B20 |
| `sim_hal.c` | 供固件调用的模拟器桩（寄存器内存块、Sim_Yield 等） |
| `hal/stm32f10x.h` | wasm 用的标准外设库替换头（Keil 不受影响） |
| `hal/OLED.h` | 大小写桥接（Keil 上文件名为 `OELD.h`） |
| `build.sh` | 用 zig 编译 `fw_sim.wasm` |
| `fw_sim.wasm` | 已编译好的固件镜像 |
| `test/` | 回归测试：73 项行为断言、PNG 渲染、页面冒烟测试 |

## 常用命令
```bash
bash build.sh                 # 重新编译固件 wasm（需要 zig 或 pip install ziglang）
bash test/run_all.sh          # 全套验证
bash test/keil_syntax_check.sh  # Keil(ARMCC) 代码路径的语法/告警检查
```
