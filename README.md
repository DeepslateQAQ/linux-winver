# linux-winver

我修复了 Linux 非 KDE 桌面环境没有 winver 的 bug

## 介绍

- 读取 `/etc/os-release`（或 `/usr/lib/os-release`）获取当前发行版信息。
- 显示发行版 Logo、名称、版本号、内核版本（对应 Windows 的“OS 内部版本”）。
- 自动探测并显示系统已安装的许可协议文本，可在对话框中查看全文。
- UI 语言跟随系统 locale：`zh*` 显示中文，其他语言显示英文。
- 内置 30 个发行版的演示数据，使用 `--demo` 可以模拟不同发行版的 winver 窗口。
- 提供 `--lang` 参数手动覆盖界面语言。

## 构建使用方法

### 依赖

- C 编译器（`cc`/`gcc`）
- GTK4 开发包
- `pkg-config`
- GLib 开发工具（提供 `glib-compile-resources`）

常见发行版安装依赖示例：

```bash
# Debian / Ubuntu
sudo apt install build-essential libgtk-4-dev pkg-config libglib2.0-dev-bin

# Fedora
sudo dnf install gcc make pkgconfig gtk4-devel glib2-devel

# Arch Linux
sudo pacman -S gcc make pkgconf gtk4 glib2
```

### 构建

```bash
make
```

构建成功后在当前目录生成 `winver` 可执行文件，并自动生成 `src/resources.c`（由 `data/icons.gresource.xml` 和 `data/icons/*.png` 编译而来）。

### 运行

```bash
./winver
```

常用选项：

```bash
# 强制使用中文界面
./winver --lang=zh

# 强制使用英文界面
./winver --lang=en

# 跟随系统 locale（默认行为）
./winver --lang=auto

# 显示发行版选择器，演示其他发行版
./winver --demo
```

### 安装

```bash
sudo make install
```

默认安装到 `/usr/local/bin/winver`。也可以自定义前缀：

```bash
make install PREFIX=$HOME/.local
```

### 使用 Nix Flake

```bash
# 进入开发环境
nix develop

# 直接运行
nix run .#winver

# 构建并查看 result/bin/winver
nix build .#winver
```

### 清理

```bash
make clean
```

## 协议（License）

本项目以 [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html)（GPLv3）发布，完整文本见 [LICENSE](LICENSE)。

对话框内展示的“系统许可协议”来自发行版实际安装的许可文件（如 `/usr/share/common-licenses/`、`/usr/share/licenses/` 等路径）；`--demo` 模式中展示的是内置演示数据。
