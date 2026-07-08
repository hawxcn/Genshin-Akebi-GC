# P1 · A 子线详解 · 绑定与生命周期

> 关联：[落地计划-P0-P2](./落地计划-P0-P2-Unity侧重构.md) §4.1 的逐文件展开。这是整条重构线里技术最敏感的一步——它触碰启动时序，时序错了偏移解析会失败、游戏会崩。故先把接口签名与 `Run()` 改造前后钉死。
>
> 铁律：**A 子线是纯搬运，启动时序逐行等价**。不优化、不合并、不"顺手"改 Sleep 时长。

---

## 一、A 子线的范围

**动**：把 `Run()`（`cheat-library/src/user/main.cpp`）里"等待运行时就绪 + 偏移初始化"这段，收敛到 `unity-il2cpp` 适配器背后的 `ILifecycle` + `IRuntimeBinding`。

**不动**（明确划走）：
- `init_il2cpp()` 本体、`il2cpp-init.cpp`、X-Macro、校验和、`ILPatternScanner` —— 适配器只当**调用方**，不重写偏移解析。
- `DebuggerBypassPre/Post` —— 是 protection 关注点，P2 才下沉；A 子线里**继续留在 `Run()`**。
- 心跳（B 子线）、光标（C 子线）—— 本子线不碰。
- `IEngineAdapter` 门面 + `CreateAdapter` 工厂 —— P2 引入 bootstrap 时才建；A 子线里 `Run()` **直接 new 出 `UnityLifecycle`**，保持自洽、免依赖工厂。

---

## 二、涉及的真实签名（已核对源码）

| 符号 | 声明位置 | 签名 |
|------|---------|------|
| `Run` | `user/main.h:8` | `void Run(HMODULE* hModule);` |
| `init_il2cpp` | `framework/il2cpp-init.h:8` | `void init_il2cpp();` |
| `DebuggerBypassPre/Post` | `user/cheat/debugger.h:3-4` | `void DebuggerBypassPre(); void DebuggerBypassPost();` |
| `il2cppi_get_base_address` | `framework/helpers.h:270` | `uintptr_t il2cppi_get_base_address();` |
| `il2cppi_get_unity_address` | `framework/helpers.h:273` | `uintptr_t il2cppi_get_unity_address();` |
| `il2cppi_new_console` | `framework/helpers.h:276` | `void il2cppi_new_console();` |

> 注：`IsStaticCheckSumValid()` 未在头文件导出，是 `il2cpp-init.cpp` 的文件内实现，被 `init_il2cpp()` 内部调用。A 子线不触碰它，`InitBinding()` 只调 `init_il2cpp()` 这个已导出入口。

---

## 三、接口签名（A 子线交付的两个接口）

放置于 `cheat-base/src/cheat-base/runtime/`，纯抽象、引擎无关，**不得 include 任何 `app::`/`il2cpp` 头**。

### 3.1 `IRuntimeBinding.h`

```cpp
#pragma once
#include <cstdint>
#include <string_view>

namespace runtime
{
    // 符号解析与模块基址的引擎无关契约。
    // Unity(IL2CPP) 主要靠编译期静态偏移，故本期实现最小化；
    // 该接口的完整价值在 UE5 适配器（P3）与主动调用（P4）时兑现。
    class IRuntimeBinding
    {
    public:
        virtual ~IRuntimeBinding() = default;

        // 运行时元数据/对象表是否已就绪。
        // Unity: InitBinding() 成功后为 true（或 GetModuleHandle("UserAssembly.dll")!=null）。
        virtual bool IsReady() const = 0;

        // 取指定模块基址。module 例:"UserAssembly.dll" / "UnityPlayer.dll"。
        // 未知模块返回 0。替代 core 里 Patch.h 对 il2cppi_get_base_address 的直接依赖(P2 收口)。
        virtual uintptr_t ModuleBase(std::string_view module) const = 0;

        // 按名解析原生函数地址。
        // Unity: 本期返回 nullptr —— 偏移在编译期静态注入(X-Macro)，无需运行时按名解析。
        //        预留此签名以便 UE5(P3) / 带扫描器构建按需实现。
        virtual void* ResolveFunction(std::string_view symbol) = 0;

        // —— 主动调用能力：UE5(P4) 用；Unity 全部空实现 ——
        virtual bool  SupportsProcessEvent() const { return false; }
        virtual void  ProcessEvent(void* uobject, void* ufunction, void* params) { }
        virtual void* FindFunction(void* uobject, std::string_view funcName) { return nullptr; }
    };
}
```

### 3.2 `ILifecycle.h`

```cpp
#pragma once

namespace runtime
{
    class IRuntimeBinding;

    // "把运行时弄到可初始化、并完成绑定" 的引擎无关契约。
    // 精修说明：设计文档 §4.2 曾把 Heartbeat()/Cursor() 挂在此接口上；
    //   现将二者移至 IEngineAdapter(P2 引入)，令 ILifecycle 单一职责=让运行时就绪，
    //   从而 A 子线不依赖尚未实现的 B/C。接口在 P5 前皆为草案，此调整合法。
    class ILifecycle
    {
    public:
        virtual ~ILifecycle() = default;

        // 阻塞直到运行时可初始化。
        // Unity: 轮询等待 UserAssembly.dll 加载 + 固定 Sleep 等 IL2CPP 元数据初始化。
        // (原 Run() 第 38-50 行的等待逻辑逐行搬入此处)
        virtual void WaitForRuntime() = 0;

        // 解析全部符号/偏移。Unity: 调用已导出的 init_il2cpp()。
        // (原 Run() 第 54 行)
        virtual void InitBinding() = 0;

        // 暴露绑定实例，供上层(Patch/Hook)取基址等。
        virtual IRuntimeBinding& Binding() = 0;
    };
}
```

> `WaitForRuntime` 与 `InitBinding` 拆成两个方法（而非合并成一个 `Init()`），是为了让调用方能在两者**之间**插入 `DebuggerBypassPost()`——精确复刻原时序（见 §五）。

---

## 四、适配器实现（放 `cheat-library/src/framework/adapters/unity-il2cpp/`）

### 4.1 `UnityBinding.{h,cpp}`

```cpp
// UnityBinding.h
#pragma once
#include <cheat-base/runtime/IRuntimeBinding.h>

namespace runtime::unity
{
    class UnityBinding : public IRuntimeBinding
    {
    public:
        bool      IsReady() const override;
        uintptr_t ModuleBase(std::string_view module) const override;
        void*     ResolveFunction(std::string_view symbol) override;
        // ProcessEvent 系列用基类默认空实现(Unity 不支持)。
    private:
        bool m_ready = false;
        friend class UnityLifecycle;   // 由 lifecycle 在 InitBinding 后置位 m_ready
    };
}
```

实现要点（`.cpp`）：
- `IsReady()` → 返回 `m_ready`。
- `ModuleBase(module)`：
  - `"UserAssembly.dll"` / 空 → `il2cppi_get_base_address()`
  - `"UnityPlayer.dll"` → `il2cppi_get_unity_address()`
  - 其它 → `(uintptr_t)GetModuleHandleA(module.data())`（或返回 0）
- `ResolveFunction(symbol)` → 本期 `return nullptr;` + 一行 `LOG_DEBUG` 注明"Unity 用静态偏移，未走按名解析"。

### 4.2 `UnityLifecycle.{h,cpp}`

```cpp
// UnityLifecycle.h
#pragma once
#include <cheat-base/runtime/ILifecycle.h>
#include "UnityBinding.h"

namespace runtime::unity
{
    class UnityLifecycle : public ILifecycle
    {
    public:
        void WaitForRuntime() override;   // 搬入原 Run() 38-50 行
        void InitBinding() override;      // 调 init_il2cpp(); 置 m_binding.m_ready=true
        IRuntimeBinding& Binding() override { return m_binding; }
    private:
        UnityBinding m_binding;
    };
}
```

实现要点（`.cpp`，include `il2cpp-init.h`、`helpers.h`、`<Windows.h>`）：

```cpp
void UnityLifecycle::WaitForRuntime()
{
    while (GetModuleHandle("UserAssembly.dll") == nullptr)
    {
        LOG_DEBUG("UserAssembly.dll isn't initialized, waiting for 2 sec.");
        Sleep(2000);
    }
#ifdef _DEBUG
    LOG_DEBUG("Waiting 10sec for loading game library.");
    Sleep(15000);
#else
    LOG_DEBUG("Waiting 15sec for game initialize.");
    Sleep(15000);
#endif
}

void UnityLifecycle::InitBinding()
{
    init_il2cpp();
    m_binding.m_ready = true;
}
```

> ⚠️ **逐行照搬**原 `Run()` 的等待逻辑，包括那个 `_DEBUG` 分支里日志写 10 秒、实际 `Sleep(15000)` 的既有小不一致——A 子线**不修**它（修了就是行为变更，脱离纯搬运）。如要修，另起独立 commit 并单独验证。

---

## 五、`Run()` 改造前后对照

### 改造前（现状，`main.cpp:14-59`，节选核心段）

```cpp
void Run(HMODULE* phModule)
{
    ResourceLoader::SetModuleHandle(*phModule);
    util::SetCurrentPath(util::GetModulePath(*phModule));
    config::Initialize((util::GetCurrentPath() / "cfg.json").string());

    auto& settings = cheat::feature::Settings::GetInstance();
    if (settings.f_FileLogging) { /* ... 文件日志 ... */ }
    if (settings.f_ConsoleLogging) { /* ... */ il2cppi_new_console(); }

    DebuggerBypassPre();                                    // ①

    while (GetModuleHandle("UserAssembly.dll") == nullptr)  // ②
    {
        LOG_DEBUG("UserAssembly.dll isn't initialized, waiting for 2 sec.");
        Sleep(2000);
    }
#ifdef _DEBUG
    LOG_DEBUG("Waiting 10sec for loading game library.");
    Sleep(15000);                                           // ③
#else
    LOG_DEBUG("Waiting 15sec for game initialize.");
    Sleep(15000);
#endif

    DebuggerBypassPost();                                   // ④

    init_il2cpp();                                          // ⑤

    cheat::Init();                                          // ⑥
    LOG_DEBUG("Config path is at %s", ...);
}
```

### 改造后（A 子线目标）

```cpp
#include <runtime/unity-il2cpp/UnityLifecycle.h>   // 新增

void Run(HMODULE* phModule)
{
    ResourceLoader::SetModuleHandle(*phModule);
    util::SetCurrentPath(util::GetModulePath(*phModule));
    config::Initialize((util::GetCurrentPath() / "cfg.json").string());

    auto& settings = cheat::feature::Settings::GetInstance();
    if (settings.f_FileLogging) { /* ... 不变 ... */ }
    if (settings.f_ConsoleLogging) { /* ... 不变 ... */ il2cppi_new_console(); }

    runtime::unity::UnityLifecycle lifecycle;               // 新增：直接构造(工厂待 P2)

    DebuggerBypassPre();                                    // ① 原位保留(P2 才下沉)
    lifecycle.WaitForRuntime();                             // ②③ 搬入 lifecycle
    DebuggerBypassPost();                                   // ④ 原位保留
    lifecycle.InitBinding();                                // ⑤ = init_il2cpp()

    cheat::Init();                                          // ⑥ 不变(心跳/光标属 B/C)
    LOG_DEBUG("Config path is at %s", ...);
}
```

**时序等价性校验表**（改造前后逐点对齐）：

| 点 | 改造前 | 改造后 | 等价? |
|----|--------|--------|-------|
| ① Bypass Pre | `DebuggerBypassPre()` | 原位 | ✅ |
| ② 等 UserAssembly | while 轮询 | `WaitForRuntime()` 内 | ✅ 逐行搬 |
| ③ 固定 Sleep | `Sleep(15000)` | `WaitForRuntime()` 内 | ✅ 逐行搬 |
| ④ Bypass Post | `DebuggerBypassPost()` | 原位 | ✅ |
| ⑤ 偏移初始化 | `init_il2cpp()` | `InitBinding()` 内调同一函数 | ✅ |
| ⑥ 功能装配 | `cheat::Init()` | 不变 | ✅ |

> 关键：`DebuggerBypassPre/Post` 仍在 `Run()` 里、仍夹在"等待"两侧，`InitBinding()` 仍在 `Post` 之后——与原时序**逐点一致**。`lifecycle` 是栈对象，生命周期覆盖整个 `Run()`，其持有的 `UnityBinding` 后续（P2）可交给 bootstrap 长期持有。

---

## 六、生命周期与所有权（P1→P2 过渡）

- **P1**：`lifecycle` 是 `Run()` 的栈局部。P1 阶段没有别处需要 `Binding()`（`Patch.h` 收口是 P2 才做），所以栈生命周期够用。
- **P2**：bootstrap 引入后，`UnityLifecycle`（及其 `UnityBinding`）改由 bootstrap 持有为长生命周期对象，`Patch.h` 经 `IRuntimeBinding::ModuleBase()` 取基址时才有稳定实例可用。A 子线**不需要**提前处理这一点——留 P2。

---

## 七、A 子线提交拆分

| commit | 内容 | 验证 |
|--------|------|------|
| A-1 | 新增 `UnityBinding.{h,cpp}`、`UnityLifecycle.{h,cpp}`；加入 `cheat-library.vcxproj`。**暂不接线** | 编译通过（新类无人调用） |
| A-2 | 改造 `Run()`：构造 `lifecycle`，用 `WaitForRuntime()`+`InitBinding()` 替换原②③⑤ | 编译 + 注入 Genshin，启动日志顺序/时长与旧版一致，偏移解析成功、功能可用 |

> 拆两个 commit：A-1 只加代码不改行为（绝对安全）；A-2 才切换调用点（唯一有回归风险处），一旦冒烟失败，`revert` A-2 即回到可用态。

---

## 八、A 子线专属验收清单

注入 Genshin 后逐项确认（对照改造前）：
1. **启动日志**：`"UserAssembly.dll isn't initialized..."`（若出现）→ 等待日志 → 之后功能就绪，顺序与时长同旧版。
2. **偏移解析成功**：无"offset/scan 失败"告警；静态校验和路径正常。
3. **随便开一个 Hook 类功能（如 GodMode）**：生效 → 证明 `init_il2cpp()` 后 `app::` 指针正确填址。
4. **随便开一个每帧功能（如 AutoRun）**：生效 → 证明 `cheat::Init()`（⑥）未受影响（心跳仍是旧路径，B 子线才改）。
5. **`Release_WithScanner` 配置**：单独编译一次并注入，确认带扫描器路径也未被打断（该配置定义 `_PATTERN_SCANNER`，走 `init_scanned_offsets`；`InitBinding` 调的是同一个 `init_il2cpp()`，应透明）。

---

## 九、A 子线的待定微决策（动工前顺手拍板）

1. **接口头的 include 前缀**：新目录物理路径 `cheat-base/src/cheat-base/runtime/`，则引用写 `#include <cheat-base/runtime/ILifecycle.h>`（沿用现有 `<cheat-base/...>` 惯例）。适配器放 `cheat-library/src/framework/adapters/unity-il2cpp/`，`cheat-library.vcxproj` 已含 `$(ProjectDir)src/framework` 于 include 路径，故引用可写 `<adapters/unity-il2cpp/UnityLifecycle.h>` 或加相对路径——二选一，全项目统一即可。
2. **`ResolveFunction` 本期形态**：确认返回 `nullptr`（Unity 静态偏移不需要它），还是顺手接一层 `ILPatternScanner::Search`？建议**先 `nullptr`**，等 P4/UE5 真正需要按名解析时再充实，避免过早实现。
3. **`_DEBUG` 分支日志/Sleep 不一致**：确认 A 子线**照搬不修**（保持纯搬运）。若想修，另开 commit。

---

> 一句话：A 子线把 `Run()` 中"等运行时 + init_il2cpp"两段搬进 `UnityLifecycle`，`DebuggerBypass` 原位不动，时序逐点等价；`IRuntimeBinding` 本期最小化（`ModuleBase` 有用、`ResolveFunction` 先空）。分 A-1（加代码）/A-2（切调用）两个 commit，回归风险集中在 A-2 且可单步回滚。
