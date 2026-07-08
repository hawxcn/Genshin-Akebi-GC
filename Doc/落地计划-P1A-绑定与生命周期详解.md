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
        // Unity: 委托 ILPatternScanner 按签名名搜索(见 §四 4.1)。
        //        限制——只能解析 signatures.json 里有签名的符号；纯静态偏移(无签名)
        //        的 app:: 函数按名查不到，故为"尽力而为"。
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
        bool               m_ready = false;
        bool               m_sigParsed = false;   // ResolveFunction 懒加载标志
        ILPatternScanner   m_scanner;             // 决策②：按名解析委托它
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
- `ResolveFunction(symbol)` → **委托 `ILPatternScanner`**（决策②）。实现：
  - 持有一个 `ILPatternScanner` 成员，**首次调用时懒加载**：`ResourceLoader::Load("Signatures", RT_RCDATA)` → `ParseSignatureFile(...)`（解析一次，置标志位，后续复用）。
  - 返回 `scanner.Search("UserAssembly.dll", symbol)`；未命中返回 `nullptr`。
  - **限制（务必在代码注释里写明）**：只解析 `signatures.json` 里有签名登记的符号；纯静态偏移的 `app::` 函数按名查不到。故此方法是"尽力而为"，Unity 主路径仍靠编译期静态偏移，本方法供确需按名解析的少数场景。
  - **性能**：每次 `Search` 会在模块内存里扫签名，开销不小；因 `ResolveFunction` 预期低频调用可接受，若将来高频再加结果缓存（`name→addr` map）。
  - **链接性**：`ILPatternScanner` 在任何构建都参与编译（`init_scanned_offsets` 恒被编译，仅调用点受 `_PATTERN_SCANNER` 约束），故此实现**无需 `#ifdef`**，静态构建下同样可用。

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
    // 决策③(顺手修正)：原 _DEBUG/_else 两分支 Sleep 时长本就相同(均 15000)，
    // 仅 _DEBUG 日志文案误写"10sec"。保守修法=合并冗余 #ifdef + 统一文案，
    // Sleep 时长不变(行为保持)。已在独立 commit A-0 于原 Run() 就地修好，此处搬的是修正后代码。
    LOG_DEBUG("Waiting 15 sec for game initialization.");
    Sleep(15000);
}

void UnityLifecycle::InitBinding()
{
    init_il2cpp();
    m_binding.m_ready = true;
}
```

> ⚠️ 等待逻辑逐行照搬，**唯一例外**是决策③：`_DEBUG` 分支的日志/`#ifdef` 不一致在**独立 commit A-0** 里先就地修好（行为保持，仅文案+去冗余 `#ifdef`），再由 A-1 搬迁修正后的代码。若你本意是"调试构建应等更短的 10 秒"（时序变更），请明确告知——当前默认按行为保持处理。

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
| A-0 | 决策③：在**现有** `Run()` 就地修正 `_DEBUG` 分支日志/`#ifdef` 不一致（合并两分支、统一文案，`Sleep(15000)` 不变）。独立于重构 | 编译 + 注入，启动等待时长与旧版一致（仅调试日志文案变化） |
| A-1 | 新增 `UnityBinding.{h,cpp}`、`UnityLifecycle.{h,cpp}`（含决策②的 scanner 委托）；加入 `cheat-library.vcxproj`。**暂不接线** | 编译通过（新类无人调用） |
| A-2 | 改造 `Run()`：构造 `lifecycle`，用 `WaitForRuntime()`+`InitBinding()` 替换原②③⑤（搬迁 A-0 修正后的等待代码） | 编译 + 注入 Genshin，启动日志顺序/时长与旧版一致，偏移解析成功、功能可用 |

> 拆三个 commit：A-0 先把既有小瑕疵就地修掉（行为保持，与重构解耦）；A-1 只加代码不改行为（绝对安全）；A-2 才切换调用点（唯一有回归风险处），一旦冒烟失败，`revert` A-2 即回到可用态。

---

## 八、A 子线专属验收清单

注入 Genshin 后逐项确认（对照改造前）：
1. **启动日志**：`"UserAssembly.dll isn't initialized..."`（若出现）→ 等待日志 → 之后功能就绪，顺序与时长同旧版。
2. **偏移解析成功**：无"offset/scan 失败"告警；静态校验和路径正常。
3. **随便开一个 Hook 类功能（如 GodMode）**：生效 → 证明 `init_il2cpp()` 后 `app::` 指针正确填址。
4. **随便开一个每帧功能（如 AutoRun）**：生效 → 证明 `cheat::Init()`（⑥）未受影响（心跳仍是旧路径，B 子线才改）。
5. **`Release_WithScanner` 配置**：单独编译一次并注入，确认带扫描器路径也未被打断（该配置定义 `_PATTERN_SCANNER`，走 `init_scanned_offsets`；`InitBinding` 调的是同一个 `init_il2cpp()`，应透明）。

---

## 九、A 子线微决策（已确认）

1. **接口/适配器 include 前缀**（已定）：runtime 接口用 `#include <cheat-base/runtime/ILifecycle.h>`；适配器放 `cheat-library/src/framework/adapters/unity-il2cpp/`，引用写 `#include <adapters/unity-il2cpp/UnityLifecycle.h>`（借 `cheat-library.vcxproj` 已有的 `$(ProjectDir)src/framework` include 路径）。全项目统一此风格。
2. **`ResolveFunction` 本期形态**（已定）：**委托 `ILPatternScanner`**（懒加载签名、按名 `Search`）。限制与性能见 §四 4.1——只解析签名登记过的符号，尽力而为。
3. **`_DEBUG` 分支不一致**（已定）：**顺手修正**，且按"行为保持"口径（统一日志文案 + 合并冗余 `#ifdef`，`Sleep` 时长不变），落在独立 commit **A-0**。唯一遗留确认：若本意是调试构建应等更短的 10 秒（时序变更），需另行告知。
4. **`s_tick`/实例承载 与 所有权**（已定，见 B 子线）：文件内单槽 + 具名 Hook；P1 用函数内 `static`、P2 交 bootstrap。

---

> 一句话：A 子线把 `Run()` 中"等运行时 + init_il2cpp"两段搬进 `UnityLifecycle`，`DebuggerBypass` 原位不动，时序逐点等价；`IRuntimeBinding` 本期最小化（`ModuleBase` 有用、`ResolveFunction` 先空）。分 A-1（加代码）/A-2（切调用）两个 commit，回归风险集中在 A-2 且可单步回滚。
