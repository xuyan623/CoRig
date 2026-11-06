# CoRig 构建与发布

[项目首页](../README.md) · [源码仓库](https://github.com/xuyan623/CoRig) · [验证范围](VERIFICATION.md)

## 开发环境

当前 Windows x64 发布使用：

- MSVC 2022 x64、CMake 3.25+ 和 Ninja。
- Qt 6.12.0 MSVC x64：Core、Test、Gui、Qml、Quick、QuickControls2、Network、SerialPort、Svg、Core5Compat。
- 仓库自带的 libssh2 1.11.1 源码，以及本机 OpenSSL 3.5.9 静态开发库。

```powershell
git clone https://github.com/xuyan623/CoRig.git
cd CoRig
```

`tools/build_native.ps1` 中的 Visual Studio、Qt 路径和 CMake 中的 OpenSSL 默认路径对应维护者环境；在其他机器上需按实际安装位置调整。Qt 可通过 `-QtPrefix` 传入，OpenSSL 可通过 CMake 的 `OPENSSL_ROOT_DIR` 指定。使用 Qt 官方安装工具准备 SDK；`tools/install_qt_sdk.py` 是可选安装辅助工具，需要单独的 Python/下载环境。

## 构建和原生测试

```powershell
.\tools\build_native.ps1 -Test
# Qt 安装在其他位置时：
.\tools\build_native.ps1 -QtPrefix C:\Qt\6.12.0\msvc2022_64 -Test
```

脚本在当前进程导入 MSVC 环境，使用 CMake/Ninja 构建，并运行十一套 CTest。输出在 `build/native/`，包括 GUI、CLI、发布器 `CoRig-publish.exe`、发布启动器 `CoRig-launcher.exe`、CoRigSdk.dll、80 个官方插件包、公开 SDK 和测试程序。

构建和官方 C++ 测试无需安装 Python。若需验证用户 Python 插件，可设置 `CORIG_TEST_PYTHON` 为实际解释器；发布含 Python 插件的工程时，`--python-exe` 必须指向装好依赖的解释器。界面与性能验收驱动使用 Python 标准库；本机 SSH 服务器验收额外需要 Paramiko。

## 生成便携包和源码包

```powershell
.\tools\package_native.ps1
```

脚本调用 Qt 官方部署工具准备 `build/native-release/CoRig/`，复制文档、技能、许可证、三个公开示例、发布器与启动器、`python/collect.py`、VC 运行库和 D3D 图形编译器，以及已有验证摘要，然后生成：

| 文件 | 用途 |
|---|---|
| `dist/CoRig-Windows-x64.zip` | 完整 Windows x64 便携程序，可直接解压运行 |
| `dist/CoRig-SDK-Windows-x64.zip` | 独立 C++ SDK、构建工具与完整示例 |
| `dist/CoRig-source.zip` | 对应项目源码、libssh2、测试、构建工具及文档 |

打包会重建自身的生成目录。便携包不包含 Python 运行时，但包含发布器、启动器、`python/collect.py`、`tools/vc/x64` 和 `d3d/x64`（`d3dcompiler_47.dll`、`dxcompiler.dll`、`dxil.dll`，取自 Windows SDK 10 Redist）；缺少这些文件时打包直接失败，避免生成运行期缺图形编译器的包。源码包不包含本地凭据、私人项目、虚拟环境或构建缓存。

便携 ZIP 是编辑器产物，已含发布器、启动器、VC 运行库和 D3D 图形编译器；发布端仍需要 Qt（含 `windeployqt`），但不需要接收方安装 Python、Qt 或 VC++ 运行库。

## 生成发布应用

发布器读取一个工程，生成只读的发布载荷；接收方运行载荷中的 `CoRig.exe`，或运行单文件封装。发布者机器需要 Qt（含 `windeployqt`）；Qt 的 QML 模块、VC 运行库和 D3D 图形编译器必须随载荷部署。安装包已在程序目录携带 `tools/vc/x64` 与 `d3d/x64`，开发构建则从 MSVC/Windows SDK 补齐：

```powershell
# 目录载荷（--output 目录；用于核对内容与索引，不能直接双击 runtime/CoRig.exe）
.\build\native\CoRig-publish.exe publish --project .\project\demo.crpj --output .\dist\demo --app-id demo --name "演示应用"
# 同时封装单 exe（输出 <output>.exe，载荷与启动器一步生成）
.\build\native\CoRig-publish.exe publish --project .\project\demo.crpj --output .\dist\demo --single-exe
# 安装版（发布器、启动器和 Qt QML 模块都在安装目录内）
$install = "$env:LOCALAPPDATA\Programs\CoRig"
& "$install\CoRig-publish.exe" publish --project .\project\demo.crpj --output .\dist\demo --single-exe `
  --qt-prefix C:\Qt\6.12.0\msvc2022_64\bin --qml-dir "$install\qml" --launcher "$install\CoRig-launcher.exe"
```

常用选项：`--app-id`、`--name`、`--version`（缺省 1.0.0）、`--retain-original`（在清单中声明保留原始工程能力，不复制原文件）、`--overwrite`、`--qt-prefix`、`--qml-dir`、`--launcher`（缺省在发布器同目录查找 `CoRig-launcher.exe`）、`--python-exe`（工程含 Python 插件时必需）。发布器必须与 `CoRig.exe`、`CoRigSdk.dll` 同目录，否则报 `RUNTIME_MISSING`。发布器调用 `windeployqt` 收集 Qt 依赖，并检查 `VCRUNTIME140.dll`、`VCRUNTIME140_1.dll`、`MSVCP140.dll`、`d3dcompiler_47.dll`、`dxcompiler.dll`、`dxil.dll`、Qt 平台插件与 QML 模块；缺少 VC 运行库或 D3D 编译器时以 `DEPLOY_FAILED`（phase=collect）报告查找路径，其他缺失以 `PUBLISH_FAILED`（phase=verify）报告文件名。目录载荷含 `manifest.json`、`payload-index.json`、`runtime/`、`project/runtime.crpj` 和 `licenses/`，不含 `plugins/`；工程含 Python 插件时另有 `python/`（解释器、依赖闭包、`worker.py`、`python-runtime.json`），并在发布期完成依赖解析与自检（`PY_*` 错误码）。

发布器失败时以非零退出并输出一行 JSON（`{ok:false,error:{code,phase,detail}}`）；成功时输出载荷路径、`release_hash`、字节数、Python 闭包摘要与警告。生成后按[发布为独立应用](PUBLISHING.md)的清单在干净目录验收，并按[发布应用验证](VERIFICATION_PUBLISH.md)记录证据。

部署目录已经准备好，仅需刷新文档、示例、验证摘要与归档时，可使用：

```powershell
.\tools\package_native.ps1 -ArchiveOnly
```

## 部署验收

使用单独的数据目录运行真实部署程序，验证清除开发 Qt/Python 搜索路径后仍能启动、打开示例、处理数据并完成界面与本地 API 操作：

```powershell
python .\tools\native_release_check.py `
  --executable .\build\native-release\CoRig\CoRig.exe `
  --data-dir .\build\release-check-data `
  --output .\verification\corig-packaged
```

该驱动会启动实际窗口、软件回环及仅监听本机的 SSH 测试服务器；测试 Python 环境需安装 Paramiko。数据目录应为本次验收专用目录。详细覆盖和实机边界见[验证范围](VERIFICATION.md)。验收后再次使用 `-ArchiveOnly`，将最新摘要纳入最终归档。

## GitHub Release

正式仓库为 [xuyan623/CoRig](https://github.com/xuyan623/CoRig)，发布页为 [Releases](https://github.com/xuyan623/CoRig/releases)。发布标签应与 CMake 的项目版本及[版本说明](RELEASE_NOTES.md)一致。

发布时附上便携包、对应源码包和 `SHA256SUMS.txt`。GitHub 自动生成的 Source code 归档对应标签源码；额外提供的 `CoRig-source.zip` 方便离线查看和构建。

## 安装程序与更新仓库

准备 Qt Installer Framework 4.7+，先构建并生成便携部署目录，再执行：

```powershell
.\tools\package_installer.ps1 -IfwBin C:\Qt\Tools\QtInstallerFramework\4.7\bin
```

输出 `dist/CoRigSetup-0.4.4-Windows-x64.exe` 与 `dist/updates/windows-x64/`。安装内容分为 Qt 运行依赖、主程序、官方插件和可选 SDK，Qt 组件版本独立，未变化时无需重下。安装器只服务编辑器；发布出去的应用不使用安装器，也没有安装、更新或卸载入口。安装器按当前用户安装，默认写入 `%LOCALAPPDATA%\Programs\CoRig`，可在向导中改到其他有写权限的文件夹；个人工程、插件与设置保存在安装目录内，更新只替换程序文件与同 ID 的官方插件包。

`-RepositoryUrl` 指定维护工具保存的更新源，默认 https://xuyan623.github.io/CoRig/updates/windows-x64/。正式发布时将整个目录同步到 gh-pages 分支的 updates/windows-x64/，GitHub Pages 从该分支根目录发布；源文件与发布附件分别保留在 main 和 GitHub Release 中。生成本地仓库后仍需验证线上 Updates.xml 与实际维护工具结果。更新组件必须提高对应 package.xml 的 Version，再运行 repogen，更新已有插件时保留用户目录与停用/移除选择。

同一软件版本重发时，可用 `-ApplicationPackageVersion` 单独提高应用组件版本，保留软件显示版本和安装包名称。0.4.4 是功能版本，应用组件与官方插件组件均随项目版本取 0.4.4，高于已安装的 0.4.3.1；后续重发仍须继续提高对应组件版本。打包脚本可用 `-BuildPath`、`-ReleasePath`、`-StagePath`、`-OutputRoot` 和 `-PublishDir` 指向明确的新候选目录，避免混用不同构建树或覆盖既有发行物。

安装包使用完整内嵌内容并保留远程维护支持；不要用 --offline-only 制作需要在线更新的包。验收用独立安装目录和临时本地更新源，不修改真实安装与个人资料。
