# 落地计划 · P0–P2（Unity 侧重构，低风险、不引入 UE）

> 关联：[设计-通用化框架改造方案](./设计-通用化框架改造方案.md)（架构定稿）。本文是其中迁移路径 **P0→P1→P2** 的可执行展开。
>
> 范围声明：**只覆盖 Unity/Genshin 侧的内部重构**——把现有代码归位到目标架构、抽出引擎适配接口、内核去耦合。**全程不引入 UE、不写 Vulkan、不做 ProcessEvent、不做功能自动注册**（那些属于 P3–P6）。目标是先在零功能回归的前提下，把"引擎适配层"这条缝干净地切出来，为后续 UE5 适配铺好插槽。

---

## 一、总原则（每一步都必须满足）

1. **每个提交都能编译，且 Genshin 注入后功能不回归**。这是唯一的验收硬指标——本项目无自动化测试，只能靠编译 + 注入 + F1 手动验证。
2. **小步可回滚**：每个步骤是一个独立 commit，出问题 `git revert` 单步即可，不牵连其它。
3. **不改行为，只搬结构**：P0–P2 是纯重构。任何"顺手优化功能"的冲动都推迟——重构与功能变更混在一个 commit 里会让回归定位变噩梦。
4. **依赖方向不倒流**：`games(cheat-library) → runtime 接口 → core(cheat-base)`。新加的接口若引用了任何 `app::`/`il2cpp` 符号，即为错误。

---

## 二、三个降低 churn 的前置决定（重要）

这些决定偏离了设计文档字面表述，但显著降低风险，理由随附：

| 决定 | 说明 | 理由 |
|------|------|------|
| **P0–P2 不重命名 `cheat-base`→`core`** | 物理项目名、`<cheat-base/...>` include 前缀全部保留。"core" 只作为**概念角色**存在 | `cheat-library.vcxproj` 靠 `$(SolutionDir)cheat-base/src/` 引头文件，`cheat-base/` 前缀被写死在成百上千行 `#include` 里。重命名是纯机械 churn + 高回归风险，且零功能收益。若将来确要改名，单独做一个"仅重命名"的机械 commit，不与逻辑重构混合。 |
| **`runtime` 接口头文件放进 `cheat-base`** | 新目录 `cheat-base/src/cheat-base/runtime/`，放纯抽象接口（`IRuntimeBinding` 等） | 这些接口是**引擎无关的纯虚契约**，天然属于内核；放这里可让 P0 不必新建 VS 工程（零构建配置改动）。将来若要独立成 `runtime` 工程，是后续可选增量。 |
| **适配器实现放进 `cheat-library`** | 新目录 `cheat-library/src/framework/adapters/unity-il2cpp/` | 适配器引用 `app::`/`il2cpp`，是引擎相关代码，必须待在游戏层项目里，不能进 `cheat-base`。 |

> 顺带记录一个已知隐患（不在本期修，仅备案）：`cheat-library.vcxproj` 各配置语言标准不一致（Debug=`stdcpp17`、Release=`stdcpp20`、Release_WS=`stdcpp17`）。接口用到较新语法时需按最低标准 C++17 写，避免只在 Release 编过。

---

## 三、P0 · 脚手架（接口占位，零行为变更）

**目标**：把目标架构的"空壳"立起来——接口头文件、目录、命名空间就位，但**不接线**，现有代码一行逻辑都不改。

### 步骤

| 序 | 动作 | 具体文件 |
|----|------|---------|
| 0.1 | 建接口目录与纯抽象头（仅声明，无实现） | `cheat-base/src/cheat-base/runtime/IRuntimeBinding.h`、`ILifecycle.h`、`IHeartbeat.h`、`ICursorController.h`、`IEngineAdapter.h` |
| 0.2 | 接口内容 = 设计文档 §4 的草图，全部纯虚，`namespace runtime` | 不 include 任何 `app::`/`il2cpp` 头；只依赖 `<string_view>`、`<functional>`、`core` 的 `renderer.h`（为 `PreferredBackend`） |
| 0.3 | 把新头加入 `cheat-base.vcxproj` 的 `<ClInclude>` 列表（及 `.filters`） | 头文件项目可见即可，无 `.cpp` |
| 0.4 | 建空适配器目录占位 | `cheat-library/src/framework/adapters/unity-il2cpp/`（先放一个 `README` 或 `.gitkeep`） |

### 验收
- 解决方案三配置（Debug/Release/Release_WS）**全部编译通过**。
- 因为无人引用新接口，Genshin 行为**逐字节不变**（可不注入，仅编译即算过；保险起见注入跑一次 F1）。

**风险**：极低。纯新增文件。**效果**：为 P1 提供落脚点。

---

## 四、P1 · 抽出 Unity 适配器（行为不变，改走接口）

**目标**：把三处引擎耦合——**绑定初始化、每帧心跳、光标控制**——从散落位置收敛到 `unity-il2cpp` 适配器背后，Genshin 行为保持完全一致。这是 P0–P2 的重头，拆成三条独立子线，各自可单独提交、单独验证。

### 4.1 子线 A：绑定与生命周期（`IRuntimeBinding` + `ILifecycle`）

现状：`main.cpp` 的 `Run()`（第 36–56 行）硬编码"`DebuggerBypassPre` → 等 `UserAssembly.dll` → 固定 `Sleep` → `DebuggerBypassPost` → `init_il2cpp()`"。

| 序 | 动作 |
|----|------|
| A1 | 新建 `adapters/unity-il2cpp/UnityLifecycle.{h,cpp}`，实现 `ILifecycle`：`WaitForRuntime()` 内搬入"等 `UserAssembly.dll` + 固定 Sleep"逻辑；`InitBinding()` 内调用现有 `init_il2cpp()` |
| A2 | 新建 `adapters/unity-il2cpp/UnityBinding.{h,cpp}`，实现 `IRuntimeBinding`：`IsReady()`、`ModuleBase()`（包 `il2cppi_get_base_address`/`il2cppi_get_unity_address`）、`ResolveFunction()`（本期可先只包一层，Unity 主要靠静态偏移，实现可最小化）。`SupportsProcessEvent()` 返回 `false` |
| A3 | `Run()` 改为：构造 `UnityLifecycle`，依次调 `WaitForRuntime()`→`InitBinding()`。`DebuggerBypassPre/Post` 暂**留在 `Run()`**（P2 才下沉 protection），只搬"等待+init"这部分 |
| A4 | `init_il2cpp()` 本体、`il2cpp-init.cpp`、X-Macro、校验和、`ILPatternScanner`**原地不动**——适配器只是"调用方"，不重写偏移解析。降低风险 |

**验收**：注入 Genshin，启动时序与之前一致（日志顺序、等待时长），偏移解析成功，功能可用。

### 4.2 子线 B：每帧心跳（`IHeartbeat`）

现状：`cheat.cpp:181` `GameManager_Update_Hook` → `events::GameUpdateEvent()`；`cheat.cpp:200` `InstallEventHooks()` 安装它。

| 序 | 动作 |
|----|------|
| B1 | 新建 `adapters/unity-il2cpp/UnityHeartbeat.{h,cpp}`，实现 `IHeartbeat::Install(tick)`：内部安装 `app::GameManager_Update` 的 Hook，Hook 体里调用传入的 `tick()` |
| B2 | 把 `GameManager_Update_Hook` 的实现移入适配器；`tick` 绑定为"触发 `GameUpdateEvent` + `CheckAccountChanged`"。**注意**：`CheckAccountChanged`、`MoveSync` Hook 是 Genshin 业务，**留在 `cheat.cpp`**，不进适配器——适配器只负责"心跳这件引擎事" |
| B3 | `cheat::Init()`/`InstallEventHooks()` 改为：向适配器要 `IHeartbeat`，调用 `Install([]{ events::GameUpdateEvent(); })`；账号检测作为 `GameUpdateEvent` 的一个订阅者挂上去（现在它本就在心跳里被调，改成订阅者即可，行为等价） |

**验收**：`GameUpdateEvent` 每帧照常触发（自动奔跑/吸怪/传送等每帧功能正常）；切号仍触发 `AccountChangedEvent`（Profile 自动切换正常）。

### 4.3 子线 C：光标控制（`ICursorController`）

现状：`GenshinCM::CursorSetVisibility/GetVisibility`（`GenshinCM.h:61-62`）是 `CheatManagerBase` 的纯虚实现。

| 序 | 动作 |
|----|------|
| C1 | 新建 `adapters/unity-il2cpp/UnityCursor.{h,cpp}`（或 `games/genshin` 下，取决于实现是否用到 Genshin 专有 API），实现 `ICursorController`，把 `GenshinCM` 里两个函数体搬过来 |
| C2 | 本期**先不动** `CheatManagerBase` 的纯虚签名（那是 P2 的事）；C 子线可仅新增 `UnityCursor` 并让 `GenshinCM` 的实现委托给它，作为过渡。这样 C1 与 P2 解耦 |

**验收**：菜单开关时光标显隐/锁定行为与之前一致。

> 子线拆分的意义：A/B/C 相互独立，可分别提交、分别回归。任一子线出问题不影响其它两条。

---

## 五、P2 · 内核去耦合 + bootstrap + manifest

**目标**：让 `cheat-base` 真正引擎无关，并用 `bootstrap` + `manifest` 取代硬编码的 `Run()`。

### 5.1 去除内核 IL2CPP 泄漏

| 序 | 动作 |
|----|------|
| 2.1 | `Patch.h:6-7` 的 `OPatch/OUnpatch` 宏依赖 `il2cppi_get_base_address()`。二选一：**(推荐)** 把这两个宏移出 `cheat-base`、下沉到 `adapters/unity-il2cpp`（它们本就是 Unity"基址+偏移"用法）；或改为经 `IRuntimeBinding::ModuleBase()` 取基址 |
| 2.2 | 全仓 grep 确认 `cheat-base/` 下不再出现 `il2cpp`/`app::` 符号（`Patch.h` 是已知唯一泄漏，修完应归零） |

### 5.2 CheatManagerBase 注入 Cursor

| 序 | 动作 |
|----|------|
| 2.3 | `CheatManagerBase` 增加持有 `ICursorController*`（由构造或 `Init` 注入），删除两个纯虚 `CursorSet/GetVisibility`，改为转发给注入的控制器 |
| 2.4 | `GenshinCM` 不再 override 这两个函数；改为在装配时把 P1-C 的 `UnityCursor` 注入基类。**自此不再强制每游戏子类化管理器来提供光标**（UE 游戏可直接注入自己的实现） |

### 5.3 protection 原语下沉 core

| 序 | 动作 |
|----|------|
| 2.5 | 新建 `cheat-base/src/cheat-base/protection/`，把通用反作弊原语沉淀进来：先迁 `DebuggerBypassPre/Post` 的**引擎无关部分**（现由 Genshin 层的 `debugger.h` 提供）。仅搬通用能力，Genshin 特定检测留原处 |
| 2.6 | 每项能力可被开关控制（读 manifest，见 5.4） |

> 注：若 `DebuggerBypass` 现有实现掺了 Genshin 专有逻辑，本期只抽出确实通用的部分，其余标注 TODO 留待有第二个引擎（UE5）时再判定归属——避免"只对着 Genshin 想当然地通用化"。

### 5.4 bootstrap + manifest 取代 Run()

| 序 | 动作 |
|----|------|
| 2.7 | 新建 `cheat-library/src/framework/bootstrap.{h,cpp}`（或 `host/`）。流程：读 `framework.manifest.json` → 按 `protection` 段启用原语 → `CreateAdapter("unity-il2cpp")` → `lifecycle.WaitForRuntime()`/`InitBinding()` → 选渲染后端 → `cheat::Init()` 装配功能 |
| 2.8 | `Run()` 瘦身为"设置路径/日志 → 调 `bootstrap()`"。`dllmain.cpp` 不动（起线程跑 `Run` 已足够通用） |
| 2.9 | 新增 `framework.manifest.json`（Genshin 版）：`runtime: "unity-il2cpp"`、`render: "auto"`、`process: ["GenshinImpact.exe","YuanShen.exe"]`、`protection` 各项开关。**发布形态既定为每目标独立产物**，故 manifest 只描述"本产物"的单一目标，不做多目标切换逻辑 |
| 2.10 | `CreateAdapter(engineId)` 工厂本期只认 `"unity-il2cpp"`，`default` 分支报错。UE 分支待 P3 |

**验收（P2 整体）**：
- `cheat-base` 全目录无引擎符号（grep 归零）。
- Genshin 注入后：启动时序、偏移解析、所有功能、光标、切号 Profile——**逐项与重构前一致**。
- 删掉/改名 `framework.manifest.json` 时有明确报错，不静默崩溃。

---

## 六、验证清单（每个 commit 收尾都跑）

无自动化测试，固定手动流程：

```
1. msbuild /m /p:Configuration=Release akebi-gc.sln   # 至少 Release 过；阶段收尾三配置都过
2. 将 CLibrary.dll + injector.exe 放同目录，运行 injector.exe
3. 进游戏，F1 呼出菜单
4. 冒烟用例（覆盖被动过的三处耦合）：
   - 心跳类：开 AutoRun / MobVacuum，确认每帧逻辑生效
   - Hook 类：开 GodMode，确认拦截生效
   - 光标：反复开关菜单，光标显隐正常、游戏输入锁正常
   - 配置：改任意开关，等 2s，确认 cfg.json 落盘；重启后保留
   - 切号（若可）：确认 Profile 自动切换
```

> UI/功能类改动**必须实际注入验证**（CLAUDE.md 硬性要求），编译过 ≠ 功能对。

---

## 七、Git 与提交策略

- 建分支 `refactor/runtime-layer`，全程在其上小步提交。
- 提交粒度对应本文步骤：P0 一提交；P1 的 A/B/C 各 1–2 提交；P2 每小节 1 提交。
- 每个提交信息写清"搬了什么、行为应不变"，便于日后二分定位回归。
- **不 force push、不 rebase 已推送历史**；合并前保证分支可编译 + 冒烟通过。

---

## 八、交付物与完成定义（DoD）

**P0–P2 交付物**：
1. `cheat-base/src/cheat-base/runtime/` 五个纯抽象接口头。
2. `cheat-library/.../adapters/unity-il2cpp/` 下 `UnityLifecycle`/`UnityBinding`/`UnityHeartbeat`/`UnityCursor` 四个实现。
3. `cheat-base/src/cheat-base/protection/` 通用反作弊原语。
4. `bootstrap.{h,cpp}` + `framework.manifest.json`（Genshin 版）。
5. `CreateAdapter` 工厂（仅 unity 分支）。

**完成定义**：
- 三配置全部编译通过。
- Genshin 冒烟清单逐项通过，行为与重构前无差异。
- `cheat-base` 目录 grep 引擎符号归零。
- 引擎适配的三处缝（绑定/心跳/光标）已全部走接口——**为 P3 的 UE5 适配器留好了对称插槽**。

---

## 九、明确不在本期范围（避免范围蔓延）

| 项 | 归属阶段 | 原因 |
|----|---------|------|
| UE5 适配器、GObjects/GNames、UWorld::Tick | P3 | 依赖尚未锁定的具体 UE5 游戏 |
| ProcessEvent 主动调用 | P4 | 依赖 P3 |
| runtime 接口冻结/修订 | P5 | 需 UE5 第二实现校准后才冻结（本期接口均为可变草案） |
| 功能自动注册（消除 `AddFeatures` 中央表） | P6 | 独立的模块化增量，与引擎解耦无关 |
| Vulkan 后端 | 推迟 | 目标 DX11/12，非阻塞 |
| `cheat-base`→`core` 物理重命名 | 可选/推迟 | 纯 churn，见 §二 |
| 注入器进程名参数化 | 可并入 P2 或推迟 | 低风险小改，不阻塞主线 |

---

## 十、风险登记（P0–P2 专属）

| 风险 | 概率 | 缓解 |
|------|------|------|
| 搬 `Run()` 时序时打乱等待/Sleep 顺序导致偏移解析失败 | 中 | A 子线严格保持时序；对照日志逐行核对 |
| 心跳改造后 `CheckAccountChanged` 调用时机变化 | 中 | 明确改为 `GameUpdateEvent` 订阅者，语义等价；切号用例验证 |
| protection 下沉时误把 Genshin 专有检测当通用搬走 | 中 | 只搬确认通用的部分，存疑的标 TODO 留待 UE5 时判定 |
| 语言标准不一致导致"只在 Release 编过" | 低 | 接口按 C++17 写；阶段收尾三配置全编 |
| 接口过早固化 | 低（本期不冻结） | 明确 P0–P4 接口皆草案，P5 才冻结 |

---

> 一句话：P0 立空壳，P1 把"绑定/心跳/光标"三处引擎缝改走接口（Genshin 行为不变），P2 让内核彻底干净并用 bootstrap+manifest 取代硬编码启动。做完这三步，UE5 适配器就有了对称的插槽，且全程 Genshin 可用作回归基线。
