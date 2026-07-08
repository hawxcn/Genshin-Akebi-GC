# P1 · B 子线详解 · 每帧心跳

> 关联：[落地计划-P0-P2](./落地计划-P0-P2-Unity侧重构.md) §4.2 的逐文件展开，并**修订**其中 B3 的做法（见 §六）。
>
> 铁律：**行为逐帧等价**。心跳是全项目每帧逻辑的唯一节拍源，任何"谁先跑谁后跑"的漂移都可能引起隐蔽 bug，故本子线的核心是**顺序等价性证明**。

---

## 一、B 子线的范围

**动**：把"每帧心跳"这件引擎事——即 `GameManager.Update` 的 Hook——从 `cheat.cpp` 收敛到 `unity-il2cpp` 适配器背后的 `IHeartbeat`。

**不动 / 明确留在 Genshin 层（`cheat.cpp`）**：
- `CheckAccountChanged()`（`cheat.cpp:164-179`）——Genshin 账号检测，**函数体一行不改**；只改"由谁触发"。
- `LevelSyncCombatPlugin_RequestSceneEntityMoveReq_Hook`（`cheat.cpp:191-196`）+ `MoveSyncEvent`——这是 Genshin 移动同步业务，**不是心跳**，与本子线无关，原样保留（连同 `InstallEventHooks` 里它那行 install）。
- `config::SetupUpdate(&events::GameUpdateEvent)`（`cheat.cpp:73`）——不动。
- 所有 Feature 的 `OnGameUpdate` 订阅——不动。

**一句话边界**：适配器只负责"每帧调用一次你给我的 tick 闭包"；tick 里放什么（含 Genshin 的账号检测）由 `cheat.cpp` 决定。

---

## 二、涉及的真实签名（已核对源码）

| 符号 | 位置 | 签名 / 现状 |
|------|------|------------|
| `GameUpdateEvent` | `cheat/events.h:8` | `extern TEvent<> GameUpdateEvent;`（无参） |
| `AccountChangedEvent` | `cheat/events.h:9` | `extern TEvent<uint32_t> AccountChangedEvent;` |
| `MoveSyncEvent` | `cheat/events.h:10` | `extern TEvent<uint32_t, app::MotionInfo*>`（Genshin 业务，留置） |
| `GameManager_Update_Hook` | `cheat.cpp:181-189` | 现心跳 Hook：`SAFE{ GameUpdateEvent(); CheckAccountChanged(); } CALL_ORIGIN` |
| `CheckAccountChanged` | `cheat.cpp:164-179` | `static void`，内部 `UPDATE_DELAY(2000U)` + `GET_SINGLETON(MoleMole_PlayerModule)` |
| `InstallEventHooks` | `cheat.cpp:198-202` | 装两个 Hook：`GameManager_Update`、`...MoveReq` |
| hook 目标 | — | `app::GameManager_Update`（`app::GameManager*, MethodInfo*`） |

### 现状 Hook 体（逐字，作为等价基准）

```cpp
static void GameManager_Update_Hook(app::GameManager* __this, MethodInfo* method)
{
    SAFE_BEGIN();
    events::GameUpdateEvent();      // ① 触发全部订阅者(config 落盘检查 + 各 Feature 每帧逻辑)
    CheckAccountChanged();          // ② 账号检测(内部 2s 节流)
    SAFE_EEND();

    CALL_ORIGIN(GameManager_Update_Hook, __this, method);   // ③ 放行原逻辑
}
```

**三个不变量**（B 子线必须逐一保住）：
- **不变量 A**：每帧 ①②③ 按此顺序发生。
- **不变量 B**：①② 被同一个 `SAFE_BEGIN/EEND` 包裹；③ 在 SAFE 之外。
- **不变量 C**：② 在 ①**之后**执行（即账号检测发生在所有 `GameUpdateEvent` 订阅者跑完之后）。

---

## 三、接口签名（B 子线交付的接口）

放 `cheat-base/src/cheat-base/runtime/IHeartbeat.h`，引擎无关，**不含任何 `app::`/SEH**。

```cpp
#pragma once
#include <functional>

namespace runtime
{
    // 每帧心跳源的引擎无关契约。
    // 适配器负责：Hook 引擎的逐帧函数，每帧调用一次 tick，并放行原逻辑。
    // Unity: Hook app::GameManager_Update。
    // UE5  : Hook UWorld::Tick / UGameEngine::Tick（P3）。
    class IHeartbeat
    {
    public:
        virtual ~IHeartbeat() = default;

        // 安装心跳。tick 在每帧被调用一次。
        // 语义约定(所有适配器须遵守)：
        //   - tick() 的调用置于引擎逐帧函数内、放行原逻辑之前；
        //   - tick() 由适配器用其平台的异常保护(如 SEH)包裹，
        //     使 tick 内访问游戏内存的失败不至于崩游戏；
        //   - 只安装一次(重复调用行为未定义)。
        virtual void Install(std::function<void()> tick) = 0;
    };
}
```

> 设计取舍：把 SEH 包裹写进"适配器语义约定"而非接口签名——因为 SEH 是平台/引擎相关的保护手段，核心接口不应知道它。Unity 适配器用现有 `SAFE_BEGIN/EEND` 兑现该约定（见 §四）。

---

## 四、适配器实现（`cheat-library/src/framework/adapters/unity-il2cpp/`）

### `UnityHeartbeat.{h,cpp}`

```cpp
// UnityHeartbeat.h
#pragma once
#include <cheat-base/runtime/IHeartbeat.h>

namespace runtime::unity
{
    class UnityHeartbeat : public IHeartbeat
    {
    public:
        void Install(std::function<void()> tick) override;
    };
}
```

实现要点（`.cpp`，include `pch-il2cpp.h`、`helpers.h`、`il2cpp-appdata.h`、`HookManager`）：

```cpp
namespace {
    std::function<void()> s_tick;   // 文件内单例：GameManager.Update 全局唯一，单槽即可

    // 适配器内部 Hook —— 引擎相关，故留在适配器里。签名须与游戏函数完全一致。
    void GameManager_Update_Hook(app::GameManager* __this, MethodInfo* method)
    {
        SAFE_BEGIN();
        if (s_tick) s_tick();       // ①② 全在 tick 闭包内(见 cheat.cpp 改造)
        SAFE_EEND();

        CALL_ORIGIN(GameManager_Update_Hook, __this, method);   // ③
    }
}

void UnityHeartbeat::Install(std::function<void()> tick)
{
    s_tick = std::move(tick);
    HookManager::install(app::GameManager_Update, GameManager_Update_Hook);
}
```

**为何用文件内 `s_tick` 而非成员**：`HookManager::install` 需要一个签名匹配的**具名函数指针**作为 Hook，且用 `__func__` 反查原始函数（`CALL_ORIGIN`）。成员函数指针不满足此形态。`GameManager.Update` 全局唯一，单个文件内槽位是最简且安全的做法。

**不变量核对**：
- 不变量 A（①②③ 顺序）：tick 闭包内是 ①②，`CALL_ORIGIN` 是 ③，顺序保住。✅
- 不变量 B（SAFE 包裹 ①② 且 ③ 在外）：`SAFE_BEGIN/EEND` 包住 `s_tick()`，`CALL_ORIGIN` 在其后。✅
- 不变量 C（② 在 ① 之后）：由 tick 闭包内部保证，见 §五/§六。✅
- **SEH × C++ 对象安全**：Hook 函数体内不声明需析构的 C++ 局部对象，仅"调用" `s_tick()`（与原版仅"调用" `GameUpdateEvent()`/`CheckAccountChanged()` 同构）。lambda 内部的 C++ 对象在其 `operator()` 的独立栈帧里，不落在本函数的 `__try` 帧，故不触犯"SEH 不得与需展开的 C++ 对象同帧"约束。与原实现等价。✅

---

## 五、`cheat.cpp` 改造前后对照

### 改造前（现状，节选）

```cpp
static void GameManager_Update_Hook(...) { SAFE_BEGIN(); events::GameUpdateEvent(); CheckAccountChanged(); SAFE_EEND(); CALL_ORIGIN(...); }
static void LevelSync..._Hook(...)       { events::MoveSyncEvent(...); CALL_ORIGIN(...); }

static void InstallEventHooks()
{
    HookManager::install(app::GameManager_Update, GameManager_Update_Hook);                        // 心跳
    HookManager::install(app::MoleMole_LevelSyncCombatPlugin_RequestSceneEntityMoveReq, LevelSync..._Hook); // 移动同步(留)
}
```

### 改造后（B 子线目标）

```cpp
#include <runtime/unity-il2cpp/UnityHeartbeat.h>   // 新增

// GameManager_Update_Hook 从这里删除 —— 已移入 UnityHeartbeat。
// CheckAccountChanged 原样保留(不改一行)。
// LevelSync..._Hook 原样保留。

static void InstallEventHooks()
{
    static runtime::unity::UnityHeartbeat heartbeat;   // 函数内静态：长生命周期(P2 交 bootstrap 持有)
    heartbeat.Install([]{                              // tick 闭包 = 原 Hook 内 SAFE 段的两次调用
        events::GameUpdateEvent();     // ①
        CheckAccountChanged();         // ②  ← 顺序与原版逐字一致
    });

    // 移动同步 Hook 原样保留(Genshin 业务，非心跳)
    HookManager::install(app::MoleMole_LevelSyncCombatPlugin_RequestSceneEntityMoveReq,
                         LevelSyncCombatPlugin_RequestSceneEntityMoveReq_Hook);
}
```

**改造前后对齐表**：

| 项 | 改造前 | 改造后 | 等价? |
|----|--------|--------|-------|
| ① `GameUpdateEvent()` | Hook 体内，SAFE 段 | tick 闭包内，被适配器 SAFE 包裹 | ✅ |
| ② `CheckAccountChanged()` | Hook 体内，① 之后 | tick 闭包内，① 之后 | ✅ 顺序一致 |
| ③ `CALL_ORIGIN` | SAFE 之外 | 适配器 Hook 内、SAFE 之外 | ✅ |
| SAFE 边界 | 包 ①② | 包 `s_tick()`(=①②) | ✅ |
| MoveSync Hook | 装 | 原样装 | ✅ |
| `CheckAccountChanged` 函数体 | — | 一行未改 | ✅ |

---

## 六、关键：为何用 tick 闭包而非"订阅者"（修订落地计划 §4.2 B3）

落地计划 §4.2 B3 原写：把 `CheckAccountChanged` **改成 `GameUpdateEvent` 的订阅者**。细化后发现该做法会破坏**不变量 C**，故**修订为 tick 闭包内显式调用**。理由如下。

### 现状订阅顺序（运行时事实）

`GameUpdateEvent` 的订阅者按订阅先后被依次调用。当前订阅时机（均在 `cheat::Init()` 内）：

1. `cheat.cpp:73` `config::SetupUpdate(&GameUpdateEvent)` → **config 落盘检查最先订阅**。
2. `AddFeatures({...})` 触发各 Feature 的 `GetInstance()` 构造 → **各 Feature 的 `OnGameUpdate` 依注册序订阅**。
3. `InstallEventHooks()` 装心跳 Hook（最后）。

而 `CheckAccountChanged` **不是订阅者**——它在 Hook 体里、`GameUpdateEvent()` 返回**之后**被显式调用。即：**它保证在当帧所有订阅者跑完之后才执行**（不变量 C）。

### 两种做法对比

| 做法 | 是否保住不变量 C | 风险 |
|------|-----------------|------|
| **订阅者**（原 B3） | ❌ 否。它会插进订阅链某处，位置取决于"何时订阅"。若在 `SetupUpdate` 与各 Feature 之间订阅，则会在 Feature 之前跑——顺序漂移 | 需精心保证"最后订阅"才等价，脆弱；且未来新增 Feature 可能再次打乱 |
| **tick 闭包显式调用**（修订） | ✅ 是。`[]{ GameUpdateEvent(); CheckAccountChanged(); }` 与原 Hook 体逐字同构 | 无。纯搬运，零顺序分析负担 |

**结论**：纯重构阶段追求**严格等价 + 最小认知负担**，采用 tick 闭包。`CheckAccountChanged` 保持"非订阅者、在事件之后显式调用"的原语义，且它仍是 `cheat.cpp` 里可见的 Genshin 业务，符合"引擎事归适配器、业务归游戏层"的边界。

> 备注：若将来确有"让账号检测解耦成订阅者"的需求，那属于**功能性重构**，应在 P0–P2 之外单独立项并单独验证顺序影响，不与本次引擎解耦混做。

---

## 七、B 子线提交拆分

| commit | 内容 | 验证 |
|--------|------|------|
| B-1 | 新增 `IHeartbeat.h`（core）+ `UnityHeartbeat.{h,cpp}`（adapter），加入工程。**暂不接线** | 编译通过（新类无人调用） |
| B-2 | 改造 `cheat.cpp`：删除本地 `GameManager_Update_Hook`，`InstallEventHooks` 改用 `heartbeat.Install(tick 闭包)`；MoveSync 保留 | 编译 + 注入 Genshin，冒烟见 §八 |

> B-1 纯加代码（安全）；B-2 是唯一切换点，回归风险集中于此，失败可单步 `revert`。

---

## 八、B 子线专属验收清单

注入 Genshin 后逐项确认（对照改造前）：
1. **每帧订阅者仍跑**：开一个每帧功能（如 `AutoRun` / `MobVacuum`），确认生效 → 证明 `GameUpdateEvent()`（①）仍每帧触发。
2. **配置落盘仍工作**：改任意开关，等 ~2s，确认 `cfg.json` 落盘 → 证明 `config` 的 `GameUpdateEvent` 订阅（落盘检查）仍在跑。
3. **账号检测/切号仍工作**：切换游戏账号（若可），确认触发 `AccountChangedEvent` → Profile 自动切换 → 证明 `CheckAccountChanged`（②）仍每帧被调、2s 节流正常。
4. **顺序未漂移**：确认功能表现与旧版无差异（尤其依赖"账号检测在功能逻辑之后"的行为，若有）。
5. **移动同步未受损**：确认 `MoveSyncEvent` 相关功能（依赖移动同步的传送/坐标类）正常 → 证明 MoveSync Hook 保留生效。
6. **原逻辑放行**：游戏本身运行正常、不卡不崩 → 证明 `CALL_ORIGIN`（③）仍在 SAFE 之外正确调用。

---

## 九、B 子线微决策（已确认）

1. **`s_tick` 承载形态**（已定）：文件内 `std::function` 单槽 + 具名 Hook 函数（契合 `HookManager` 具名指针 + `CALL_ORIGIN` 约束）。
2. **`heartbeat` 实例所有权**（已定）：P1 用 `InstallEventHooks` 内 `static` 局部；P2 bootstrap 引入后交 bootstrap 持有，与 A 子线 `UnityLifecycle`/`UnityBinding` 归口一致。
3. **`CheckAccountChanged` 触发方式**（已定）：**保持非订阅者、由 tick 闭包显式调用**（§六结论，修订落地计划 B3），严格等价、无顺序风险。本次不把它解耦成订阅者。

---

> 一句话：B 子线把 `GameManager.Update` 的 Hook（引擎事）搬进 `UnityHeartbeat`，`cheat.cpp` 改为向心跳注册一个 tick 闭包 `{ GameUpdateEvent(); CheckAccountChanged(); }`——与原 Hook 体逐字同构，三个不变量全保住。**修订了落地计划 B3**：不把账号检测改成订阅者（会破坏"在事件之后执行"的顺序不变量），而用 tick 闭包显式调用，严格等价。分 B-1/B-2 两个 commit，风险集中在 B-2 且可单步回滚。
