# 局部打码插件

## 下载 v1.6.1-local-r3

Windows x64 · OBS 32.2.2 · Composite Blur 1.6.1 修改版

- **[下载 Windows 插件安装包 ZIP](https://github.com/BUYAOJIAOWOSHUNSHUN/obs-local-blur/releases/download/v1.6.1-local-r3/obs-local-blur-1.6.1-local-r3-windows-x64.zip)**（解压后合并复制到 OBS 安装目录）
- [SHA-256 校验文件](https://github.com/BUYAOJIAOWOSHUNSHUN/obs-local-blur/releases/download/v1.6.1-local-r3/SHA256SUMS.txt)
- [更新说明与历史安装包](https://github.com/BUYAOJIAOWOSHUNSHUN/obs-local-blur/releases)

GitHub 自动生成的 Source code 压缩包是源码，不是编译好的插件安装包。

## 简介

版本功能优化：[不要叫我顺顺](https://space.bilibili.com/40900884)

基于 [FiniteSingularity / Composite Blur 1.6.1](https://github.com/finitesingularity/obs-composite-blur/) 开发优化，保留上游源码、作者信息和 GPL-2.0 许可。这是独立修改版，并非上游官方发布。

## 相比原项目优化了什么

- 中文滤镜名为“局部打码”。
- 在滤镜预览中直接拖动裁剪蒙版范围，拖动边角调整大小，Esc 取消当前拖动。
- 细边框实时跟随圆角半径参数，保留四边中点手柄，隐藏四角手柄。
- 保留羽化和反转参数；辅助框仅用于编辑，不进入源输出。
- 底部显示插件仓库、优化作者哔哩哔哩主页、原插件仓库三个链接。

## 界面与功能预览

以下为本修改版独立 Qt/libobs 测试窗口的实际渲染截图，使用合成示例画面；并非用户的直播内容。实际 OBS 主题、尺寸和属性排列可能不同。

### 直接拖动裁剪范围

四个方向的裁剪数值整合到预览范围框中：拖动框内移动，拖动四边或四角缩放。边框只用于编辑，录制或直播输出不显示它。

![可视裁剪预览](.github/assets/visual-crop-preview.png)

### 圆角实时同步

调节“圆角半径”时，细边框实时匹配蒙版轮廓。保留四边中点白色手柄，移除四角方块和常驻操作说明。

![实时圆角与四边手柄](.github/assets/rounded-preview.png)

### 插件与作者链接

底部使用两行介绍，分别链接本仓库、优化作者的哔哩哔哩主页和原项目。

![底部两行链接](.github/assets/credit-preview.png)

## 下载和安装

从本仓库 [Releases](https://github.com/BUYAOJIAOWOSHUNSHUN/obs-local-blur/releases) 下载 Windows x64 修改版。

验证目标为 **Windows x64 / OBS 32.2.2 / Qt 6.11.1**。可视编辑器目前仅在 OBS 32.2.2 启用；其他 OBS 版本未验证。此包是本地构建，未使用原作者的数字签名。

1. 正常退出 OBS，备份现有 `obs-plugins/64bit/obs-composite-blur.dll` 和 `data/obs-plugins/obs-composite-blur`。
2. 将压缩包中的 `obs-plugins`、`data` 合并复制到 OBS 安装目录。
3. 打开来源的滤镜，添加“局部打码”，在效果蒙版中选择裁剪模式。

现有 Composite Blur 滤镜会沿用原设置。已自定义的滤镜名称保留。回退时退出 OBS，再恢复备份的插件及其数据目录。

## 验证状态

基础拖拽交互已由用户在实际 OBS 中验收。r2 默认缩放与高 DPI 各 51 项独立 Qt/libobs/D3D11 检查通过；r3 仅修改底部署名和链接，最终结果见 CHANGELOG.md。独立测试不替代所有设备和 OBS 版本的实机验证。

## 构建

已验证构建入口为 `build-local.ps1`，测试入口为 `tests/run-local-tests.ps1`。使用 MSVC 14.44、Windows SDK 10.0.26100.0、Qt 6.11.1 和 OBS 32.2.2 源码头文件，链接目标 OBS 安装目录中的库。CMake 配置保留，但本修改版尚未验证 CMake 和非 Windows 构建。

通过参数指定本地目录，不需要使用作者电脑的路径：

```powershell
./build-local.ps1 -Dependencies <依赖目录> -ObsBin <OBS安装目录/bin/64bit> -BuildDirectory <构建目录> -PackageDirectory <输出目录>
./tests/run-local-tests.ps1 -Dependencies <依赖目录> -ObsRoot <OBS安装目录> -BuildDirectory <构建目录> -PackageDirectory <输出目录> -ReviewDirectory <测试输出目录>
```

当前脚本的依赖目录结构：

- `10.msvc-bootstrap/msvc/VC/Tools/MSVC/<版本>` 与 `10.msvc-bootstrap/msvc/Windows Kits/10`
- `obs-qt6-20260715/include`、`lib`（Qt 6.11.1）
- `obs-studio/libobs`、`deps`（OBS 32.2.2）
- `llvm-mingw-20261006-ucrt-x86_64/bin/llvm-readobj.exe`

这些目录名仅为构建脚本沿用的依赖布局。工具链和第三方依赖不随仓库分发。

## 上游来源与许可

原作者：FiniteSingularity。上游版本：v1.6.1。原说明保存在 [README.upstream.md](README.upstream.md)，完整许可见 [LICENSE](LICENSE)。本修改版继续遵循上游许可，保留源码版权声明。

