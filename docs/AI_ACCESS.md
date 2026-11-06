# 从 AI 接入发布应用

[项目首页](../README.md) · [发布指南](PUBLISHING.md) · [本地 API](API.md) · [发布应用验证](VERIFICATION_PUBLISH.md)

发布应用不是服务，也没有安装步骤：用户双击运行后，本机出现一个只监听 127.0.0.1 的 v3 接口。AI 或脚本要做的只有三件事——找到当前实例的连接记录、带上令牌调用、按需轮询异步任务。

本文只写发布应用与编辑器的差异；命令字段、值编码和错误结构以 [本地 API](API.md) 与运行时 `schema` 为准。

## 实现状态

| 能力 | 状态 | 说明 |
|---|---|---|
| 实例记录与别名 `api.json` | 已验证（W3） | `<工作根>/data/.sessions/`；退出后别名删除，CLI 可重新发现 |
| `GET /v3/schema`、`POST /v3/command` | 已验证（W3） | Bearer 401 覆盖全部路由；schema/commands 均 134 项 |
| `GET /v3/describe` | 已验证（W3） | 身份、策略、schema 摘要与能力清单与实测一致 |
| `app.connection`（复制接入信息） | 已验证（W3） | 纯文本接入即可发现并调用；文本不含令牌 |
| `params.get/set/reset` 参数覆盖 | 已验证（W3） | 改参→重启读回；写入 `data/params/user-params.json`，不弄脏工程 |
| `project.export_original` | 已验证（W3） | 仅 `retain_original=true`；导出哈希等于载荷 `runtime.crpj`；无留存变体拒绝；目标存在需 `overwrite` |
| 结构化错误码（`code` 字段） | 计划中 | 当前失败只有 `{ok:false,error:"…"}` |

## 前提

- **应用必须已经启动**：发布应用不注册服务、不自动拉起。没有进程就没有监听端口，任何“远程接入”都不成立。
- **仅本机**：接口只监听 `127.0.0.1`，不接受局域网或远程地址；也不处理跨域请求，不要在浏览器网页里直接调用（没有 CORS 响应头），用命令行、脚本或本机程序。
- **版本**：`api_version=3`。连接记录与 schema 都声明 `application=CoRig`；客户端应像 `CoRig-cli` 一样校验，避免连到别的产品或旧实例。

## 找到实例

发布应用的数据目录固定在工作根内，不受 `--data-dir` 影响：

```text
<exe 主文件名>.data/data/.sessions/
├─ api.json                              ← 别名：最近一次启动发布的实例记录
└─ instances/
   └─ api-<pid>-<port>.json              ← 每个进程一条，退出时删除
```

记录字段：`application`、`api_version`、`url`、`token`、`instance_id`、`pid`、`started_at`、`discovery`、`app_id`、`app_name`、`app_version`、`release_hash`、`mode`（`publish`）、`retain_original`。

- 单实例：直接读 `api.json`。
- 多实例：`api.json` 只指向最近启动的实例；要指定某个实例，列 `instances/` 并按 `app_id`、`pid`、`started_at` 或工程选择，或让 `CoRig-cli` 自动探测（它先读 `api.json`，再按时间列 `instances/`，逐个请求 `GET /v3/schema`，取第一个可认证的实例）。
- 别名更新有锁 `.api-discovery.lock`（3 秒超时）；读到“本地连接信息正在更新”时稍后重试，不要自行改写这些文件。
- 实例退出时只删除自己的 `instances/` 记录；`api.json` 仅在令牌匹配时删除，所以别名可能指向已退出进程——连接失败就回退到 `instances/`。

## 复制接入信息

`app.connection` 返回 `{text,path,url}`：`text` 是可直接粘贴到对话或脚本说明里的接入指引，`path` 指向实例记录，令牌**不在** `text` 里；调用方从 `path` 本地读取令牌。`/v3/describe` 同样只返回脱敏后的 `instance`（`instance_id/pid/url/started_at/data_dir`），从不返回令牌。

令牌规则：每次启动随机生成 32 字节，以十六进制字符串写入记录，仅供本机调用；重启后旧令牌立即失效（401 时重新读取记录）。令牌不得写入工程、脚本源码、日志、截图或对话；不要把整个 `api.json` 贴进聊天——它包含令牌。

## W3 黑盒接入结果

独立验收只给执行者一段 `app.connection.text`（不含路径、端口与令牌），完成：从文本找到 `time.period_ms`，由 25 改为 40；`job.get` 轮询到终态；运行日志显示 tick 从 32.3/s 降到 21.6/s；全流程令牌未出现在工程、日志、截图或对话中。该结果证明接入文本足以完成发现、鉴权、读取能力、调参与异步等待，不需要先读源码。

适用范围仍是同机环回；干净 Windows VM、真断网与跨用户 ACL 未验证（见[发布应用验证](VERIFICATION_PUBLISH.md)）。

## 协议

```http
GET /v3/describe
Authorization: Bearer <token>

POST /v3/command
Authorization: Bearer <token>
Content-Type: application/json

{"op":"node.state","args":{"node":"gain"}}
```

| 项 | 值 |
|---|---|
| 认证 | 全部请求（含 `schema`/`describe`）要求 `Authorization: Bearer <token>` |
| 请求体 | JSON 对象 `{op,args}`；必须带 `Content-Length`，最大 8 MiB；缺少或非法 `Content-Length` 按 413 拒绝，分块传输按 400 拒绝 |
| 请求头 | 最大 16 KiB，超出返回 431 |
| 超时 | 单连接 10 秒，超时直接断开；每次连接只处理一个请求 |
| 成功 | `200 {ok:true,result:…}` |
| 失败 | `400 {ok:false,error:"原因"}`（无结构化 code）；401 认证失败；404 路由不存在；413 请求体超限 |

`/v3/describe` 返回：

```json
{"app":{"name":"FunctionLab","id":"org.corig.functionlab","version":"1.0.0","release_hash":"…","mode":"publish"},
 "instance":{"instance_id":"…","pid":1234,"url":"http://127.0.0.1:51234","started_at":…,"data_dir":"…"},
 "policy":{"retain_original":false,"editable":false},
 "schema":{"version":3,"digest":"…"},
 "capabilities":{"parameters":[…],"metrics":[…],"actions":[…],"views":[…],"exports":[…]}}
```

`capabilities` 每项都带现成的 `read` / `write` / `call` 调用模板（`node.state`、`node.configure`、`node.action`、`view.read`、`view.actions`、`workspace.list` 等），`available=false` 时附 `reason`（如“运行会话尚未建立”）。发布应用的 `policy.editable` 恒为 `false`，表示不能编辑设计；**参数条目的 `writable` 在发布模式为 `true`**（`write` 模板为 `node.configure`），实测 `line_width` 的 `writable=true`——可调参不等于可写回设计。不要用 `editable=false` 推断参数不可写。

> 注意：当前实现的 `policy.editable` 写死为 `false`，编辑器实例同样如此；不要用它判断编辑器是否可写。

## 发布应用能做什么

发布模式按命令白名单执行，白名单由命令注册表生成，`schema` 中的 `signatures[*].publish`（`policy` 为 `allow|deny|conditional` 与 `reason`）是机器可读版本。当前 allow 46、conditional 2、deny 86；allow 的 46 个操作：

- **观测**：`app.state`、`runtime.state`、`node.state`、`view.read`、`view.actions`、`view.call`、`log.query`、`io.ports`、`resources.get`、`registry`、`schema`、`job.get`、`job.list`、`workspace.list`
- **运行控制**：`runtime.start`、`runtime.stop`、`runtime.pause`、`runtime.reset`、`runtime.step`、`runtime.debug`、`runtime.emit`、`node.action`、`node.configure`
- **用户参数**：`params.get`、`params.set`、`params.reset`（仅发布模式；写入 `<工作根>/data/params/user-params.json`，不改工程、不标记未保存）
- **会话与窗口**：`app.quit`、`app.restart`、`window.close`、`workspace.open`、`workspace.close`、`settings.open`
- **运行时控件**：`controls.list/get/set/click/focus/commit/key/pointer`（只操作实际 Qt 对象，不注入系统输入）
- **日志与导出**：`log.clear`、`log.configure`、`log.export`、`capture`
- **接入**：`app.connection`、`job.cancel`

两条条件命令（`conditional`，进入处理器后按参数自行校验）：

- `preferences.set`：发布模式只接受 `theme`；`python` 与 `read_only` 直接报错“发布运行模式不允许修改此设置”。
- `project.export_original`：仅 `retain_original=true` 的发布实例可用；`path` 必填，目标存在需 `overwrite=true`；导出载荷内 `project/runtime.crpj` 的字节。

`batch` 本身在发布模式为 `deny`：整批不可用，避免用批处理夹带设计命令。

注册表另有 5 个参数类型标记为 `unknown`（`controls.set.value`、`log.export.batch`、`log.query.batch`、`params.set.value`、`runtime.step.source`），`schema` 如实保留，调用时仍需按运行时类型传值。

调参有两层语义：

- `node.configure` 是运行控制命令：`persist` 默认 `false`，只改当前运行；传 `persist=true` 时改动同时写入用户参数覆盖并保留到重启。发布模式只接受不改变节点端口/连线的参数：会让端口集合变化的修改（例如动态输出列表）直接报错，不会删除既有导线。
- `params.set/get/reset` 是用户参数入口：`params.set` 先按节点声明校验参数名、类型和范围，并拒绝会改变节点端口/连线的值；控件值按当前上下限/选项校验，暂停时非实时参数按设计值变更处理。随后执行与编辑器控件相同的运行路径（控件 `value` 走 `node.action set_value`，其他参数走 `node.configure`），并更新 `data/params/user-params.json` 与内存工程副本；运行会话已建立时返回 `job`，引擎拒绝的修改会回滚覆盖层。`params.reset` 重建运行会话以恢复发布时的初始值。

两者都受发布 `allow` 策略允许，都不写回设计；需要跨重启保留时用 `persist=true` 或 `params.set`。

### 后台操作与设置

窗口被遮挡或最小化时，API 的 `workspace.open` 默认 `activate=false`：只切换实际页面和布局，不显示、不恢复、不抢系统前台。需要显式显示时才传 `activate=true`，或用 `window.configure` 的 `focus=true`。`controls.*` 使用应用自身 Qt 对象和内部事件，不移动系统鼠标、不注入系统键盘；控件仍按真实可见/可用条件检查。

发布应用的“设置”菜单现在可打开，只提供“通用”和“后台任务”；编辑器提供通用、工程、插件、Python、资源额度、后台任务。AI 可用 `settings.open(page)` 打开同一窗口并选中页面；关闭窗口用 `settings.close` 控件，旧的 `plugins.close` 已移除。

`capture` 是异步任务：窗口最小化时按需抓取，不恢复、不激活窗口；整个任务（含页面安装、前置 job 和 grabToImage ready）共享 30 秒总时限，超时或前置失败返回明确错误。结果中的 `observation` 是请求时的观察版本，另有 `observation_scope="request"`、`request_observation`、`completion_observation`；`batch_associated=false` 和 `single_batch=false` 表示请求观察与完成渲染不承诺属于同一采样批次。需要等待业务操作完成时，把操作返回的 job 传给 `capture.job`。

## 明确被拒绝

以下操作返回 `400`，错误文本为“发布运行模式不允许此操作：<op>”：

| 类别 | 命令 |
|---|---|
| 工程与设计 | `project.get/new/open/save/export/options/rename/validate`、`graph.*`、`editor.*`、`sync.*`、`history.*`、`property.*`、`ui.*`、`container.layout`、`node.configure.plan` |
| 批处理 | `batch`（整批不可用，避免夹带设计命令） |
| 插件管理 | `plugin.*`、`python.probe` |
| 更新与维护 | `update.*`、`clipboard.read` |
| 软件发布 | `publish.*`（发布应用不能再次导出） |
| 资源与窗口布局 | `resources.set`、`window.configure`、`window.pin`、`workspace.configure`、`workspace.detach`、`workspace.move`、`workspace.remove` |

被拒命令不会出现在发布应用的实际可调用面里：`schema.signatures` 标 `deny`，`/v3/describe` 不为其生成能力条目。不要用编辑器文档里的 `project.get`、`editor.*`、`publish.*` 去驱动发布应用；运行时控件应使用 `controls.*`，工程编辑仍应被拒绝。

## 异步任务

设计、运行和控件映射后的动作可能返回 `{job:"ID"}`。拿到 ID 不代表已经执行：轮询 `job.get` 直到 `state=complete`（读 `result`）或 `state=failed`（读 `error`）。`job.list` 列当前实例的任务；可协作取消的任务用 `job.cancel`。`CoRig-cli` 的 `--wait` 会等待单个 job 或 `jobs_complete/job_results`。

## 命令行

```powershell
# 命令行调用（自动探测 .sessions）
.\CoRig-cli.exe api app.state --data-dir "D:\Apps\FunctionLab.data\data"
.\CoRig-cli.exe api node.state --json '{"node":"gain"}' --data-dir "D:\Apps\FunctionLab.data\data"
.\CoRig-cli.exe api runtime.start --json-file .\start.json --wait --data-dir "D:\Apps\FunctionLab.data\data"

# describe 是 GET 路由，用任意本机 HTTP 客户端；令牌只从记录里读，不回显
$record = Get-Content "D:\Apps\FunctionLab.data\data\.sessions\api.json" | ConvertFrom-Json
Invoke-RestMethod -Uri "$($record.url)/v3/describe" -Headers @{ Authorization = "Bearer $($record.token)" }
```

`CoRig-cli` 的 `api <op>` 走 `POST /v3/command`；`--discovery <实例记录>` 固定实例，`--timeout <毫秒>` 调整等待。CLI 不依赖 Python，也从不打印令牌。

## 安全边界

- 令牌只存在实例记录中，不写入工程、日志、截图与 `/v3/describe`；日志里的失败信息也不含令牌。
- 接口不加密（HTTP 环回），同机进程理论上可读连接记录；适用场景是“同一台机器上的 AI 协助”，不是多租户或远程访问。
- 发布应用没有账户体系、权限分级或审计日志；能力边界就是白名单。
- 不要把工作根或截图目录整体上传到外部服务：`data/params`、`data/captures`、`data/.sessions` 都含用户数据或凭据。

## 未承诺

- 不承诺纯网页客户端：没有 CORS、没有 WebSocket，也没有静态页面入口。
- 不承诺跨机、容器内穿透或反向代理场景。
- 不承诺结构化错误码；`project.export_original` 的返回字段为 `{exported,path}`，其余字段以运行时 schema 为准。
