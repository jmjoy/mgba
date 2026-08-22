# AGENTS.md — 龙芯蜂鸟板（2K0300）开发须知

本目录下的项目均面向同一块目标板。改代码前先读完本文档，尤其是交叉编译的 LSX 大坑。

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
- GUI 推荐路径：Slint 软件渲染器直写 fbdev（本项目做法），无需任何窗口系统

### fbcon 光标坑

内核 fbcon（vtcon1）会在我们写的帧缓冲之上异步叠画闪烁光标。
解法已内置：启动时对 `/dev/tty0` 发 `ioctl(KDSETMODE, KD_GRAPHICS)`（见 `src/platform.rs::hide_console_cursor`）。
副作用：demo 运行期间控制台文字不可见，重启恢复。

## 触屏与输入

- 触屏控制器：FocalTech **FT5x06**（"generic ft5x06 (82)"），I2C1 @ 0x38
- 设备节点：`/dev/input/event0`，MT 协议 B（ABS_MT_SLOT / ABS_MT_TRACKING_ID / ABS_MT_POSITION_X/Y）
- 坐标即面板坐标，1024×600 与 fb 一一对应，无需变换
- 另有 PS/2 鼠标（`/dev/input/mice`）；无键盘
- libinput/libevdev 库已装（但本项目裸读 evdev，24 字节 `input_event` 结构）

## 交叉编译（最重要！）

本机（x86_64 Linux）工具链已配好，**不要在板上装 rustc/gcc**。

```
cargo build --release --target loongarch64-unknown-linux-musl
```

关键配置（已在项目里就位，新项目照抄）：

1. `rust-toolchain.toml` → `channel = "nightly"`（需 rust-src 组件）
2. `.cargo/config.toml`：
   ```toml
   [target.loongarch64-unknown-linux-musl]
   linker = "./.cargo/zigcc.sh"
   rustflags = ["-C", "target-feature=-lsx,-lasx"]

   [unstable]
   build-std = ["std", "panic_abort"]
   ```
3. `.cargo/zigcc.sh`（可执行权限！）：
   ```sh
   #!/bin/sh
   exec zig cc -target loongarch64-linux-musl -mcpu=la64v1_0 -mno-lsx -mno-lasx "$@"
   ```

### LSX 大坑（SIGILL / exit=132 的根源）

- LA264 没有向量单元，任何 LSX/LASX 指令 → `Illegal instruction`
- rustc 侧：预编译 std 不含向量，但必须用 `-Zbuild-std` + `-C target-feature=-lsx,-lasx` 重编 std 兜底（RUSTFLAGS 会应用到所有 crate 包括 build-std）
- **zig cc 侧才是重灾区**：zig 内置 clang 对 loongarch64 默认开 LSX，连 `-mcpu=la64v1_0` 都拦不住（musl 启动代码里有几百条 vldi/vld）。必须显式 `-mno-lsx -mno-lasx`
- 症状识别：程序秒退且 exit=132（128+SIGILL）、日志为空 → 先怀疑漏了上面某个 flag
- 验证方法（本机即可做，不用上板）：
  ```bash
  ~/.rustup/toolchains/nightly-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/bin/llvm-objdump -d <二进制> \
    | grep -P '\t' | awk -F'\t' '{split($2,a," "); print a[1]}' | grep -cE '^v'
  ```
  输出应为 0（llvm-tools-preview 组件提供，支持 loongarch 反汇编）

### 为什么是 zig cc

rustup 的 loongarch64 musl target 不带 self-contained libc，rust-lld 直接链接会报 `unable to find library -lc`。zig 自带全架构 musl 源码，正好当 linker+libc 提供者。产物是静态 PIE，scp 上板即跑。

## Slint 开发注意事项（v1.17.x 实测）

- feature 组合：`default-features = false` + `["std", "compat-1-2", "renderer-software"]`；不要开任何 backend-*
- 不要用官方 `backend-linuxkms`：它无条件依赖 C 库 libinput 和 libxkbcommon，纯静态链接必挂。自己写 ~100 行平台层（参考本项目 `src/platform.rs`）：
  - `MinimalSoftwareWindow::new(RepaintBufferType)` + `slint::platform::set_platform()`
  - `Platform::run_event_loop` 里循环 `update_timers_and_animations()` + `window.draw_if_needed(|r| r.render(&mut buf, stride))`
  - 像素类型用 `PremultipliedRgbaColor`（RGBA 字节序），blit 时手工转成 fb 的 BGRX
- **fontconfig 依赖坑**：Slint 1.17 文字栈 fontique/parley 默认拉 C 库 fontconfig。本项目用 vendor 补丁解决：
  - `vendor/fontique/Cargo.toml`: `default = ["std"]`（原 `["system"]`）
  - `vendor/parley/Cargo.toml`: `default = ["std"]`（原 `["system"]`）
  - 根 Cargo.toml `[patch.crates-io]` 指向这两个目录；直接依赖声明 `fontique = { version = "0.10", default-features = false, features = ["std"] }` 不够（transitive default feature 关不掉）
- **字体嵌入**：`.slint` 文件**顶层**写 `import "DejaVuSans.ttf";`（不能放组件内部！），字体进二进制，运行时零系统依赖。中文界面需换 CJK 字体文件（~10MB）
- `Math.sin/cos` 参数是角度类型：浮点要乘单位，如 `Math.sin(t * 63deg)`
- 输入注入 API 是公开的：`WindowEvent::PointerPressed/Moved/Released { position: LogicalPosition, .. }` + `window.dispatch_event(ev)`，TouchArea 能真实响应（touch-demo 已验证）
- 多个 bin 共用一个 .slint bundle：所有组件 export 在同一个文件里，lib.rs 统一 `slint::include_modules!()`

## 部署与调试

```bash
# 部署（产物 ~15MB 静态 PIE）
scp target/loongarch64-unknown-linux-musl/release/<bin> root@192.168.1.10:/root/

# 板上运行
ssh root@192.168.1.10 '(pkill -x slint-demo; /root/slint-demo > /tmp/app.log 2>&1 &); sleep 2; cat /tmp/app.log'

# 远程截图：dump 帧缓冲转 PNG（注意 BGRA 字节序）
ssh ... 'cat /dev/fb0 > /tmp/f.raw' && scp ... && python(PIL): Image.frombytes("RGBA",(1024,600),data,"raw","BGRA").save("out.png")
```

- ⚠️ `pkill -f "slint-demo|touch-demo"` 会匹配到 SSH shell 自己的命令行把自己杀掉——用 `pkill -x`
- 板上网络：crates.io 被墙(403)，清华 Alpine 镜像极快(~0.17s)；cargo 一律在本机跑
- 板上没有 gdb/rustc；崩溃诊断靠 exit code（132=SIGILL）+ 本机 llvm-objdump

## 项目结构速查

```
loongson-2k300-slint-demo/
├── Cargo.toml            # [[bin]]×2 + [lib] + [patch.crates-io]
├── rust-toolchain.toml   # nightly
├── .cargo/config.toml    # build-std + rustflags 去 LSX
├── .cargo/zigcc.sh       # zig cc 包装（-mno-lsx -mno-lasx）
├── vendor/{fontique,parley}/  # 去 system feature 的补丁副本
├── src/lib.rs            # include_modules! + pub mod platform
├── src/platform.rs       # Fbdev mmap / HummingbirdPlatform / KD_GRAPHICS 光标抑制
├── src/main.rs           # bin: slint-demo（动画演示）
├── src/bin/touch-demo.rs # bin: touch-demo（evdev 读线程 + 指针事件注入）
└── ui/demo.slint         # Demo + TouchDemo 组件，内嵌 DejaVuSans.ttf
```

新增 GUI 二进制时：组件加到 `ui/demo.slint` 并 export；bin 放 `src/bin/`；复用 `platform::*`。
