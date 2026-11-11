<!-- USER BEGIN feature-map-current -->
# CoRig 0.5.0 功能追踪

更新：2026-11-11。本表为初始化盘点，不表示旧声明均已实现，更不表示新架构已经通过验证。实施规则与大步定义见 [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)，当前结果见 [PROGRESS.md](PROGRESS.md)。

## 1. 来源与维护方式

`archive:/` 表示只读本地归档 `D:\Projects\Software\CoRig-archive\20261111-before-0.5.0` 内的路径。本次盘点以旧 `src/plugins/catalog.json`、`docs/COMMAND_SCHEMA.json`、用户/SDK/发布说明、现有源码位置和 Git 状态为线索。

旧云端提交 `87688404f7e25ad93ae75a3df41b66b7a786fc74` 已保留于 `archive/pre-0.5.0`；本地归档另有未提交修改，云端分支不包含全部本地归档内容。换机继续时必须核对使用哪份基线，不静默把云端旧文件当成已包含这些改动。

初始状态统一为“未实施/未验收”。下列目标是职责和行为，不是已确定的新节点 ID 或新 API 名。源码文件为复用/追踪线索，进入相应大步时必须进一步查调用链、配置、资源、失败路径和测试。旧返回格式、命令名、全局入口与格式兼容不属于必须照搬的能力。

实现后在对应行补实际路径、场景与证据；能合并名称但不能删减用户行为。确认不保留或改变行为必须标明设计决定，不能直接删除行。阶段仅表示主要闭合位置，前期需要的最小部分可以在相应已授权闭环中先做。

## 2. 旧官方节点逐项索引

以下完整列出本次读取到的 80 个 catalog 项。每行共同来源为 `archive:/src/plugins/catalog.json`；实现线索在表内。端口、默认值、参数、视图资源和动作需在实施时从原声明继续展开核对，节点数量不能代替功能等价。

| 旧 ID | 旧名称 | 新能力归属与验收重点 | 主要大步 | 旧实现线索 | 状态 |
|---|---|---|---|---|---|
| `io.serial` | 串口 | 完整串口连接；流控、RTS/DTR、收发、重连及资源回收 | M5 | `src/plugins/io_hub.cpp` | 未实施/未验收 |
| `io.network` | TCP / UDP | TCP/UDP；多客户端、来源、定向/广播及重连 | M5 | `src/plugins/io_hub.cpp` | 未实施/未验收 |
| `io.ssh` | SSH | SSH 连接；认证、指纹、PTY、取消与关闭 | M5 | `src/plugins/ssh_session.cpp` | 未实施/未验收 |
| `protocol.decode` | 数据解码器 | 协议编解码；对应格式往返、坏帧、来源隔离与 Python 支持 | M5 | `src/plugins/protocol_node.cpp` | 未实施/未验收 |
| `protocol.python` | Python 协议 | 协议编解码；对应格式往返、坏帧、来源隔离与 Python 支持 | M5 | `src/plugins/protocol_node.cpp` | 未实施/未验收 |
| `protocol.encode` | 数据编码 | 协议编解码；对应格式往返、坏帧、来源隔离与 Python 支持 | M5 | `src/plugins/protocol_node.cpp` | 未实施/未验收 |
| `protocol.text` | 文本解码 | 协议编解码；对应格式往返、坏帧、来源隔离与 Python 支持 | M5 | `src/plugins/protocol_node.cpp` | 未实施/未验收 |
| `control.number` | 数值输入 | 控件/显示及公开绑定；真实读写、动作、状态和独立视图 | M5 | `src/plugins/instrument_node.cpp` | 未实施/未验收 |
| `control.slider` | 滑块 | 控件/显示及公开绑定；真实读写、动作、状态和独立视图 | M5 | `src/plugins/instrument_node.cpp` | 未实施/未验收 |
| `control.text` | 文本输入 | 控件/显示及公开绑定；真实读写、动作、状态和独立视图 | M5 | `src/plugins/instrument_node.cpp` | 未实施/未验收 |
| `control.checkbox` | 开关值 | 控件/显示及公开绑定；真实读写、动作、状态和独立视图 | M5 | `src/plugins/instrument_node.cpp` | 未实施/未验收 |
| `control.select` | 下拉选项 | 控件/显示及公开绑定；真实读写、动作、状态和独立视图 | M5 | `src/plugins/instrument_node.cpp` | 未实施/未验收 |
| `control.button` | 按钮 | 控件/显示及公开绑定；真实读写、动作、状态和独立视图 | M5 | `src/plugins/instrument_node.cpp` | 未实施/未验收 |
| `display.value` | 数值 / 文本显示 | 控件/显示及公开绑定；真实读写、动作、状态和独立视图 | M5 | `src/plugins/instrument_node.cpp` | 未实施/未验收 |
| `debug.console` | 收发调试 | 收发面板；文本/HEX、编码转义、历史与文件发送取消 | M5 | `src/qml/ConsoleView.qml` | 未实施/未验收 |
| `data.record` | 信号录制 | 分别归录制/回放节点；类型、时间、分段、多流与冲突 | M6 | `src/plugins/recording_node.cpp` | 未实施/未验收 |
| `source.timer` | 定时触发 | 触发器；周期、倒计时、外部启停与重复开始 | M5 | `src/plugins/timer_node.cpp` | 未实施/未验收 |
| `source.time` | 时间 | 常量/时间；命名输出、时间单位、就绪与业务时间边界 | M5 | `src/plugins/data_node.cpp` | 未实施/未验收 |
| `source.constant` | 常量 | 常量/时间；命名输出、时间单位、就绪与业务时间边界 | M5 | `src/plugins/data_node.cpp` | 未实施/未验收 |
| `data.extract` | 通道提取 | 数据提取/分发/门控/选择；来源、断连、本地值和条件 | M5 | `src/plugins/data_node.cpp` | 未实施/未验收 |
| `data.fork` | 数据分发 | 数据提取/分发/门控/选择；来源、断连、本地值和条件 | M5 | `src/plugins/data_node.cpp` | 未实施/未验收 |
| `data.gate` | 通断开关 | 数据提取/分发/门控/选择；来源、断连、本地值和条件 | M5 | `src/plugins/data_node.cpp` | 未实施/未验收 |
| `data.select` | 二选一开关 | 数据提取/分发/门控/选择；来源、断连、本地值和条件 | M5 | `src/plugins/data_node.cpp` | 未实施/未验收 |
| `function.sin` | 正弦 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.cos` | 余弦 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.tan` | 正切 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.asin` | 反正弦 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.acos` | 反余弦 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.atan` | 反正切 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.abs` | 绝对值 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.sqrt` | 平方根 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.exp` | 指数 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.log` | 自然对数 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.log10` | 常用对数 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.floor` | 向下取整 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.ceil` | 向上取整 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.negate` | 取反 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.degrees` | 弧度转角度 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.radians` | 角度转弧度 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.sign` | 符号 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.add` | 加法 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.subtract` | 减法 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.multiply` | 乘法 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.divide` | 除法 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.minimum` | 最小值 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.maximum` | 最大值 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.power` | 幂 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.atan2` | 双参数反正切 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.greater` | 大于 | 条件门控；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.less` | 小于 | 条件门控；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.equal` | 等于 | 条件门控；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.limit` | 限幅 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.linear_map` | 线性映射 | 数学运算；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.sine_wave` | 正弦波 | 信号生成；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.square_wave` | 方波 | 信号生成；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.triangle_wave` | 三角波 | 信号生成；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.sawtooth_wave` | 锯齿波 | 信号生成；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.ramp` | 斜坡 | 信号生成；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.random` | 随机数 | 信号生成；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.integral` | 积分 | 信号处理；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.derivative` | 微分 | 信号处理；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.moving_average` | 滑动平均 | 信号处理；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.lowpass` | 一阶低通 | 信号处理；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.expression` | 数学表达式 | 多命名输入/公式输出表达式；对应运算结果、定义域/边界和运算切换影响 | M5 | `src/plugins/math_node.cpp` | 未实施/未验收 |
| `function.python` | Python 函数 | Python 函数；真实脚本、依赖、异步结果与错误 | M5 | `src/core/python_node.cpp` | 未实施/未验收 |
| `plot.curves` | 实时曲线 | 完整曲线；历史、通道、游标统计、FFT、视图与导出 | M6 | `src/quick/renderers.cpp` | 未实施/未验收 |
| `plot.attitude` | 3D 姿态仪 | 合并姿态/位姿；模型、单位、旋转、参考与失效提示 | M6 | `src/quick/renderers.cpp` | 未实施/未验收 |
| `plot.xy` | 二维坐标显示 | XY；范围、区域、按压/激活和保存恢复 | M6 | `src/quick/renderers.cpp` | 未实施/未验收 |
| `plot.sixdof` | 3D 空间显示 | 合并姿态/位姿；模型、单位、旋转、参考与失效提示 | M6 | `src/quick/renderers.cpp` | 未实施/未验收 |
| `terminal.ansi` | 交互终端 | 终端；ANSI/UTF-8、输入、PTY 尺寸及关闭 | M5 | `src/plugins/terminal_screen.cpp` | 未实施/未验收 |
| `logic.and` | 与门 | 逻辑运算；布尔真值/边沿序列及切换后端口状态 | M5 | `src/plugins/logic_node.cpp` | 未实施/未验收 |
| `logic.or` | 或门 | 逻辑运算；布尔真值/边沿序列及切换后端口状态 | M5 | `src/plugins/logic_node.cpp` | 未实施/未验收 |
| `logic.not` | 非门 | 逻辑运算；布尔真值/边沿序列及切换后端口状态 | M5 | `src/plugins/logic_node.cpp` | 未实施/未验收 |
| `logic.nand` | 与非门 | 逻辑运算；布尔真值/边沿序列及切换后端口状态 | M5 | `src/plugins/logic_node.cpp` | 未实施/未验收 |
| `logic.nor` | 或非门 | 逻辑运算；布尔真值/边沿序列及切换后端口状态 | M5 | `src/plugins/logic_node.cpp` | 未实施/未验收 |
| `logic.xor` | 异或门 | 逻辑运算；布尔真值/边沿序列及切换后端口状态 | M5 | `src/plugins/logic_node.cpp` | 未实施/未验收 |
| `logic.xnor` | 同或门 | 逻辑运算；布尔真值/边沿序列及切换后端口状态 | M5 | `src/plugins/logic_node.cpp` | 未实施/未验收 |
| `logic.edge` | 边沿触发 | 触发器；布尔真值/边沿序列及切换后端口状态 | M5 | `src/plugins/logic_node.cpp` | 未实施/未验收 |
| `data.playback` | 信号回放 | 分别归录制/回放节点；类型、时间、分段、多流与冲突 | M6 | `src/plugins/recording_node.cpp` | 未实施/未验收 |
| `data.recorder` | 字节记录 | 字节记录；二进制/HEX 内容和文件关闭，不与信号录制混淆 | M5 | `src/plugins/recording_node.cpp` | 未实施/未验收 |

## 3. 旧公开命令的行为映射

以下按行为归组覆盖本次旧 schema 中的全部 135 条命令，源为 `archive:/docs/COMMAND_SCHEMA.json`。实现追踪入口为 `archive:/src/app/command_registry.cpp`、`controller_commands.cpp`、`editor_commands.cpp`、`model_commands.cpp`、`ui_commands.cpp` 和 `src/services/command_service.cpp`，具体执行路径仍需逐项核对。

新接口统一定位到会话/实例/公开接口。命令合并、改名或撤回旧机制时保留其有用用户行为；不得把旧中心命令表原封不动搬进新架构。每组状态均为未实施/未验收。

| 旧入口组 | 全部对应命令 | 新职责及必须保留/替代的行为 | 主要大步 |
|---|---|---|---|
| app | `app.connection`、`app.quit`、`app.restart`、`app.state` | 应用配套/接入；状态、就绪、退出、重启及连接信息；任务和未保存内容收尾 | M1/M3 |
| 批量与发现 | `batch`、`registry`、`schema` | 实际装配接口发现与明确操作；按真实语义处理部分失败，不将任意批量冒充事务 | M1/M3 |
| 截图 | `capture` | 视图/窗口截图；查询任务终态、真实尺寸和产物；隐藏/最小化按支持范围实测 | M4 |
| 剪贴板 | `clipboard.read` | 系统交互；明确读取与编辑上下文 | M4 |
| 容器 | `container.layout` | 容器/布局编辑；配置、嵌套、影响和撤销 | M4 |
| 控件交互 | `controls.click`、`controls.commit`、`controls.focus`、`controls.get`、`controls.key`、`controls.list`、`controls.pointer`、`controls.set` | 视图公开状态/动作与结构化交互；焦点、目标和忙时行为，不模拟鼠标接入业务 | M4/M5 |
| 节点编辑 | `editor.add`、`editor.configure`、`editor.copy`、`editor.paste`、`editor.place`、`editor.plan`、`editor.remove`、`editor.reorder`、`editor.replace`、`editor.state`、`editor.withdraw` | 节点编辑/工程提交；预览、放置、复制粘贴、替换撤下、排序及状态 | M3/M4 |
| 图编辑与活动 | `graph.activity`、`graph.connection.options`、`graph.fit`、`graph.node.add`、`graph.node.move`、`graph.node.patch`、`graph.node.remove`、`graph.node.rotate`、`graph.overlaps`、`graph.select`、`graph.wire.add`、`graph.wire.handles`、`graph.wire.patch`、`graph.wire.remove` | 画布编辑与观测；节点、连线、路径点、旋转、重叠、选择和真实活动 | M3/M4 |
| 撤销 | `history.redo`、`history.undo` | 设计历史；修订、原子提交、预览内历史与工程历史分开 | M3/M4 |
| 通信端口 | `io.ports` | 通信能力枚举实际端口，不放入永久全局内核 | M5 |
| 任务 | `job.cancel`、`job.get`、`job.list` | 任务所属能力提供查询/取消/终态，面板只观察和操作 | M1/M5 |
| 节点库 | `library.configure`、`library.favorite`、`library.preview`、`library.search`、`library.state` | 统一节点库搜索、预览、分类/收藏与配置；浏览不启动节点 | M2/M3 |
| 日志 | `log.clear`、`log.configure`、`log.export`、`log.query` | 观测日志；过滤、清理、配置和导出，稳定错误结构与译文分开 | M5 |
| 节点操作 | `node.action`、`node.configure`、`node.configure.plan`、`node.state` | 实例公开配置、状态和动作；参数生效与变更影响由所属能力负责 | M1/M3/M5 |
| 运行参数 | `params.get`、`params.reset`、`params.set` | 配置存储/节点参数；读取、保存、恢复默认，区分设计与实际生效 | M1/M3 |
| 旧包管理 | `plugin.directory`、`plugin.enable`、`plugin.export`、`plugin.export.dialog`、`plugin.extract_source`、`plugin.import`、`plugin.remove`、`plugin.settings`、`plugin.state`、`plugin.tree` | 迁为节点文件管理及库操作；位置、导入导出、版本和源码，不保留 .crpl 身份 | M2/M3 |
| 偏好与设置 | `preferences.set`、`settings.open` | 配置与设置视图；选择的实际位置、记忆和恢复默认 | M3/M4 |
| 工程 | `project.export`、`project.export_original`、`project.get`、`project.info`、`project.new`、`project.open`、`project.options`、`project.rename`、`project.save`、`project.validate` | 工程会话、保存、校验、导出与完整工程提取；不隐式启动或回退原件 | M2/M7 |
| 属性 | `property.commit`、`property.discard`、`property.drafts`、`property.get`、`property.stage`、`property.text` | 节点自带属性工具；失焦/Enter、多选、复杂预览与外部多行编辑；旧统一表单/草稿机制按新规则替代 | M3/M4 |
| 发布 | `publish.cancel`、`publish.log`、`publish.start`、`publish.state` | 发布节点的任务、配置、取消、日志和真实最终产物 | M7 |
| Python 环境 | `python.probe` | Python 执行支持检查；依赖诊断与交付闭包，不自动安装 | M1/M7 |
| 资源 | `resources.get`、`resources.set` | 所属能力的额度及配置；可解释的限制和失败，不建设通用权限系统 | M2/M5 |
| 运行 | `runtime.debug`、`runtime.emit`、`runtime.pause`、`runtime.reset`、`runtime.start`、`runtime.state`、`runtime.step`、`runtime.stop` | 明确绑定的运行控制/调试；查询、运行、暂停、结束、重置、单步和输入；取消旧 debug 总开关，按需观测 | M1/M5 |
| 旧跨页同步 | `sync.apply`、`sync.plan` | 保留批量建视图和显式影响预览；撤回快照双向复制及缺视图自动删节点 | M4 |
| 界面设计 | `ui.container.add`、`ui.item.move`、`ui.item.patch`、`ui.item.remove`、`ui.item.schema`、`ui.update`、`ui.view.add`、`ui.view.appearance`、`ui.view.bind` | 同一节点的独立视图及绑定、外观、布局；编辑修改经工程提交 | M3/M4 |
| 更新 | `update.check`、`update.install`、`update.state` | 可选官方更新能力；检查、暂存、校验、切换和失败恢复 | M8 |
| 视图接口 | `view.actions`、`view.call`、`view.read` | 视图公开动作、读取和调用；自定义视图可发现，状态与业务实例一致 | M3/M4 |
| 窗口 | `window.close`、`window.configure`、`window.pin` | 容器/窗口操作；关闭、配置、置顶与正确工程归属 | M4 |
| 工作区 | `workspace.close`、`workspace.configure`、`workspace.detach`、`workspace.list`、`workspace.move`、`workspace.open`、`workspace.remove` | 跨窗口页面/工作区；打开、配置、移动、分离、关闭及移除，不隐式停业务 | M4 |

## 4. 超出节点/命令索引的用户工作流与新能力

这里追踪依赖多个入口才能完成的行为以及新设计要求。初始阶段号是计划建议，不表示该行已实现。

| 编号 | 能力与验收场景 | 来源/依据 | 新归属与主要大步 | 当前状态 |
|---|---|---|---|---|
| F01 | 生成→跨语言处理→文件输出；消息、服务、动作及真实取消 | D-031、D-074；本轮确认 | 执行支持、类型生成与示例；M1 | 未实施/未验收 |
| F02 | 第三方新增/替换实现不修改入口或其他节点内部代码 | D-031、D-127～130 | 公开接入、SDK、装配；M1/M3 | 未实施/未验收 |
| F03 | .crnd/.crnp、7z、可选上锁/去锁、错误与取消保护 | D-118～121 | 归档及文件能力；M1 最小/M2 完整 | 未实施/未验收 |
| F04 | 引用/完整收纳、去重、多版本、递归及内容冲突 | D-082、D-096、D-146～147 | 工程/节点文件管理；M2 | 未实施/未验收 |
| F05 | 工程内实际副本批量导出、分类、目录与覆盖 | archive:/docs/USER_GUIDE.md；controller_commands.cpp；D-023～024 | 节点管理/工程导出；M2 | 未实施/未验收 |
| F06 | 文件只读占用、另存、设计恢复、未知内容保真及源更新 | D-025～028、D-078 | 工程会话/文件；M2 | 未实施/未验收 |
| F07 | 所有可控默认位置可选且实际位置可查询 | D-029 | 配置与各所属能力；M2 起逐步闭合 | 未实施/未验收 |
| F08 | GUI/CLI/API 同语义、实际装配发现、随机令牌可选接入 | archive:/docs/AI_ACCESS.md；D-039、D-123、D-138 | 接入与能力节点；M1/M3/M4/M7 | 未实施/未验收 |
| F09 | 多工程独立运行，编辑器自身会话与业务工程隔离 | D-016～018、D-129 | 工程/装配；M1/M3/M4 | 未实施/未验收 |
| F10 | 节点完整自定义属性与画布外观，多选、外部多行编辑 | D-100～104、D-134～138 | 所属节点视图/编辑工具；M3/M4 | 未实施/未验收 |
| F11 | 组合轮廓、原地封装、内外接口、展开与拆开 | D-131～133 | .crnp 编辑与视图；M4 | 未实施/未验收 |
| F12 | 预览、应用/完成/取消、工具内撤销、保存及切工程 | D-134～137 | 编辑工具/工程提交；M4 | 未实施/未验收 |
| F13 | 多页导航、跨窗口停靠、浮动分组、标签重组与多工程混排 | D-066、D-097～099 | 容器与用户工作区；M4 | 未实施/未验收 |
| F14 | 视图独立配置、复制视图、切页状态、关闭视图不停业务 | D-064～070、D-099 | 节点视图/容器；M3/M4 | 未实施/未验收 |
| F15 | 动态运算/端口与通道变更、运行期编辑、失败及撤销 | D-092～096、D-105、D-109、D-112 | 所属编辑及运行能力；M4/M5/M6 | 未实施/未验收 |
| F16 | 启动失败收尾、暂停确认、异步单步、重试与崩溃处理 | D-083～095 按 D-127、D-140 收窄 | 官方运行/调试及工程流程；M1/M5 | 未实施/未验收 |
| F17 | 批次、队列、跳批、独立来源同步、有效期和时间跳变 | D-141～144 | 相关计算/时间能力；M1/M5/M6 | 未实施/未验收 |
| F18 | 外部程序、交互终端、取消超时、任务关闭和本次提权 | D-045～050 | 进程执行及发起业务；M5/M8 | 未实施/未验收 |
| F19 | 曲线多通道、历史、游标统计、FFT、CSV/PNG/SVG | archive:/src/qml/PlotView.qml；D-111 | 曲线节点及视图；M6 | 未实施/未验收 |
| F20 | 位置/姿态合并、模型资源、旋转约定、失效及多视图 | archive:/src/qml/AttitudeView.qml、SixDofView.qml；D-111、D-145 | 位姿节点及视图；M6 | 未实施/未验收 |
| F21 | rosbag2＋MCAP、多类型多流、通道映射、分段变速及跳转 | D-108～110、D-114、D-143～144 | 录制/回放节点；M6 | 未实施/未验收 |
| F22 | 实时负载下跨窗口、3D、工作区恢复与资源回收 | archive:/src/quick/renderers.cpp；D-126、D-145 | 视图/执行支持；M4/M6 | 未实施/未验收 |
| F23 | 普通应用及 CoRig 同一路径发布 EXE、必要依赖及离线运行 | archive:/docs/PUBLISHING.md、src/core/publish.cpp；D-148～149 | 发布及依赖提供者；M7 | 未实施/未验收 |
| F24 | EXE 不启动即可导出完整 .crnp，官方源码独立重建 | archive:/src/core/source_archive.cpp；D-056、D-117、D-150 | 源码工程导出/构建材料；M2/M7 | 未实施/未验收 |
| F25 | 安装器可删除、独立更新卸载、系统登记与提权 | archive:/installer/application/installscript.qs、src/app/uninstall.cpp；D-151 | 可选官方维护能力；M8 | 未实施/未验收 |
| F26 | 清单更新失败恢复、数据保留、默认卸载与明确完全删除 | D-124～125 按 D-151 归属官方能力 | 更新/卸载节点与工程；M8 | 未实施/未验收 |
| F27 | 文档生成/渲染、用户区与孤立区保护、包内图片 | D-057～061、D-115 | 说明及渲染能力；各步随包交付/M9 总验收 | 未实施/未验收 |
| F28 | 官方中英文、第三方默认语言回退、稳定机器错误 | archive:/src/i18n、src/plugins/i18n；D-040、D-062 | 所属节点和本地化能力；各步/M9 | 未实施/未验收 |
| F29 | 应用启动就绪退出、对话框、系统交互、配置与恢复默认 | D-129、D-139 | 软件配套节点；M3 起/M7 总验收 | 未实施/未验收 |
| F30 | 官方工程与可复用模板，开箱可用且可替换能力 | D-129、D-153；archive:/examples、sdk/examples | 官方节点/工程/SDK；各步/M9 | 未实施/未验收 |

## 5. 明确替代与不自动扩展的边界

- .crpl、.crpj、独立插件库和旧格式迁移路径不延续，保留其实际可用能力而非文件身份。
- 不照搬旧全局 Controller、固定命令表、全部节点统一生命周期、权限系统和视图快照双向同步。
- 旧调试模式总开关与普通断点机制按新运行/观测设计替代，不以删除旧开关为由删除单步与诊断。
- 不加入多 AI 协作、自编辑正在运行的 CoRig、通用自动表单、密码记忆或未经确认的专用“从模板新建”入口。
- 多机自动部署、通用执行现场恢复和自动反编译不在首版目标内；实际网络、SSH、外部 ROS 接入能力仍需保留。
- 节点完整用途、官方默认实现和第三方准入分别处理，不将官方行为变成全节点强制规则。

## 6. 后续盘点入口

80 个节点和 135 条命令的索引已经覆盖，但仅凭索引不构成完整行为审计。DSH 在进入每个大步时，要展开对应旧声明的参数、端口、资源、动作，以及实现调用链、异常分支和现有测试，并把遗漏子行为补回对应行或工作流。

特别核对动态接口/多来源、同步与复制、资源与源码完整性、任务取消与恢复、截图和窗口承载、发布配置、路径与个人数据。这些跨入口行为不能靠节点名或命令计数推定完成。

状态推进以实际证据为准：未实施 → 实施中 → 自测通过/待用户验收 → 用户已验收。部分通过必须明确写子范围和遗留项。证据保存到实际运行产生的 verification 记录，不填虚构链接。
<!-- USER END feature-map-current -->
