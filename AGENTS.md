# AGENTS.md — mGBA 龙芯蜂鸟板（2K0300）hummingbird 前端开发须知

本文件面向本仓库的 `hummingbird` 前端（`src/platform/hummingbird`），改代码前先读完本文档，尤其是交叉编译的 LSX 大坑与 `1024×600` 布局。

## 硬件与系统概况

| 项目 | 值 |
|---|---|
| SoC | Loongson 2K0300（蜂鸟），LA264 单核 @1GHz |
| 架构 | loongarch64（LA64 v1.0 基础指令集） |
| **CPU 扩展** | 仅 `cpucfg lam fpu crc32 lspw`；**没有 LSX/LASX 向量扩展** |
| 内存 | 369MB，**无 swap**（编译大工程别在板上进行） |
| 系统 | Alpine Linux 3.21.2，musl libc，内核 6.12.0.lsgd |
| 连接 | `ssh root@192.168.1.10`（known_hosts 只读会报警告，加 `-o StrictHostKeyChecking=no`） |

## 显示栈

- DRM：`/dev/dri/card0` + `renderD128`，RGB LCD 接在 `card0-DPI-1`（connected）
- fbdev：`/dev/fb0` = **1024×600, 32bpp XRGB8888 小端，stride 4096**（内存字节序 B,G,R,X）
- 默认**无 compositor**；weston 已安装（DRM 后端）但一般不跑
- Mesa 仅有 llvmpipe 软渲染（EGL/GLES 可用）；**无 GPU 加速、无 Vulkan**
- 没装 Xorg server
- 本项目 `hummingbird` 前端**直写 fbdev**（裸 `mmap`），不依赖 DRM/weston/X11

### fbcon 光标坑

内核 fbcon（vtcon1）会在我们写的帧缓冲之上异步叠画闪烁光标。
解法已内置：启动时对 `/dev/tty0` 发 `ioctl(KDSETMODE, KD_GRAPHICS)`（见 `src/platform/hummingbird/hb-fb.c:92` `hbHideConsoleCursor`）。
恢复由 `hbRestoreConsoleCursor` 发 `KD_TEXT`（`src/platform/hummingbird/hb-fb.c:104`），在 `src/platform/hummingbird/main.c:200` 退出时调用。
副作用：运行期间控制台文字不可见，重启恢复。

## 触屏与输入

- 触屏控制器：FocalTech **FT5x06**（"generic ft5x06 (82)"），I2C1 @ 0x38
- 设备节点：`/dev/input/event0`，MT 协议 B（`ABS_MT_SLOT` / `ABS_MT_TRACKING_ID` / `ABS_MT_POSITION_X/Y`），见 `src/platform/hummingbird/hb-touch.c:44`
- 坐标即面板坐标，1024×600 与 fb 一一对应，无需变换；支持最多 `HB_TOUCH_MAX_POINTS=5` 点（`src/platform/hummingbird/hb-touch.h:6`）
- 启动参数可覆盖：`--touch /dev/input/eventN`（`src/platform/hummingbird/main.c:45`），默认 `event0`
- 另有 PS/2 鼠标（`/dev/input/mice`）；无键盘
- 本项目裸读 evdev（24 字节 `input_event`），不依赖 libinput；`SYN_DROPPED` 时清空状态（`src/platform/hummingbird/hb-touch.c:77`）

## hummingbird 前端架构（`src/platform/hummingbird`）

这是 mGBA 的专用前端，只做一件事：把 `mCore` 的帧缓冲以整数缩放贴到 `1024×600` 屏中间，底部/两侧用触控按钮补齐。

### 模块清单

| 文件 | 职责 |
|---|---|
| `src/platform/hummingbird/main.c` | 参数解析、信号处理、mCore 生命周期、主循环与限帧 |
| `src/platform/hummingbird/hb-fb.h:8` / `hb-fb.c:19` | `HBFb` 结构体，`open/mmap` fbdev，`stridePx`，`hbFbRow`，`hbFbClear`，`KD_GRAPHICS` 光标抑制 |
| `src/platform/hummingbird/hb-touch.h:6` / `hb-touch.c:13` | `HBTouchPoint`，`hbTouchOpen/Close/Poll/Points`，MT-B 解析 |
| `src/platform/hummingbird/hb-ui.h:10` / `hb-ui.c:258` | `HBUI`/`HBButton`/`HBShape`，屏幕布局、SDF 按钮渲染、触控命中、`hbUiBlitGame` |
| `src/platform/hummingbird/CMakeLists.txt:7` | 定义 `mgba-hb` 可执行文件，链接 `libmgba` |
| `.cmake/zig-toolchain.cmake:1` / `.cmake/zigcc.sh:7` | LoongArch64 musl 交叉工具链（见下节） |

### 屏幕与布局参数（1024×600 基准）

`hb-fb.c:19` 动态读取 `fb_var_screeninfo` / `fb_fix_screeninfo`：
- 校验 `bits_per_pixel == 32`，`line_length >= xres*4`，否则拒绝（`hb-fb.c:42`）
- `width = vi.xres`, `height = vi.yres`, `stridePx = fi.line_length/4`，实测 `1024×600 stride 4096`
- `memLen = smem_len ?: line_length*yres_virtual`，`mmap(NULL, memLen, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)`（`hb-fb.c:59`），`hbFbRow` 以 `stridePx` 寻行（`hb-fb.h:24`）
- 像素格式：显存为 `XRGB8888` 小端，内存字节序 `B,G,R,X`；`hb-ui.c:415` blit 时做 `RGBA→BGRA` 交换：`0xFF000000 | (p&0xFF)<<16 | (p&0xFF00) | ((p>>16)&0xFF)`，`hb-fb.c:68` 启动日志打印 `width×height stride`

`hb-ui.c:258` `hbUiInit(screenW, screenH, gameW, gameH)` 的居中整数缩放：
- `gameW/gameH` 来自 `core->baseVideoSize`（GBA `240×160`，GB `160×144`，见 `main.c:105`）
- 可用区：`availW = screenW/2 - 32`（`1024→480`），`availH = screenH - 155`（`600→445`），为左右按钮与底部留白
- 整数倍 `gs`：`while (gameW*(gs+1) <= availW && gameH*(gs+1) <= availH) gs++`，`gw=gameW*gs`，`gh=gameH*gs`，`gx=(screenW-gw)/2`，`gy=6+(availH-gh)/2`（顶部 6px 边距）
- 日志 `main.c:131` 打印 `core platform、gw×gh、gs、gx/gy、fps`

触控按钮坐标（硬编码按 1024×600 设计，`hb-ui.c:273`）：
- `A(938,534) B(846,534) r38` 圆形；`SELECT(461,556) START(563,556) 42×16` 胶囊；`L(140,30) R(884,30) 58×17` 胶囊；十字 `150,500 rx60 ry25` 四方向共享同一 `HB_QUADPAD`（`hb-ui.h:11` `HB_CIRCLE/HB_PILL/HB_QUADPAD`）
- 形状 SDF 见 `hb-ui.c:45` `_shapeSdf`，预渲染 `sprUp/sprDown` 与 `quadScratch`（`hb-ui.c:173`），`hbUiDraw` 按 `pressed/drawn` 脏标记 `memcpy` 到 `hbFbRow`（`hb-ui.c:348`）

### 帧循环与按键

`main.c:149` 主循环：`hbTouchPoll()` → `hbUiComputeKeys()`（`hb-ui.c:310` 遍历触点与 `_quadInside/_quadArm` 命中）→ `core->setKeys()` → `core->runFrame()` → `core->getPixels()` → `hbUiBlitGame()` → `hbUiDraw()`。
限帧用 `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)`（`main.c:182`），`freq/frameCycles` 计算 `fps`（默认 `59.7275`，`main.c:124`），落后 `>200ms` 重置 `next`（`main.c:183`），每 `10s` 统计实际 fps（`main.c:190`）。

其他：存档路径 `ROM.sav` 同目录（`main.c:28` `_makeSavePath`），`mCoreConfigSetDefaultValue("idleOptimization","remove")` / `mute=true`（`main.c:79`），`--touch` 可选，`SIGINT/SIGTERM` 优雅退出并恢复光标（`main.c:199`）。

## 交叉编译（最重要！）

本机（x86_64 Linux）工具链已配好，**不要在板上装 gcc**（板上 369MB 无 swap）。

```bash
# 配置（首次或工具链变更后）
cmake -B build-hummingbird -DCMAKE_TOOLCHAIN_FILE=.cmake/zig-toolchain.cmake \
      -DBUILD_HUMMINGBIRD=ON -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build-hummingbird --parallel
# 产物：build-hummingbird/hummingbird/mgba-hb（静态 PIE，~数 MB）
```

关键配置（已在项目里就位）：

1. `.cmake/zig-toolchain.cmake:1`：
   ```cmake
   set(CMAKE_SYSTEM_NAME Linux)
   set(CMAKE_SYSTEM_PROCESSOR loongarch64)
   set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zigcc.sh")
   set(CMAKE_C_COMPILER_TARGET loongarch64-linux-musl)
   set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zigar.sh")
   set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zigranlib.sh")
   ```
2. `.cmake/zigcc.sh:7`（可执行权限！）：
   ```sh
   #!/bin/sh
   exec zig cc -target loongarch64-linux-musl -mcpu=la64v1_0 -mno-lsx -mno-lasx "$@"
   ```
3. 顶层 `CMakeLists.txt:78`：`option(BUILD_HUMMINGBIRD "Build Loongson 2K0300 hummingbird fbdev frontend" OFF)`，`CMakeLists.txt:1083` 按需 `add_subdirectory(src/platform/hummingbird)`。

### LSX 大坑（SIGILL / exit=132 的根源）

- LA264 没有向量单元，任何 LSX/LASX 指令 → `Illegal instruction`
- **zig cc 侧是重灾区**：zig 内置 clang 对 loongarch64 默认开 LSX，连 `-mcpu=la64v1_0` 都拦不住（musl 启动代码里有几百条 `vldi/vld`）。必须显式 `-mno-lsx -mno-lasx`（已在 `.cmake/zigcc.sh:7` 中固定）
- 症状识别：程序秒退且 exit=132（128+SIGILL）、日志为空 → 先怀疑漏了上面某个 flag
- 验证方法（本机即可做，不用上板）：
  ```bash
  llvm-objdump -d build-hummingbird/hummingbird/mgba-hb \
    | grep -P '\t' | awk -F'\t' '{split($2,a," "); print a[1]}' | grep -cE '^v'
  ```
  输出应为 0（需 `llvm` 支持 loongarch 反汇编；可用 `zig objdump` 或带 LA 目标的 `llvm-objdump`）

### 为什么是 zig cc

`loongarch64-linux-musl` 的系统 `gcc` 不一定带 musl sysroot，且 `mGBA` 需静态 PIE；`zig` 自带全架构 musl 源码，正好当 `linker+libc` 提供者（`zig cc` / `zig ar` / `zig ranlib`），产物 `scp` 上板即跑，无需板上装库。

## 部署与调试

```bash
# 部署（产物为静态 PIE）
scp build-hummingbird/hummingbird/mgba-hb root@192.168.1.10:/root/

# 板上运行（ROM 需先 scp 上板；--touch 可选，默认 /dev/input/event0）
ssh root@192.168.1.10 'pkill -x mgba-hb; /root/mgba-hb /root/your.gba --touch /dev/input/event0 > /tmp/app.log 2>&1 & sleep 2; cat /tmp/app.log'

# 指定存档/日志观察
ssh root@192.168.1.10 'cat /tmp/app.log; ls -lh /root/*.sav'

# 远程截图：dump 帧缓冲转 PNG（注意 BGRA 字节序，1024×600）
ssh root@192.168.1.10 'cat /dev/fb0 > /tmp/f.raw' && scp root@192.168.1.10:/tmp/f.raw /tmp/f.raw \
  && python3 -c "from PIL import Image; d=open('/tmp/f.raw','rb').read(); Image.frombytes('RGBA',(1024,600),d,'raw','BGRA').save('out.png'); print('saved out.png')"
```

- ⚠️ `pkill -f "mgba-hb|..."` 会匹配到 SSH shell 自己的命令行把自己杀掉——一律用 `pkill -x mgba-hb`（`main.c:21` 亦处理 `SIGTERM/SIGINT`）
- 板上网络：`crates.io` 被墙(403) 不影响本项目（纯 C/CMake）；`cmake` 一律在本机跑
- 板上没有 `gdb`/`gcc`；崩溃诊断靠 exit code（132=SIGILL）+ 本机 `llvm-objdump` 查向量指令
- 日志关键字：`hb-fb:`（`hb-fb.c:68`）、`hb: keys ->`（`main.c:153`）、`hb: core ... target ... fps`（`main.c:131`）、`hb: ... frames, ... fps actual`（`main.c:192`）

## 项目结构速查

```
mGBA/
├── CMakeLists.txt                          # option(BUILD_HUMMINGBIRD) + add_subdirectory(hummingbird)
├── .cmake/
│   ├── zig-toolchain.cmake                 # CMAKE_SYSTEM_NAME/PROCESSOR/COMPILER/AR/RANLIB
│   ├── zigcc.sh                            # zig cc -target loongarch64-linux-musl -mcpu=la64v1_0 -mno-lsx -mno-lasx
│   ├── zigar.sh                            # zig ar
│   └── zigranlib.sh                        # zig ranlib
└── src/platform/hummingbird/
    ├── CMakeLists.txt                      # add_executable(mgba-hb ...)
    ├── main.c                              # 入口、mCore 集成、主循环、限帧
    ├── hb-fb.c / hb-fb.h                   # fbdev mmap、stride、KD_GRAPHICS 光标抑制
    ├── hb-touch.c / hb-touch.h             # evdev MT-B 解析
    └── hb-ui.c / hb-ui.h                   # 1024×600 布局、SDF 按钮、blit
```

新增的前端逻辑优先改 `hb-ui.c`（布局/按钮坐标/渲染）与 `hb-touch.c`（输入）；`hb-fb.c` 仅在显示/光标相关时动；`main.c` 负责 mGBA 集成与时序。
