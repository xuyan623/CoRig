# 发布应用验证矩阵

[项目首页](../README.md) · [发布指南](PUBLISHING.md) · [AI 接入](AI_ACCESS.md) · [验证范围](VERIFICATION.md)

本文定义“把工程发布为独立 exe 应用”的验收方法：每条用例给出目的、方法步骤、证据、前提与失败判据，并对当前实现状态标注**已验证（W3）/ 可执行 / 计划中**。矩阵用于后续开发与评审；标“已验证（W3）”的用例有 2026-11-04 独立验收证据，其余未执行。编辑器版本的历史验证结果仍见[验证范围](VERIFICATION.md)。

状态说明：**已验证（W3）**——有 t36 独立验收证据；**部分验证（W3）**——部分子用例通过，未覆盖项见正文；**可执行**——方法可执行但本轮未执行或未覆盖全部条件；**计划中**——对应能力尚未实现（如 GUI 发布入口）。

## W3 实测结果（2026-11-04）

t36 独立端到端验收（开发机，Windows 10.0.26300、普通用户、非干净 VM）共 13 个用例、**103/103 检查通过**，ctest **12/12**。报告：`build/w3-verify/report.md`（15,335 B）、`report.json`（63,698 B）；用例源 `report-core/py/extra/menu/ui/ui2.json`；截图在 `build/w3-verify/shots/`。本节只覆盖实际执行的用例，未列出的矩阵项仍是可执行/未执行。

| 证据 | 结果 |
|---|---|
| 产物 | FunctionLab.exe 109,745,972 B / SHA-256 `f3aea833…`；无留存变体 109,753,158 B；PythonOnly 134,878,061 B；第三方库 136,297,011 B |
| A1/A2/A3 | 首次启动 2.82s，解包到 `FunctionLab.data/cache/payloads/98bbf17ac0879e69`；含空格+中文目录 2.84s；icacls deny → exit 3 + `WORK_ROOT_UNWRITABLE`；exe 旁只新增工作根，退出无残留进程 |
| C | schema app_mode=publish、commands=signatures=134、allow/conditional/deny=46/2/86；401 覆盖 schema/错误令牌/command；token 仅在 `data/.sessions` |
| D | 10 项设计/编辑/插件/批量命令 400 拒绝；batch 夹带 log.clear+project.save 整批拒且 log 未清；theme 允许；runtime.start/stop、params、node.configure(persist)、export_original(retain) 可用 |
| H6 | `line_width` 1.625 改参→退出→0.78s 重启读回；`user-params.json` release_hash 一致；dirty=false |
| F1/F2 | 留存导出 5,901,016 B，SHA-256 等于载荷 `runtime.crpj`，改参后仍等于原始快照；目标存在需 overwrite；无留存变体 exports=[] 且拒绝 |
| B | PythonOnly 与第三方库变体均从载荷解释器启动；paramiko=5.1.0、cryptography=50.0.2 由 node.state 读回；日志无 pip/下载痕迹 |
| Z | `%LOCALAPPDATA%/CoRig` 零新增/修改；`%APPDATA%` 无 CoRig/FunctionLab 相关变化；`%TEMP%` 无新会话目录 |
| U1/U2 | 运行页无应用名+版本标题；设置菜单 300×180、3 项、无空白占位、无插件管理/Python 设置；标签仅运行/日志 |
| R | ctest 12/12，55.87s |

**未验证（不得当作通过）**：干净 Windows VM（本机已装 Qt/MSVC 运行时/Python）、真断网、跨用户 ACL/共享目录、代码签名/SmartScreen/杀软、UNC/只读介质/超长路径、长时/压力/内存/性能/GPU、多语言高 DPI、旧版本迁移升级、t37 后的重新打包、真实设备。t35 的第三方库运行证据见 `build/w3-thirdparty/`；t36 未重新打包 t37 视觉修正。

## 环境与工具

| 项 | 要求 |
|---|---|
| 系统 | Windows 10/11 x64，普通用户（非管理员），系统盘与目标盘均为本地 NTFS |
| 构建 | 同一提交构建的 `CoRig.exe`、`CoRig-publish.exe`、`CoRig-launcher.exe`；记录三者 SHA-256 |
| 工具 | `windeployqt`（发布器自带 `--qt-prefix`）、`Get-FileHash`、`icacls`、`netstat`、`curl.exe` 或 PowerShell `Invoke-RestMethod` |
| 夹具 | `examples/FunctionLab.crpj`、`SerialPlot.crpj`、`RecordPlayback.crpj`；一个带 Python 插件的自有工程（B 组用） |
| 证据 | 每个用例一个目录 `verification/publish/<编号>/`，含命令、stdout/stderr、截图、文件清单与哈希；汇总写 `verification/publish/report.json` |

每条用例执行前记录：构建提交、三个 exe 的哈希、Windows 版本、是否管理员、目标目录文件系统。证据里不得出现令牌；需要展示连接记录时先脱敏。

## 矩阵总览

| 编号 | 目的 | 状态 |
|---|---|---|
| A1 | 单 exe 独立启动 | 已验证（W3） |
| A2 | exe 目录不可写时明确拒绝 | 已验证（W3） |
| A3 | exe 旁只新增工作根 | 已验证（W3） |
| A4 | 全程仅本机回环，无外连 | 可执行（真断网未覆盖） |
| B1–B6 | 载荷 Python 闭包与来源证明 | 部分验证（W3）：B1/B2/B5/B6 通过；B3/B4 未执行 |
| C1 | 只给数据目录即可定位实例 | 已验证（W3） |
| C2 | describe 暴露未上屏参数 | 已验证（W3） |
| C3 | 改参并读回 | 已验证（W3） |
| C4 | 异步任务等待 | 已验证（W3 黑盒） |
| C5 | 令牌不泄露 | 已验证（W3） |
| C6 | 无前文接入 | 已验证（W3 黑盒） |
| D1 | 发布下无编辑入口 | 已验证（W3） |
| D2 | 改只读/Python 偏好被拒 | 已验证（W3） |
| D3 | 批处理与间接入口全拒 | 已验证（W3） |
| D4 | 运行期命令不被误拒 | 已验证（W3） |
| E1 | 运行期落点可枚举 | 可执行 |
| F1 | 留存开时可导出原始工程 | 已验证（W3） |
| F2 | 留存关时无导出通道 | 已验证（W3） |
| G1 | 原生测试全通过 | 已验证（W3，12/12） |
| G2 | 端到端驱动通过 | 可执行（需 Paramiko） |
| G3 | 能力面不缩水 | 可执行 |
| G4 | CLI 与多实例 | 可执行 |
| H1 | 截断/篡改被拒 | 可执行 |
| H2 | 并发解包只产生一个载荷 | 可执行 |
| H3 | 多代载荷清理与占用保护 | 可执行 |
| H4 | 批处理夹带设计命令被整批拒绝 | 已验证（W3） |
| H5 | 导出哈希与清单一致 | 已验证（W3） |
| H6 | 参数跨重启保持且不弄脏工程 | 已验证（W3） |
| H7 | 无 `.ok` 时强校验重解包 | 可执行 |
| H8 | 直接运行载荷内 exe 仍是发布模式 | 可执行 |
| H9 | 改 `read_only`/`python` 被拒 | 已验证（W3） |
| H10 | 控件自动化允许、工程导出被拒且未声明 | 部分（controls.list/click 已验证） |

## A 启动与便携性

### A1 单 exe 独立启动
- **目的**：证明分发物只有单个 exe，接收方无 CoRig、无 Python、无安装步骤。
- **步骤**：新建空目录（路径含空格与中文）→ 只放入 `<output>.exe` → 双击 → 等待主窗口 → 读取 `app.state`。
- **证据**：目录清单（只 1 个文件）、窗口截图、`GET /v3/schema` 返回 200、`app.state.app_mode=publish`、`GET /v3/describe` 的 `app.mode`。W3 实测：FunctionLab.exe 109,745,972 B，首次启动 2.82s，解包到 `98bbf17ac0879e69`；含空格+中文目录 2.84s。
- **前提**：目录可写；x64 系统。分别测试本地盘、U 盘、移动后换盘符三种位置。
- **失败判据**：缺 DLL 弹窗、5 秒内退出、窗口打开但页面空白/加载失败、需要手动安装 VC 运行库或 Python。

### A2 exe 目录不可写时明确拒绝
- **目的**：确认工作根不可创建时给出确定失败，而不是静默回退到系统盘或半启动。
- **步骤**：把 exe 放入目录 → `icacls <目录> /deny <当前用户>:(W)` → 双击 → 记录退出码与 stderr → 恢复权限。
- **证据**：退出码 3、stderr 一行 `{"code":"WORK_ROOT_UNWRITABLE","phase":"work-root",…}`、目录内无新增文件、系统盘无同名新目录。W3 实测：icacls deny 后 exit 3，exe 旁无 `.data`。
- **前提**：允许对自有目录设置 deny；若无法自锁，改用只读介质或受限共享目录并注明差异。
- **失败判据**：弹窗后继续运行、在 `%LOCALAPPDATA%`/`%TEMP%` 出现解包目录、退出码不是 3、无诊断输出。
- **注**：本条在便携工作根方案下**预期为拒绝启动**；旧方案“只读 exe 目录 + LocalAppData 缓存”已废弃。

### A3 exe 旁只新增工作根
- **目的**：确认运行期写入严格限制在 `<exe 主文件名>.data`。
- **步骤**：记录 exe 所在目录树哈希（递归文件名+大小+SHA-256）→ 启动→运行→截图→退出 → 再次记录并对比。
- **证据**：前后哈希清单、只新增 `<exe 主文件名>.data/`（内部 `data/`、`cache/`）、exe 旁无 `project/`、`plugins/`、`settings.ini`、`cache/`。 W3：顶层仅 `cache`/`data`；`%LOCALAPPDATA%/CoRig` 零新增/修改，`%APPDATA%` 无 CoRig/FunctionLab 相关变化，`%TEMP%` 无新会话目录。
- **前提**：目录初始为空；测试前后进程已退出。
- **失败判据**：exe 旁出现工作根之外的持久文件、工作根内出现第二套 `project/` 或 `plugins/`。

### A4 全程仅本机回环
- **目的**：确认发布应用没有后台外连。
- **步骤**：启动应用并保持 5 分钟，每 5 秒 `netstat -ano` 采样过滤该 PID → 同时用 `Get-NetTCPConnection -OwningProcess` 复核 → 记录远端地址集合。
- **证据**：采样原始输出、唯一远端地址集合只含 `127.0.0.1`（本地 API）与用户显式发起的设备连接。
- **前提**：应用处于空闲状态，用户未主动连接设备；本机无法真正断网时在证据中标注。
- **失败判据**：出现任何非用户发起的外部地址或域名解析；出现遥测、更新检查或插件市场的连接。
- **边界**：真断网（拔网卡/隔离网络）本轮未验证，见“未验证边界”。

## B 载荷 Python 闭包

> 状态：部分验证（W3）。B1/B2/B5/B6 已执行并通过；B3/B4 未执行。发布器收集闭包，`loadPublishedProject` 在载荷含 `python/python.exe` 时设置载荷解释器。胜出条件是工程含 Python 插件且 `--python-exe` 指向装好依赖的解释器。运行期在载荷含 `python/python.exe` 时由 `loadPublishedProject` 设置载荷解释器环境，编辑器仍用本机设置；B1/B2/B5 可在接收机未装 Python 的条件下执行。`python.probe` 在发布模式被拒，验证请用发布报告与载荷解释器自检。

### B1 标准库、第三方库与 `.pyd`
- **方法**：发布一个声明 `numpy`、`sqlite3` 的工程（`--python-exe` 指向已安装这些分发的解释器）→ 检查发布报告 `python.checks` → 用 `<载荷>/python/python.exe -I -c "import numpy,sqlite3,sys;print(sys.executable,sys.prefix)"` 检查来源 → 运行插件做一次真实计算。
- **证据**：`python.checks` 全 `ok`；解释器、`prefix` 与模块路径均在 `<载荷>/python` 下；计算输出与执行次数正确。W3：PythonOnly 载荷 `d1c15e3fab63a324`；第三方库载荷 `198c7438367a6044` 读回 paramiko=5.1.0、cryptography=50.0.2；发布模式不要调用 `python.probe`（白名单拒绝）。
- **前提**：发布机装好分发的解释器；接收机未装 Python。
- **失败判据**：`ModuleNotFoundError`、解释器路径指向发布机或系统目录、`.pyd` 无法加载。

### B2 两类 Python 节点实算
- **方法**：建图加入 `function.python` 与 `protocol.python`，注入已知输入，比对输出与 executions。
- **证据**：`node.state` 的输入/输出/耗时、两次运行结果一致。W3：第三方库 `executions>0`、`outputs={paramiko:5.1.0,cryptography:50.0.2}`；PythonOnly 退出码 0、无残留 python。
- **前提**：工程随载荷携带插件包。
- **失败判据**：节点缺失、结果错误、执行次数不增长。

### B3 依赖冲突报告
- **方法**：两个插件声明互斥的 `requires` → 发布。
- **证据**：发布失败，`{ok:false,error:{code:"PY_CONFLICT",phase:"collect",detail:"…"}}`；`detail` 列出分发名、已安装版本与冲突的 specifier。发布报告只保留 `code/phase/detail`，没有独立 `conflicts[]` 字段。**W3 未执行**。
- **前提**：构造冲突声明。
- **失败判据**：无报告、静默择一、发布成功但运行期 ImportError。

### B4 发布失败可读
- **方法**：故意缺一个依赖 → 发布。
- **证据**：`{ok:false,error:{code:"PY_DECL_UNRESOLVED",phase:"collect",detail:"…"}}`（未给 `--python-exe` 时为 `PY_INTERPRETER`）；detail 指出缺失分发与要求的 specifier；`PY_COLLECTOR_MISSING` 会列出已搜索位置。**W3 未执行**。
- **前提**：本地无该 wheel 且无网络。
- **失败判据**：只有弹窗、无阶段信息、错误里没有包名。

### B5 来源证明
- **方法**：在接收机用载荷内 `python/python.exe -I -c` 打印 `sys.executable`、`sys.prefix` 与模块 `__file__`（发布模式不开放 `python.probe`）。
- **证据**：三个路径都在载荷根之下（目录载荷为 `--output`，单 exe 为 `<工作根>/cache/payloads/<载荷 ID>/`）。
- **前提**：临时把 worker 指向一个系统路径文件做反证。
- **失败判据**：任一来源越出载荷根，或反证时未失败。

### B6 离线闭包
- **方法**：断开网络 → 用已装好依赖的解释器发布含 Python 插件的工程 → 在未装 Python 的接收环境运行。
- **证据**：发布成功、`python-runtime.json` 与 `payload-index.json` 含 `python/` 文件、载荷解释器可离线导入声明模块、接收方无 Python 时节点仍可计算。W3 只证明日志无 pip/下载痕迹，**真断网未验证**。
- **前提**：发布机依赖已安装（发布器只复制，不接受 wheel 目录）；本机无法真断网时用无网络命名空间或禁用网卡并注明。
- **失败判据**：发布过程访问索引或联网下载、载荷缺分发、接收后仍需装包。

## C AI 接入

### C1 只给数据目录即可定位实例
- **方法**：不告诉测试者端口，只给 `<exe 主文件名>.data/data`；读取 `.sessions/api.json` → 调 `GET /v3/schema`。
- **证据**：schema 200、`application=CoRig`、`api_version=3`、记录中 `mode=publish` 与 `app_id` 一致。
- **前提**：应用已启动。
- **失败判据**：需要读源码、猜端口、遍历进程或要求 UI 显示端口。

### C2 describe 暴露“未上屏”的参数
- **方法**：工程里放一个只暴露部分参数的视图 → `GET /v3/describe` → 找视图未显示的运行参数 → 用该参数条目的 `write` 模板（`node.configure`）修改 → `node.state` 读回。
- **证据**：`capabilities.parameters` 含该参数（`node/name/type/range/writable=true`；发布模式可调参，`editable` 仍为 false），用 `write` 模板 `node.configure` 修改后 `node.state.config` 变化。W3 实测 `line_width` 的 `writable=true`。
- **前提**：`/v3/describe` 可用（已实现）。
- **失败判据**：只能看到 UI 上屏参数、只有参数名没有类型/范围、改不了或读不回。

### C3 改参并读回
- **方法**：`node.configure`（不带 `persist` 或 `persist=false`）→ `node.state`；`params.set` → `params.get`。
- **证据**：读回值与写入一致；`app.state.dirty=false`；载荷内 `project/runtime.crpj` 哈希不变。
- **前提**：参数在插件声明范围内。
- **失败判据**：值不一致、工程被写、需要重启才生效。

### C4 异步任务等待
- **方法**：触发一个返回 `job` 的动作（如 `node.action` 的连接/发送）→ 轮询 `job.get`。
- **证据**：`state=complete` 且 `result` 可读；失败时 `state=failed` 且有 `error`。
- **前提**：无硬件时用软件回环。
- **失败判据**：提交成功即当作完成、无限轮询无终态、`job.get` 找不到该 ID。

### C5 令牌不泄露
- **方法**：完成 C1–C4 后扫描 `data/`（logs、captures、params、.sessions 中的非记录文件）、工程文件、发布报告、CLI stdout/stderr、截图文字区域。
- **证据**：令牌明文在除实例记录以外的位置 0 命中；`/v3/describe` 与 `app.connection` 文本不含令牌。
- **前提**：测试期间重启过一次应用（覆盖“旧令牌残留”）。
- **失败判据**：任一非记录位置命中令牌、CLI 打印令牌、截图里可见令牌。

### C6 无前文接入
- **方法**：新会话只给 `README.md` 与 `AI_ACCESS.md`（不给路径、端口、命令清单）→ 完成 C1–C4。
- **证据**：会话记录与最终命令序列；无提问“端口是多少”“怎么连”。
- **前提**：执行者未参与开发；可人工扮演。
- **失败判据**：需要问人、读源码、反复试错才连通。

## D 只读边界

### D1 发布下无编辑入口
- **方法**：在发布应用里检查菜单、右键菜单、工具栏、快捷键 → 逐一尝试保存/另存为/导出工程/插件管理/Python 设置 → 记录 UI 表现与 API 结果。
- **证据**：截图对照表；菜单栏保留“应用”（复制 AI 接入信息、导出工程…仅留存开启时、关闭窗口），文件/编辑菜单隐藏，设置菜单只剩资源额度/后台任务/主题；工作区与标签只剩运行/日志，关闭标签、添加工作区、标签右键菜单消失；插件管理与本地 Python 环境不可见；连接视图“配置”与日志双击定位不可用。API 侧对应命令返回 400。W3 视觉：运行页无应用名+版本标题；设置菜单 300×180、3 项、无空白占位。
- **前提**：发布应用已加载工程；`retain_original` 分别开/关各测一次。
- **失败判据**：存在可点击且不报错的编辑入口；入口点击后静默无反应（不可诊断）；留存关闭时仍出现“导出工程…”。

### D2 改只读偏好被拒
- **方法**：`preferences.set {read_only:false}`、`preferences.set {python:"…"}`、`preferences.set {theme:"light"}`。
- **证据**：前两条 400（“发布运行模式不允许修改此设置”）；主题成功且 `app.state.theme` 变化。
- **前提**：无。
- **失败判据**：`read_only` 被改成 false、Python 路径被接受、主题也失败。

### D3 批处理与间接入口
- **方法**：分别提交 `batch` 含 `project.save`/`project.new`/`plugin.import`/`node.configure(persist=true)`/`app.restart`；确认整批被拒，且批内允许命令也未执行。
- **证据**：400 错误、批内允许命令的副作用不存在（如 log 未清）、`app.state` 未变。
- **前提**：使用能产生可观测副作用的允许命令作为“夹带”（如 `log.clear`）。
- **失败判据**：任一子命令成功、前面的子命令已生效、错误码与其他拒绝路径不一致。

### D4 运行期命令不被误拒
- **方法**：`runtime.start/pause/step/reset/stop`、`node.action`、`capture`（显式 path 与缺省 path 各一次）、`log.query`/`log.export`、`view.read`。
- **证据**：全部成功；截图文件存在且可打开；日志导出文件非空。
- **前提**：工程可运行，无硬件依赖。
- **失败判据**：白名单内的运行期命令被拒、截图落点越出工作根。

## E 运行期落点

### E1 运行期写入位置可枚举
- **方法**：首次启动后列 `<exe 主文件名>.data` 全树 → 截图（缺省与显式路径各一次）→ `log.export` 到显式路径 → `params.set` → 退出 → 再列全树。
- **证据**：三份清单——工作根、应用目录、系统目录（`%LOCALAPPDATA%`、`%TEMP%`）；预期：
  - 工作根：`data/settings.ini`、`data/.sessions/`、`data/params/user-params.json`、`data/captures/<id>.png`、`data/diagnostics/`（崩溃时）、`cache/{qml,shaders,temp/<pid>,plugin-packages,payloads}/`；
  - 显式路径：用户指定位置的日志与截图；
  - 应用目录：无新增；系统目录：无新增。
- **前提**：发布应用只读，无“保存工程”路径——工程保存不属于本用例。
- **失败判据**：出现未列出的持久路径、写系统目录、把参数写进载荷。

## F 留存开关

### F1 留存开时可导出原始工程
- **方法**：`--retain-original` 发布 → 运行 → `project.export_original` 导出 → 用编辑器打开导出文件。
- **证据**：导出文件 SHA-256 与 `payload-index.json` 中 `project/runtime.crpj` 条目一致；编辑器可打开并运行；`/v3/describe` 的 `capabilities.exports` 含 `available=true` 的 `project.export_original`。W3：5,901,016 B，SHA-256 与载荷 `runtime.crpj` 一致；改参后再导出仍等于原始快照；目标存在需 `overwrite`。
- **前提**：`retain_original=true`；目标路径可写。
- **失败判据**：命令不存在/报错、导出字节与清单不一致、导出物打不开、目标已存在且未传 `overwrite` 时被覆盖。
- **说明**：实现口径为“复制载荷内 `project/runtime.crpj` 字节”，不是发布机磁盘上的原工程文件；导出只在该发布实例上进行，不修改载荷。

### F2 留存关时无导出通道
- **方法**：`--retain-original` 未开 → `project.export_original` → 也检查 `project.get`、`project.export`、`project.save`。
- **证据**：全部 400；`/v3/describe` 的 `capabilities.exports` 为空。
- **前提**：无。
- **失败判据**：任一通道能取得完整工程；`exports` 非空或标 `available=true`。

## G 回归

### G1 原生测试
- **方法**：`tools/build_native.ps1 -Test`。
- **证据**：CTest 12/12 通过（W3，55.87s）：`plugins`、`editor`、`w1_publish`、`user_params`、`describe`、`command_registry`、`workbench_ui`、`core`、`execution`、`control`、`recording`、`io`。
- **前提**：MSVC/Qt 环境（见[构建与发布](BUILDING.md)）。
- **失败判据**：任一套失败；新增测试未纳入 CTest。

### G2 端到端驱动
- **方法**：依次运行 `tools/native_*_check.py` 的 release、ui、workbench、instrument、animation、recording、ai、plugin、installer、storage 驱动。
- **证据**：各自 `report.json` 的 `failed` 为空；AI 驱动覆盖命令数与 `schema` 一致。
- **前提**：本机 SSH 服务器验收需 Paramiko；安装/更新验收用临时本地仓库。
- **失败判据**：任一驱动失败；报告与当前版本不匹配。

### G3 能力面不缩水
- **方法**：编辑器构建读取 `registry` 与 `schema`；三示例实算。
- **证据**：官方节点 80 个、公开操作数与 `schema.commands` 一致（发布白名单之外仍完整）；FunctionLab/SerialPlot/RecordPlayback 运行正常。
- **前提**：与基线版本对比。
- **失败判据**：节点数减少、命令缺失、示例失败。
- **当前基线**：0.4.4 开发线中命令注册表与运行时 `schema` 均为 134 个操作（含 `project.export_original`、`settings.open`、`publish.*`）；以 `schema` 为公开操作数的唯一来源。

### G4 CLI 与多实例
- **方法**：两个发布实例（不同 exe 或同一 exe 两次启动）→ `CoRig-cli api app.state --data-dir …`；再指定 `--discovery`。
- **证据**：默认探测回退到存活实例；指定记录命中目标实例；两实例 `pid` 不同、`app_id` 可区分。
- **前提**：两实例都已启动。
- **失败判据**：连到已退出实例不重试、拿错工程、CLI 打印令牌。

## H 载荷与容器

### H1 截断或篡改被拒
- **方法**：分别对单 exe 做三组操作——截掉末尾 1 字节、截掉末尾 64 字节、翻转 payload 中一个字节——各运行一次。
- **证据**：退出码 2；stderr 一行 `{"code":"PAYLOAD_CORRUPT","phase":"container|index","detail":…}`；`--diagnose` 同样失败；工作根未产生新载荷目录。
- **前提**：任一可执行单 exe。
- **失败判据**：退出码 0、窗口出现、解包出部分文件、错误无 code。

### H2 并发解包只产生一个载荷
- **方法**：删除工作根 → 同一 exe 连续快速启动两次（脚本同时拉起）。
- **证据**：`cache/payloads/` 只有一个载荷目录、无 `.tmp-*` 残留；两个进程都进入发布模式；`.lock` 存在。
- **前提**：首次解包耗时足够重叠（大载荷或慢盘）。
- **失败判据**：两份载荷目录、`.tmp-*` 残留、其中一个进程报错退出。

### H3 多代载荷清理与占用保护
- **方法**：准备 4 个不同载荷的单 exe（改版本或改工程）→ 依次运行前三个并退出 → 启动第四个 → 再启动第三个并保持运行 → 启动第五个。
- **证据**：`cache/payloads/` 保留最新 3 代（含当前）；被运行实例持锁的目录未被删除；`.tmp-*`/`.broken-*` 超 24 小时才消失。
- **前提**：可构造多个载荷；可控制运行与退出时序。
- **失败判据**：删除了当前或被持锁的载荷、保留超过 3 代、残留目录永不清理。

### H4 批处理夹带设计命令被整批拒绝
- **方法**：`batch` 内先放一个允许命令（`log.clear`）再放 `project.save`；另测单独调用 `project.save`。
- **证据**：整批 400；`log.query` 仍能查到旧日志（未被清空）；`app.state` 无变化。
- **前提**：有可观测副作用的允许命令。注：`node.configure(persist=true)` 在当前发布模式被允许并写入用户参数覆盖（见 H6），不再作为拒绝用例。
- **失败判据**：部分执行、错误只针对子命令而批仍成功。

### H5 导出哈希与清单一致
- **方法**：`--retain-original` 发布 → 运行 → `project.export_original` → 计算导出文件 SHA-256。
- **证据**：与 `payload-index.json` 的 `project/runtime.crpj` 条目一致，也与 `manifest.json` 的 `release.snapshot_sha256` 一致。
- **前提**：`retain_original=true`；导出处理器按路径写入并返回 `{exported,path}`。
- **失败判据**：哈希不一致、导出的是发布机原文件（含未嵌入插件）。

### H6 参数跨重启保持且不弄脏工程
- **方法**：`params.set` 改一个参数 → `app.restart` → `params.get`/`node.state` 读回 → 检查 `app.state.dirty`、载荷哈希、`data/params/user-params.json`。
- **证据**：值保持；`dirty=false`；`project/runtime.crpj` 哈希不变；`user-params.json` 含 `format=corig-user-params`、`release_hash`、`nodes`/`controls`。W3：`line_width` 1.625→退出→0.78s 重启读回 1.625。
- **前提**：参数属于用户可调范围。
- **失败判据**：重启后丢失、写入载荷、工程变脏、参数文件损坏导致启动失败（应改名 `.bad` 并回到出厂值）。

### H7 无 `.ok` 时强校验重解包
- **方法**：① 删除载荷目录的 `.ok` 后启动；② 删除 `.ok` 并改动其中一个文件后启动；③ 保留 `.ok` 但改动文件后启动。
- **证据**：①② 重新解包并校验，②退出码 2 且保留 `.broken-*`；③ 直接复用（已知边界：`.ok` 存在即信任，不重复校验）。
- **前提**：可编辑工作根。
- **失败判据**：①不重新解包、②损坏文件被接受、③行为与文档不符。

### H8 直接运行载荷内 exe 仍是发布模式
- **方法**：进入 `<工作根>/cache/payloads/<载荷 ID>/runtime/` 直接运行 `CoRig.exe`（不设 `CORIG_APP_ROOT`）。
- **证据**：`app.state.app_mode=publish`；数据仍写 `<工作根>/data`；`describe.app.mode=publish`。
- **前提**：载荷布局完整（`cache/payloads` 结构）。
- **失败判据**：进入编辑器、数据目录落到别处、报“应用载荷不完整”。

### H9 改 read_only / python 偏好被拒
- **方法**：`preferences.set {read_only:false}`、`{python:"C:\\Python311\\python.exe"}`。
- **证据**：均 400；`app.state.read_only=true`、`app.state.python` 未变；reload 后仍不变。
- **前提**：无。
- **失败判据**：任一被接受、状态变化但报错不一致。

### H10 控件自动化允许、工程导出被拒且未声明
- **方法**：`controls.list`、`controls.get`、`controls.click`、`project.export`、`project.get`。
- **证据**：`controls.list` 在发布运行页返回实际对象；`controls.click` 在最小化发布应用点击“暂停绘制”，实际图标切换；`project.export`、`project.get` 返回 400，`schema.signatures[op].publish.policy=deny`，`describe.capabilities` 不含对应条目。`controls.set` 尚未单独补证。
- **前提**：运行视图存在（用于证明控件发现不是空列表）。
- **失败判据**：工程编辑命令成功；schema 未标 deny；describe 声称编辑可用但调用失败；控件操作造成系统输入或抢焦点。

## 未验证边界

以下项目本轮（只读设计阶段）无法取得有效证据，必须在实机阶段单独执行并在报告中标注：

- **干净机**：当前验证在开发机进行，已装 MSVC 运行库、Qt、Python 与字体；A1 已在开发机实测 2.82s，但未在未装开发环境的 Windows 上复测，不能据此宣称干净机可用。
- **真断网**：A4/B6 的离线结论依赖 netstat 采样与日志无 pip/下载痕迹；没有隔离网络或断网手段时，外连只能“未观测到”，不能宣称“不存在”。
- **跨用户 ACL**：A2 只验证“当前用户不可写”；受限用户、只读共享、组策略重定向目录均未覆盖。
- **代码签名与 SmartScreen**：无签名，未验证下载后的信誉提示与企业策略拦截。
- **杀毒/EDR**：未验证单 exe 自解包是否被启发式拦截。
- **网络盘与只读介质**：未验证 UNC 路径、只读 U 盘、同步盘（OneDrive）下的工作根行为。
- **长时间与压力**：未验证参数文件频繁写入、截图批量导出、多实例并发下的稳定性。
- **性能**：已测 FunctionLab 单 exe 首次启动 2.82s（开发机）；未测干净机、数百 MB 大载荷、GPU/渲染帧率与压力下的启动耗时。
- **多语言与高 DPI**：未验证非中文系统与 200% 缩放下的窗口/截图。
- **重新打包**：t37 视觉修正在 t36 验收期间完成；截图对应 exe sha `f3aea8…`，但修正后的源码重新打包未再复验。
- **真实设备**：未连接真实串口/设备；软件回环与内置视图不能替代硬件验收。
- **迁移与升级**：未验证跨版本参数覆盖重放（节点改名、参数类型变化）与旧载荷清理的实际顺序。

这些边界不影响“发布物可分发”的结论，但不能被引用为“已在任意 Windows 上可用”。

## 报告格式

`verification/publish/report.json`：

```json
{
  "version": "0.4.4",
  "built_at": "2026-11-04T…",
  "artifacts": [{"name":"FunctionLab.exe","sha256":"…","bytes":0}],
  "environment": {"windows":"10.0.26100","admin":false,"clean_machine":false,"offline":false},
  "cases": [{"id":"A1","status":"passed","evidence":["A1/list.txt","A1/window.png"],"notes":""}],
  "unverified": ["clean machine","real offline","cross-user ACL"]
}
```

`status` 取值：`passed`、`failed`、`blocked`（依赖未实现）、`planned`（能力未实现）、`skipped`（环境不具备，须写明原因）。矩阵总览与报告必须一致；没有证据的用例不得标 `passed`。
