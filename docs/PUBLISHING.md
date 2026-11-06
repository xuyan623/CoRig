# 发布为独立应用

[项目首页](../README.md) · [使用手册](USER_GUIDE.md) · [构建与发布](BUILDING.md) · [AI 接入](AI_ACCESS.md) · [发布应用验证](VERIFICATION_PUBLISH.md)

把调试好的工程连同运行所需的程序、Qt 依赖和插件封装成一个可直接分发的应用。接收方拿到单个 exe，双击即可运行，不需要安装 CoRig、不需要 Python、不需要管理员权限；工程以只读快照运行，用户只能调整参数、观察数据和控制运行。

发布应用与编辑器共用同一套程序代码和同一份本地 API，区别只在启动来源（运行载荷）与权限（只读、命令白名单）。

## 实现状态

本文按 0.4.4 开发线的实现编写（2026-11-04 核对）。状态含义：**已验证（W3）**——有独立端到端证据（见下文“W3 验收结果”）；**已实现**——有完整通路但未列入本轮端到端矩阵；**计划中**——契约已定，尚未实现。

| 能力 | 状态 | 说明 |
|---|---|---|
| 目录型运行载荷（`CoRig-publish publish`） | 已实现 | 输出 `manifest.json`、`payload-index.json`、`runtime/`、`project/`、`licenses/` |
| 单 exe 容器 + 启动器解包 | 已验证（W3） | `--single-exe`；首次启动 2.82s 解包并写 `.ok`，退出清理正常 |
| 便携工作根 `<exe 主文件名>.data` | 已验证（W3） | 启动器创建；含空格/中文路径可用；不可写退出码 3 |
| 用户参数持久化 | 已验证（W3） | 改参→退出→重启读回 1.625；`dirty=false` |
| AI 接入 `GET /v3/describe` | 已验证（W3） | 身份/策略/134 签名/能力清单与实测一致 |
| `app.connection`（复制接入信息） | 已验证（W3） | 文本/路径/URL 可用，文本与 describe 均不含令牌 |
| 留存开关导出原始工程 | 已验证（W3） | 导出 5,901,016 B，哈希等于载荷 `runtime.crpj`；改参后仍等于原始快照；无留存变体拒绝 |
| 载荷内 Python 依赖收集 | 已验证（W3） | 工程含 Python 插件且提供 `--python-exe` 时，复制基础解释器与依赖闭包并自检；B1/B2 在接收环境运行通过 |
| 发布应用运行 Python 插件 | 已验证（W3） | PythonOnly 与第三方库两变体均从载荷启动；paramiko 5.1.0 / cryptography 50.0.2 读回 |
| GUI 发布入口 | 已实现，待完整验收 | 主程序“文件 → 导出为软件”，调用 `CoRig-publish --single-exe --progress`；后端发布与阶段进度已验证 |
| 代码签名、安装器、自动更新 | 不提供 | 发布应用是绿色目录 + 单文件，更新靠替换 exe |

## W3 验收结果

独立验收（t36，2026-11-04）在开发机运行：13 个用例、103/103 检查通过，ctest 12/12；报告与证据在 `build/w3-verify/report.md`（含 `report.json`、截图与日志）。本轮不等于“全部通过”，未验证边界见下。

- 单 exe：FunctionLab.exe 109,745,972 B，首次启动 2.82s 解包到 `FunctionLab.data/cache/payloads/98bbf17ac0879e69`；含空格与中文目录可用；目录不可写时退出码 3 且 stderr 为 `WORK_ROOT_UNWRITABLE` 单行 JSON。启动器在 staging 内写 `.ok` 后再原子改名，避免半成品被标记为完整。
- 参数持久化：`line_width` 改为 1.625 后退出，0.78s 重启读回 1.625；`user-params.json` 的 `release_hash` 与清单一致，`dirty=false`。
- 导出：留存变体导出 5,901,016 B，SHA-256 等于载荷内 `project/runtime.crpj`，改参后仍等于原始快照；目标存在未加 `overwrite` 时拒绝。无留存变体 `describe.exports=[]` 且调用返回 400。
- 零外写：`%LOCALAPPDATA%/CoRig` 无新增/修改；`%APPDATA%` 无 CoRig/FunctionLab 相关变化；`%TEMP%` 无新会话目录；exe 旁只新增工作根。
- 视觉：运行页无应用名+版本标题；设置窗口在发布模式只保留“通用/后台任务”两页，无插件管理/Python/工程/资源额度设置；工作区/标签只保留运行与日志；应用菜单只有复制接入信息/导出工程/关闭。
- Python：PythonOnly 与含第三方库变体均使用载荷内解释器；paramiko=5.1.0、cryptography=50.0.2 从 `node.state` 读回，日志无 pip/下载痕迹。
- **未验证**：干净 Windows VM（本机已装 Qt/MSVC/Python）、真断网、跨用户 ACL、代码签名/杀软、UNC/只读介质/超长路径、长时与压力/性能/GPU、多语言高 DPI、旧版本迁移、真实设备；本报告不把这些写成通过。

## 发布者准备

构建产物需要三项：

- `CoRig.exe` 与其 Qt 运行文件（由 `windeployqt` 收集，见[构建与发布](BUILDING.md)）。
- `CoRig-publish.exe`：无界面发布器，依赖 Qt；必须与 `CoRig.exe`、`CoRigSdk.dll` 放在同一目录（发布器从自身所在目录取运行时，找不到即 `RUNTIME_MISSING`）。程序目录中的 `tools/vc/x64` 和 `d3d/x64` 会优先用于补齐 VC 运行库与 D3D 图形编译器。
- `CoRig-launcher.exe`：单 exe 启动器，不依赖 Qt，静态链接 MSVC 运行库，目标平台必须与载荷一致（x64）。缺省在发布器同目录查找，或用 `--launcher` 指定。

发布时本机需要 Qt 部署工具和 MSVC 运行库：`windeployqt` 6.5+ 只给 `vc_redist` 安装器，发布器会补齐 `vcruntime140*.dll`、`msvcp140*.dll`，保证接收方无需安装运行库。安装包把 VC 运行库放在 `tools/vc/x64`，发布器优先从程序目录查找；开发构建则使用 `VCToolsRedistDir` / `VCINSTALLDIR`。Qt 6.12 的 QRhi 还会动态加载 `d3dcompiler_47.dll`、`dxcompiler.dll`、`dxil.dll`，它们不是导入表依赖，windeployqt 不会收集。安装包把它们放在 `d3d/x64`，发布器只接受同一目录同时含这三件的候选，选中后整套覆盖载荷中的旧文件，避免混用不同来源；开发构建从 Windows SDK 的 `Redist/D3D/x64` 读取。缺少 `dxcompiler/dxil` 时发布会以 `DEPLOY_FAILED` 失败并列出每个候选的缺件，不会生成缺少图形编译器的载荷。工程不含 Python 插件时发布不需要 Python；含 Python 插件时必须用 `--python-exe` 指向装好依赖的本机解释器，发布器只复制、不下载、不装包。运行期只使用载荷内的解释器，不再读取本机 Python 设置。

## 生成发布物

```powershell
# 目录型载荷 + 单 exe
.\CoRig-publish.exe publish --project .\examples\FunctionLab.crpj --output D:\build\FunctionLab --single-exe

# 仅目录型载荷（核对清单与索引；不能直接双击运行）
.\CoRig-publish.exe publish --project .\examples\FunctionLab.crpj --output D:\build\FunctionLab
```

| 选项 | 说明 |
|---|---|
| `--project` / `-p` | 工程文件（`.crpj`）。必填 |
| `--output` / `-o` | 输出目录；即载荷根。已存在时须 `--overwrite`。必填 |
| `--app-id` | 应用标识，缺省 `org.corig.<工程名归一化>`；不合规时改用 `corig.app.<工程内容哈希前 12 位>` |
| `--name` | 应用名称，缺省取工程文件名 |
| `--version` | 应用版本，缺省 `1.0.0` |
| `--retain-original` | 记录“保留原始工程”能力标志，见下文 |
| `--overwrite` | 覆盖已存在的输出目录与 `<output>.exe` |
| `--qt-prefix` | Qt 安装目录或 `windeployqt.exe`；缺省用构建期 Qt |
| `--qml-dir` | QML 源目录；缺省从构建树推导 `<exeDir>/../../src/qml` |
| `--single-exe` | 除目录载荷外，再封装为 `<output>.exe` |
| `--launcher` | `CoRig-launcher.exe` 路径；缺省在发布器同目录查找 |
| `--python-exe` | 收集 Python 载荷用的本机 `python.exe`；工程含 Python 插件时必需，纯 C++/QML 工程可省略 |

发布器向标准输出写一行 JSON 报告，退出码 `0` 成功、`1` 失败：

```json
{"ok":true,"output":"D:\\build\\FunctionLab","release_hash":"…","payload_bytes":246000000,
 "single_exe":"D:\\build\\FunctionLab.exe","single_exe_bytes":246500000,"index_sha256":"…",
 "warnings":[],"runtime_files":[{"path":"runtime/CoRig.exe","sha256":"…","mtime":"…"}]}
```

失败时为 `{"ok":false,"error":{"code":"…","phase":"prepare|collect|verify|assemble|container|commit","detail":"…"}}`。`runtime_files` 记录被复制的 `CoRig.exe` 与 `CoRigSdk.dll` 的哈希与时间，用于发现“两个构建混装”。

## 载荷布局

目录型载荷：

```text
FunctionLab/                 ← --output
├─ manifest.json             ← 载荷清单：身份与发布元数据
├─ payload-index.json        ← 载荷索引：逐文件 path/size/sha256
├─ runtime/                  ← CoRig.exe、CoRigSdk.dll、Qt6*.dll、platforms/、styles/、qml/、vc_redist、licenses/
├─ project/runtime.crpj      ← 强制携带全部插件的工程快照
├─ python/                   ← 仅含 Python 插件时：python.exe、Lib/、worker.py、python-runtime.json、依赖
└─ licenses/
```

- **载荷清单** `manifest.json`：`format=corig-app`、`format_version=1`、`mode=publish`、`app{id,name,version}`、`release{snapshot_sha256,created_at,corig_version}`、`retain_original`、`runtime.executable="runtime/CoRig.exe"`。运行时先读它决定模式，再校验 `project/runtime.crpj` 的哈希与 `snapshot_sha256` 一致。
- **载荷索引** `payload-index.json`：`format=corig-payload-index`、`format_version=1`、`files[{path,size,sha256}]`，不含自身；用于核对目录载荷是否完整。
- **插件**：始终嵌入 `project/runtime.crpj`，发布时强制 `include_plugins=true`，没有 `plugins/` 目录。运行期按需解包到 `<工作根>/cache/plugin-packages/<哈希>`。工程保存选项里的“只记录依赖”对发布无效。
- **Python**：工程含 `implementation=python` 的插件时，发布器把采集解释器的基础运行时复制到 `python/`，按各插件 `python.distributions/requires/extras` 解析已安装分发的依赖闭包并复制，再写 `python-runtime.json`（含解释器版本、模块与分发清单）并跑一遍载荷自检；依赖缺失或版本冲突在发布阶段失败，不留给接收方。纯 C++/QML 工程没有 `python/`。采集器以 `-E -B -X utf8` 运行（保留 user site，忽略 `PYTHON*`），因此可使用 DevTools 等带 user site 依赖的解释器；已用 DevTools 解释器复验 paramiko 5.1.0 + cryptography 46.0.7 进载荷，`python-runtime.json` 全相对路径。运行期 worker 使用 `-I`，固定 `python/python.exe` 与 `python/worker.py`，不读取本机设置、不回退到系统解释器；`loadPublishedProject` 在载荷含解释器时设置该环境。找不到采集器时错误会列出已搜索位置。依赖声明见[插件开发](PLUGIN_SDK.md#python-声明与发布依赖)。
- **路径约束**：载荷根、`cache/payloads`、`data` 必须构成 `<工作根>/cache/payloads/<载荷 ID>` 布局，否则运行时拒绝启动（`PAYLOAD_INCOMPLETE`）。

## 单 exe 容器

`--single-exe` 在目录载荷之外生成 `<output>.exe`，两者内容一致，目录载荷保留用于核对与调试。容器格式：

```text
[启动器 PE][载荷文件 1][载荷文件 2]…[二进制索引][64 字节 trailer]
```

- 索引记录每个文件的相对路径、大小、偏移与 SHA-256；trailer 记录版本、启动器大小、索引偏移/大小与索引哈希，magic 为 `CORIGIX1` / `CORIGPK1`。
- 只存储不压缩（标志位 0）：载荷以 Qt DLL 与 QML 为主，压缩收益低，且启动器要保持无 Qt 依赖。
- 上限：载荷 8 GiB、20 万个文件、索引 64 MiB；路径拒绝绝对路径、`..`、盘符、ADS 与反斜杠。
- **未签名**：索引哈希只用于发现截断或损坏，不防重新打包；需要可信分发时由发布者另行签名与校验和。

启动流程（`CoRig-launcher.exe`）：

1. 读自身末尾 trailer 与索引，校验索引哈希与每项边界；版本不支持 → 退出码 5，其余损坏 → 退出码 2。
2. 工作根 = `<exe 所在目录>/<exe 主文件名>.data`（例 `FunctionLab.exe` → `FunctionLab.data`），载荷目录 = `cache/payloads/<载荷 ID>`，载荷 ID 取索引 SHA-256 前 16 字节的十六进制。
3. 建立 `cache/payloads` 并写探针文件；失败 → `WORK_ROOT_UNWRITABLE`、退出码 3。
4. 目标目录不存在或没有 `.ok` 标记时，解包到 `.tmp-<pid>`，逐文件校验哈希，在 staging 内写 `.ok` 后再原子改名；失败保留 `.broken-<pid>` → 退出码 2。已有 `.ok` 的目录直接复用，不重复校验。
5. 以独占方式打开载荷目录的 `.lock` 并持有到子进程退出；清理旧代后设置 `CORIG_APP_ROOT`，以 `runtime/` 为工作目录启动 `runtime/CoRig.exe`，透传其退出码。
6. 清理规则：保留最新 3 代载荷（含当前）；`.tmp-*`、`.broken-*` 超过 24 小时删除；被其他实例持锁的目录不动。

`CoRig-launcher.exe --diagnose` 不启动应用，只校验容器并向标准输出打印一行 `{"ok":true,"index":"…","payload":<字节>}`，失败仍向标准错误打印 `{code,phase,detail}` 并返回对应退出码。

## 接收方运行

- 把单 exe 放到任意可写位置（用户目录、桌面、U 盘）双击。首次启动会在 exe 旁创建且只创建一个 `<exe 主文件名>.data` 目录；exe 所在目录的其余内容不变。
- 工作根内容：`data/`（`settings.ini`、`.sessions/` 连接记录、`params/user-params.json` 参数覆盖、`captures/` 截图、`diagnostics/` 崩溃与转储）与 `cache/`（`qml/`、`shaders/`、`temp/<pid>/`、`plugin-packages/`、`payloads/`）。
- 目录载荷不是运行入口：运行时要求载荷位于 `<工作根>/cache/payloads/<载荷 ID>`，或由启动器传入 `CORIG_APP_ROOT`；直接双击 `runtime/CoRig.exe` 会报“应用载荷不完整（PAYLOAD_INCOMPLETE）：无法确定应用工作目录”。分发用单 exe，目录载荷只用于核对内容与索引。
- 程序目录不可写时不回退到系统盘：启动器退出码 3 并在标准错误打印 `{"code":"WORK_ROOT_UNWRITABLE",…}`；手工构造工作根布局直接调试时，创建 `data/` 失败会弹窗“程序所在目录不可写，无法创建工作目录……请将程序移到可写位置后重试”，退出码 1。
- 发布应用忽略：`--data-dir`（静默）、命令行位置参数工程、`--update-installer`、`--configure-installer`。工作根不可自定义，快捷方式无法改写数据位置。
- 发布应用恒为只读：没有编辑模式，不能保存工程、切换工程或管理插件（界面入口已按模式隐藏，见下文“发布应用界面”）；参数改动写入覆盖文件，不改工程、不标记未保存。
- 同一 exe 可重复启动：共用同一工作根与载荷目录，各自写入独立实例记录；多实例注意共享 `settings.ini` 与参数覆盖文件。
- 升级：用新 exe 覆盖旧文件（或改名并存）即新载荷；旧载荷按清理规则最多保留 3 代；`data/` 保留，参数覆盖按“节点 + 参数名”重放到新快照，`release_hash` 在下次写入时更新。
- 重置：删除整个 `<exe 主文件名>.data` 即回到出厂；只清参数保留设置，删除 `data/params/`。

## 发布应用界面

发布运行模式只呈现运行所需入口，编辑入口在界面上直接消失，而不是点击后才被 API 拒绝：

- 菜单栏保留“应用”菜单：复制 AI 接入信息、导出工程…（仅 `retain_original=true` 时出现）、关闭窗口；文件与编辑菜单隐藏，相关快捷键同时禁用。设置菜单仍可打开，但发布模式只保留“通用/后台任务”两页；工程、插件、Python、资源额度页不创建。
- 工作区与标签只保留运行页与日志页；关闭标签、添加工作区与标签右键菜单隐藏。启动后自动打开运行页。
- 插件管理、本地 Python 环境、工程保存选项与资源额度页隐藏；连接视图的“配置”按钮与日志页双击定位节点在发布模式禁用，避免绕过菜单进入设计图。设置窗口通过 API 的 `settings.open` 与菜单共用，只接受 `general`/`tasks`。
- AI 仍可通过本地 API 观测、调参、控制运行与截图；界面隐藏只减少误导入口，权限边界仍由命令注册表决定。

## 留存开关

`--retain-original` 只把 `retain_original=true` 写入清单并注入运行时状态，不额外复制工程：载荷里始终只有一份 `project/runtime.crpj`，导出通道（`project.export_original`）按该文件的字节导出。该命令仅发布模式且 `retain_original=true` 时可用，参数为 `path`（必填）与 `overwrite`（目标已存在时）；成功返回 `{exported:true,path}`。`/v3/describe` 只在留存开启时列出该能力，`available` 与实际一致。它与发布机上的原始工程文件可能不同（插件嵌入、路径归一化、另存后的版本）；需要原文件时请保留发布输入，不要依赖导出通道还原。

明确边界：保留与否都不是保密手段。任何人都能解包载荷并读取 `project/runtime.crpj`；不提供加密、混淆或 DRM，也不需要把它当作保护商业秘密的机制。

## 失败语义

发布器（退出码 1，报告 `error.code` / `error.phase`）：

| code | phase | 含义 |
|---|---|---|
| `INVALID_REQUEST` | prepare | 缺工程/输出参数，或无法创建输出父目录 |
| `PROJECT_NOT_FOUND` | prepare | 工程文件不存在 |
| `OUTPUT_OVERLAPS_PROJECT` | prepare | 输出目录与源工程相同、互相包含 |
| `OUTPUT_EXISTS` | prepare / container | 输出已存在且未加 `--overwrite` |
| `APP_ID_INVALID` | prepare | 应用标识不合规 |
| `PROJECT_INVALID` | prepare | 工程读取或校验失败 |
| `PLUGIN_INVALID` / `MISSING_PLUGIN` | prepare | 工程内插件包损坏，或缺少插件 |
| `SNAPSHOT_FAILED` | prepare | 写不出 `project/runtime.crpj` |
| `QML_DIR_MISSING` | collect | 找不到 QML 源目录 |
| `DEPLOY_FAILED` | collect | `windeployqt` 缺失或失败 |
| `RUNTIME_MISSING` | collect | 找不到 `CoRig.exe` / `CoRigSdk.dll` |
| `PUBLISH_FAILED` | collect / verify / container | 复制失败、校验失败（缺 DLL、缺 Qt 插件、快照哈希不符）或打包失败 |
| `LAUNCHER_MISSING` / `LAUNCHER_INVALID` | container | 找不到启动器，或不是有效 PE |
| `COMMIT_FAILED` | commit / container | 无法替换输出目录或写单文件产物 |
| `PY_INTERPRETER` | collect | 工程含 Python 插件但未提供有效的 `--python-exe` |
| `PY_COLLECTOR_MISSING` | collect | 找不到 `python/collect.py`；detail 列出发布器同目录、上级目录与 `CORIG_PYTHON_COLLECT` 等已搜索位置 |
| `PY_DECL_INVALID` / `PY_DECL_UNRESOLVED` / `PY_CONFLICT` | collect | 插件 Python 声明不合法、导入名/依赖在采集解释器中无法解析，或版本约束冲突；由采集器透传 |
| `PY_COLLECT_FAILED` | collect | 采集器启动/超时/输出无法解析，或复制载荷失败 |
| `PY_PROBE_FAILED` | verify | 载荷解释器自检失败：路径越出载荷、模块无法导入或不是载荷内的 `python.exe` |

启动器退出码：`0` 正常（透传应用退出码）、`2` 载荷损坏或解包失败、`3` 工作根不可写、`4` 拉起运行时失败、`5` 容器版本不支持。失败一律向标准错误写一行 `{code,phase,detail}`。

运行时启动失败：载荷清单缺失或不合规 → 弹窗“应用载荷不完整（PAYLOAD_INCOMPLETE）：…”，退出码 1；工程快照哈希不符或插件未随载荷提供 → 弹窗“应用载荷不完整：…”，退出码 2；创建数据目录失败 → “CoRig 无法启动”，退出码 1。

## 清理与残留

- 发布失败或中断会留下 `<output>.part-<pid>`、`<output>.exe.part-<pid>`、`<output>.old-<pid>`，确认无进程占用后可手动删除；发布器下次运行同一输出时会重建暂存目录。
- `--overwrite` 先把旧输出改名为 `.old-<pid>`，提交成功后才删除；提交失败会回滚旧输出。
- 接收方清理见上文“重置”；卸载发布应用只需删除 exe 与工作根目录，不写注册表、不写系统目录。

## 未承诺

- 不含安装器、代码签名、自动更新、崩溃上报与许可校验。
- 不压缩载荷（单 exe 体积约等于目录载荷）。
- 主程序提供 GUI 发布入口（“文件 → 导出为软件”），发布逻辑仍由 `CoRig-publish` 执行；Python 依赖只从发布机的已安装环境复制，不联网解析、不创建虚拟环境。
- 不保证在 exe 所在目录不可写、网络盘或受限权限下的行为；这类环境的验证边界见[发布应用验证](VERIFICATION_PUBLISH.md)。
