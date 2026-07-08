# 设计文档 · UE4/UE5 静态模块化框架改造计划

## 1. 目标

本计划的目标是把当前项目改造成一个保留现有运行形态的多引擎静态模块化框架。

目标形态保持不变：

- 仍然是 Windows 进程内注入式 DLL。
- 仍然由 `injector.exe` 启动或注入目标进程。
- 仍然由 `CLibrary.dll` 在目标进程内初始化框架。
- 仍然使用 ImGui + DX11/DX12 叠加界面。
- 仍然使用现有配置、热键、事件、Hook、渲染和 Feature 管理能力。
- 不做外部 DLL 模块热加载，模块全部静态编译进目标 DLL。

新增能力：

- 支持 Unity IL2CPP、Unreal Engine 4、Unreal Engine 5 三类目标引擎。
- 通过引擎适配层隔离 IL2CPP 与 UE 的运行时差异。
- 通过模块元信息声明模块支持的目标引擎。
- 通过配置控制目标引擎、模块启用状态、模块分组与显示顺序。
- 让新增功能模块尽量不再修改中央注册逻辑。

## 2. 非目标

以下内容不纳入第一阶段目标：

- 不做外部插件热加载。
- 不做脚本模块系统。
- 不追求一个 DLL 同时适配所有游戏的完全通用二进制。
- 不把 Unity 的 Entity 与 UE 的 Actor 强行抽象成统一游戏对象。
- 不重写现有所有功能模块。
- 不更换构建系统，仍优先使用当前 Visual Studio/MSBuild 工程。
- 不把 UE4/UE5 SDK 细节暴露到通用框架层。

## 3. 总体判断

当前项目已经具备可改造成框架的基础。

可直接复用的部分主要在 `cheat-base`：

- `Feature` 抽象。
- `CheatManagerBase` 功能管理和 ImGui 菜单。
- `config` JSON 配置和 Profile 机制。
- `events` 事件系统。
- `HookManager` Detours 封装。
- `renderer` DX11/DX12 ImGui 渲染后端。
- `Logger`、`Hotkey`、`ResourceLoader`、`util` 等基础设施。

需要隔离或重写的部分主要在 `cheat-library`：

- `main.cpp` 固定等待 `UserAssembly.dll` 并初始化 IL2CPP。
- `il2cpp-init.cpp`、`appdata/*`、`ILPatternScanner` 都是 IL2CPP 专属。
- `cheat.cpp` 硬编码 Feature 注册和 Genshin 游戏事件 Hook。
- 大量功能模块直接依赖 `app::`、`MethodInfo`、`GET_SINGLETON`。
- `GenshinCM` 混合了通用菜单管理、Genshin 光标控制、账号/Profile 业务。

因此改造策略是：保留 `cheat-base` 作为通用框架底座，把 `cheat-library` 拆成 Bootstrap、Engine Adapter、Host Modules 三部分。

## 4. 目标分层

建议目标分层如下：

```text
cheat-base
  通用框架层
  配置、事件、日志、热键、HookManager、渲染、Feature/Module 管理

cheat-library
  注入 DLL 宿主层
  Bootstrap、Engine Adapter 选择、模块注册、资源加载

engine-il2cpp
  Unity IL2CPP 适配层
  UserAssembly 等待、IL2CPP 初始化、静态偏移、Pattern Scanner、IL2CPP 心跳事件

engine-unreal
  UE4/UE5 适配层
  GWorld/GObjects/GNames/FNamePool、UObject、UFunction、ProcessEvent、UE Tick/Event Bridge

modules
  静态编译模块
  每个模块声明支持 IL2CPP、UE4、UE5 或 Any
```

在现有仓库中可以先不大规模移动目录，推荐先采用低风险目录：

```text
cheat-library/src/framework/bootstrap/
cheat-library/src/framework/engine/
cheat-library/src/engine/il2cpp/
cheat-library/src/engine/unreal/
cheat-library/src/user/cheat/
```

等接口稳定后，再考虑进一步调整物理目录。

## 5. 引擎适配器

引擎适配器是本次改造的核心。它负责把“目标引擎如何初始化、如何产生每帧事件、如何控制光标、如何访问运行时对象”隔离出来。

建议最小接口：

```cpp
enum class EngineKind
{
    Il2cpp,
    Unreal4,
    Unreal5
};

class IEngineAdapter
{
public:
    virtual ~IEngineAdapter() = default;

    virtual EngineKind Kind() const = 0;
    virtual const char* Name() const = 0;

    virtual bool WaitForRuntime() = 0;
    virtual bool Initialize() = 0;
    virtual void InstallEventHooks() = 0;
    virtual void Shutdown() = 0;
};
```

第一版接口要保持窄。不要在初始版本里把 UE 的全部对象系统都塞进通用接口。

## 6. IL2CPP 适配器职责

现有 IL2CPP/Genshin 初始化逻辑应被包进 `Il2cppEngineAdapter`。

职责包括：

- 等待 `UserAssembly.dll`。
- 执行现有 `init_il2cpp()`。
- 处理静态偏移和 Pattern Scanner fallback。
- 安装当前 `GameManager_Update` Hook。
- 安装当前 `MoveSync` Hook。
- 将当前 `GameUpdateEvent` 迁移或映射到通用 `EngineUpdateEvent`。
- 保持现有 Genshin 功能行为不变。

迁移前：

```text
Run()
  wait UserAssembly.dll
  init_il2cpp()
  cheat::Init()
  InstallEventHooks()
```

迁移后：

```text
Run()
  create Il2cppEngineAdapter
  adapter.WaitForRuntime()
  adapter.Initialize()
  cheat::Init(adapter)
  adapter.InstallEventHooks()
```

## 7. UE4/UE5 适配器职责

UE 适配器不应该复制 IL2CPP 的 `app::` 模式，而应封装 UE 自己的运行时模型。

建议一个 `UnrealEngineAdapter` 同时支持 UE4 和 UE5：

```cpp
enum class UnrealVersion
{
    UE4,
    UE5
};

class UnrealEngineAdapter : public IEngineAdapter
{
public:
    explicit UnrealEngineAdapter(UnrealVersion version);
};
```

UE 适配器职责包括：

- 等待 UE 运行时关键结构就绪。
- 定位或初始化 `GWorld`。
- 定位或初始化 `GUObjectArray` / `GObjects`。
- 定位或初始化 `GNames` / `FNamePool`。
- 提供 UObject 基础访问能力。
- 提供 UWorld / Actor 枚举能力。
- 提供 UFunction 查找能力。
- 封装 `ProcessEvent` 调用。
- 安装 UE Tick 或替代心跳 Hook。
- 将 UE 帧事件映射到通用 `EngineUpdateEvent`。
- 支持 DX11/DX12 Overlay 初始化。

UE4 和 UE5 差异应封装在 `UnrealProfile` 中：

```cpp
struct UnrealProfile
{
    UnrealVersion version;
    uintptr_t gWorldOffset;
    uintptr_t gObjectsOffset;
    uintptr_t gNamesOffset;
    uintptr_t processEventOffset;
    bool usesFNamePool;
    bool usesChunkedObjects;
};
```

`UnrealProfile` 可以来自编译期常量、目标游戏配置或后续 Pattern Scanner。

## 8. UE4/UE5 差异处理原则

UE4 和 UE5 的差异不能泄漏到 `cheat-base`。

需要重点隔离的差异：

- `GNames` 与 `FNamePool`。
- `GUObjectArray` 布局。
- `UObject` 基础布局。
- `FName` 内部表示。
- `ProcessEvent` 定位方式。
- `GWorld` 获取方式。
- Tick Hook 位置。
- SDK 生成工具输出结构。

通用模块不能直接包含 UE SDK 头文件。UE SDK 只允许出现在 `engine-unreal` 或 UE 专属模块中。

## 9. 通用事件模型

当前项目的 `GameUpdateEvent` 名称和来源都偏 Genshin/IL2CPP。支持 UE 后建议新增通用事件：

```cpp
namespace engine::events
{
    extern TEvent<> EngineReadyEvent;
    extern TEvent<> EngineUpdateEvent;
    extern TEvent<> EngineShutdownEvent;
}
```

映射关系：

- IL2CPP：`GameManager_Update_Hook` 触发 `EngineUpdateEvent`。
- UE4/UE5：`UWorld::Tick`、`UGameEngine::Tick`、`UGameViewportClient` 或渲染帧兜底触发 `EngineUpdateEvent`。

第一版只需要稳定的 `EngineUpdateEvent`，不要过早设计过多事件。

## 10. 模块系统

现有 `Feature` 可以保留，但需要增加模块元信息。

建议新增：

```cpp
enum class EngineMask : uint32_t
{
    None    = 0,
    Il2cpp  = 1 << 0,
    Unreal4 = 1 << 1,
    Unreal5 = 1 << 2,
    Unreal  = Unreal4 | Unreal5,
    Any     = Il2cpp | Unreal4 | Unreal5
};

struct ModuleInfo
{
    const char* id;
    const char* name;
    const char* category;
    EngineMask supportedEngines;
    bool enabledByDefault;
};
```

现有 `FeatureGUIInfo` 可以继续负责 GUI 分组，也可以逐步合并到 `ModuleInfo`。

推荐演进路线：

```text
阶段 1：保留 FeatureGUIInfo，新增 GetModuleInfo()
阶段 2：CheatManagerBase 使用 ModuleInfo 过滤模块
阶段 3：FeatureGUIInfo 合并或降级为 ModuleInfo 的 GUI 字段
```

## 11. 模块兼容性

模块必须声明支持的引擎。

分类建议：

```text
EngineMask::Any
  设置、热键、About、FPS、通用 UI、日志窗口

EngineMask::Il2cpp
  当前 Genshin/IL2CPP 专属功能
  GodMode、NoClip、AutoLoot、Teleport、ESP、InteractiveMap 等

EngineMask::Unreal
  UE 通用调试/可视化模块
  UObject Browser、Actor List、World Info、ProcessEvent Logger

EngineMask::Unreal4 / Unreal5
  只适配特定 UE 大版本的模块
```

模块管理器根据当前引擎过滤不兼容模块。

规则：

- 不兼容模块不注册到菜单。
- 不兼容模块不触发生命周期。
- 不兼容模块配置可以保留，但运行时不读取。
- Any 模块必须不能包含 `app::` 或 UE SDK 类型。

## 12. 模块注册

当前中央注册在 `cheat-library/src/user/cheat/cheat.cpp`，新增功能需要手动改 include 和 `AddFeatures`。

第一版不需要完全自动注册，可以先做低风险的注册表封装：

```cpp
void RegisterCommonModules(ModuleRegistry& registry);
void RegisterIl2cppModules(ModuleRegistry& registry);
void RegisterUnrealModules(ModuleRegistry& registry);
```

Bootstrap 根据当前引擎调用：

```cpp
RegisterCommonModules(registry);

if (engine.Kind() == EngineKind::Il2cpp)
    RegisterIl2cppModules(registry);

if (engine.Kind() == EngineKind::Unreal4 || engine.Kind() == EngineKind::Unreal5)
    RegisterUnrealModules(registry);
```

后续再引入静态注册宏：

```cpp
REGISTER_MODULE(GodMode);
```

不建议第一阶段直接依赖静态初始化自动注册，因为需要处理初始化顺序问题。

## 13. 配置化

当前 `cfg.json` 已支持 Profile 和 shared 配置。建议第一版只增加最小 runtime 和 modules 配置。

示例：

```json
{
  "runtime": {
    "engine": "unreal5",
    "render_backend": "auto"
  },
  "modules": {
    "framework.settings": {
      "enabled": true
    },
    "ue.object_browser": {
      "enabled": true
    },
    "il2cpp.god_mode": {
      "enabled": false
    }
  },
  "current_profile": "default",
  "shared": {},
  "profiles": {
    "default": {}
  }
}
```

配置职责：

- `runtime.engine` 控制引擎适配器。
- `runtime.render_backend` 控制 DX11/DX12/auto。
- `modules.<id>.enabled` 控制模块启用。
- 模块内部设置继续使用现有 `NF/NFS/NFEX` 体系。

第一版不要求重写配置系统，只要在现有 `config` 体系外层增加 runtime/module 配置读取即可。

## 14. Bootstrap 流程

当前 `Run()` 应演进为统一 Bootstrap。

目标流程：

```text
1. 初始化 ResourceLoader 与当前路径
2. 初始化 cfg.json
3. 初始化 Logger
4. 读取 runtime.engine
5. 创建 EngineAdapter
6. EngineAdapter.WaitForRuntime()
7. EngineAdapter.Initialize()
8. 初始化通用事件与模块注册表
9. 注册 Common 模块
10. 注册当前引擎模块
11. 根据 EngineMask 与配置过滤模块
12. 初始化 CheatManagerBase / UI
13. EngineAdapter.InstallEventHooks()
14. 框架进入运行状态
```

`dllmain.cpp` 可以继续只负责起线程，不需要复杂化。

## 15. UE 最小闭环

UE 支持的第一阶段不要追求复杂功能。最小闭环定义如下：

- 能注入 UE4 或 UE5 目标进程。
- 能初始化 ImGui Overlay。
- 能按 F1 打开菜单。
- 能识别当前运行时为 UE4 或 UE5。
- 能定位基础 UE 运行时结构，至少包括 `GWorld` 或可替代的 World 获取方式。
- 能产生每帧 `EngineUpdateEvent`。
- 能显示一个 UE Runtime Info 模块。

UE Runtime Info 模块可以显示：

- 当前引擎版本配置。
- 当前渲染后端。
- 是否定位到 World。
- UObject 数量。
- 当前 Level 或 World 名称。
- 每帧 Tick 计数。

这能验证框架、Overlay、事件、UE Adapter 是否跑通。

## 16. UE 后续模块建议

UE 最小闭环完成后，可以逐步增加以下模块：

- `ue.runtime_info`：运行时状态。
- `ue.object_browser`：UObject 浏览器。
- `ue.actor_list`：当前 World Actor 列表。
- `ue.world_info`：World/Level 信息。
- `ue.process_event_logger`：ProcessEvent 调用观察。
- `ue.function_caller`：按对象和函数名触发 UFunction。
- `ue.property_inspector`：对象属性查看。

这些模块主要用于验证 UE 适配层，不应和现有 Genshin 功能混在一起。

## 17. 构建策略

由于不需要热加载，继续使用静态编译。

推荐第一版使用编译期宏控制目标：

```text
TARGET_ENGINE_IL2CPP
TARGET_ENGINE_UE4
TARGET_ENGINE_UE5
```

后续可以增加构建配置：

```text
Release_IL2CPP
Release_UE4
Release_UE5
```

但不建议第一步就大改 `.sln/.vcxproj`。先在现有 `Release` 或 `Debug` 下通过宏和少量新增文件验证结构即可。

## 18. 注入器改造

`injector` 不需要知道目标引擎。它只需要参数化目标进程和 DLL。

建议改造：

- 目标进程名从 `cfg.ini` 读取。
- 目标 exe 路径从 `cfg.ini` 读取。
- 命令行参数从 `cfg.ini` 读取。
- 是否挂起启动从配置读取。
- 是否使用 explorer 作为父进程从配置读取。
- DLL 路径继续支持 `argv[1]` 覆盖。

第一版可暂不改注入器，只要 UE 目标可通过现有 argv 或手动路径跑通即可。

## 19. 迁移路线

建议按以下顺序执行，保证每一步都可构建、可回归。

### P0：建立文档与边界

- 明确目标形态。
- 明确不做热加载。
- 明确 UE4/UE5 通过 Adapter 支持。
- 明确现有 IL2CPP 功能不重写。

### P1：增加模块元信息

- 新增 `ModuleInfo`。
- 新增 `EngineMask`。
- 为现有 Feature 增加默认模块信息。
- `Settings`、`Hotkeys`、`About` 标记为 `Any`。
- Genshin 功能标记为 `Il2cpp`。

### P2：封装模块注册

- 新增 `ModuleRegistry`。
- 从 `cheat.cpp` 提取 `RegisterCommonModules` 和 `RegisterIl2cppModules`。
- 保持注册内容不变。
- 保持现有功能可用。

### P3：抽象 EngineAdapter

- 新增 `IEngineAdapter`。
- 新增 `Il2cppEngineAdapter`。
- 把等待 `UserAssembly.dll` 和 `init_il2cpp()` 迁入 Adapter。
- 把 `InstallEventHooks()` 迁入 Adapter。
- `cheat::Init()` 接收 Adapter 或 EngineContext。

### P4：新增通用 EngineUpdateEvent

- 新增 `engine::events::EngineUpdateEvent`。
- IL2CPP 下由原 `GameManager_Update_Hook` 触发。
- 逐步把可通用模块从 `GameUpdateEvent` 迁到 `EngineUpdateEvent`。
- Genshin 专属事件继续保留在 IL2CPP/Genshin 层。

### P5：UE Adapter 空壳

- 新增 `UnrealEngineAdapter`。
- 支持 `UnrealVersion::UE4` / `UE5`。
- 实现 `Name()`、`Kind()`、基础初始化流程。
- 可以先不定位完整 UObject 系统。
- 能让通用 UI 模块在 UE 目标中显示。

### P6：UE 最小闭环

- 定位 World 或可替代 World 入口。
- 实现 UE 心跳事件。
- 增加 `ue.runtime_info` 模块。
- 验证 UE4 和 UE5 至少各一个目标。

### P7：UE 对象服务

- 增加 UObject 基础包装。
- 增加 Object 枚举。
- 增加 FName 转字符串。
- 增加 UFunction 查找。
- 增加 ProcessEvent 调用封装。

### P8：模块配置化

- 增加 `runtime.engine`。
- 增加 `modules.<id>.enabled`。
- 菜单根据配置隐藏或禁用模块。
- 模块顺序可配置。

## 20. 验收标准

### IL2CPP 回归验收

- Debug x64 可构建。
- Release x64 可构建。
- 原有 `CLibrary.dll` 输出路径不变。
- 原有 `injector.exe` 使用方式不变。
- Genshin/IL2CPP 初始化流程不回归。
- 原有功能菜单和配置仍可用。

### UE4 验收

- 可注入 UE4 目标进程。
- ImGui 菜单可打开。
- `EngineKind::Unreal4` 正确识别。
- `EngineUpdateEvent` 能持续触发。
- `ue.runtime_info` 能显示基础状态。

### UE5 验收

- 可注入 UE5 目标进程。
- ImGui 菜单可打开。
- `EngineKind::Unreal5` 正确识别。
- DX11 或 DX12 后端至少一个可用。
- `EngineUpdateEvent` 能持续触发。
- `ue.runtime_info` 能显示基础状态。

## 21. 主要风险

| 风险 | 影响 | 应对 |
|------|------|------|
| UE4/UE5 内部结构差异 | Adapter 不稳定 | 用 `UnrealProfile` 隔离版本差异 |
| UE SDK 生成依赖目标游戏 | 无法真正一套 SDK 跑所有 UE 游戏 | 接受每目标编译期产物差异 |
| UE Tick Hook 不稳定 | `EngineUpdateEvent` 缺失 | 允许用渲染帧作为心跳兜底 |
| 现有模块强依赖 `app::` | 无法通用于 UE | 全部标记为 `Il2cpp`，不强行迁移 |
| 静态注册初始化顺序 | 模块注册不确定 | 第一版用显式注册函数，后续再做注册宏 |
| 配置系统全局字段注册 | 动态卸载困难 | 不做热加载，避免第一阶段重构压力 |
| DX12 在 UE5 下差异 | Overlay 不稳定 | 先验证 DX11/DX12，必要时针对 UE5 修补 |

## 22. 推荐第一批改动

最小可执行改造建议：

1. 新增 `EngineKind`、`EngineMask`、`ModuleInfo`。
2. 新增 `IEngineAdapter`。
3. 新增 `ModuleRegistry`。
4. 从 `cheat.cpp` 提取 `RegisterCommonModules` 和 `RegisterIl2cppModules`。
5. 新增 `Il2cppEngineAdapter`，只包现有逻辑，不改变行为。
6. 新增 `engine::events::EngineUpdateEvent`，先由 IL2CPP Hook 转发触发。
7. 新增 `UnrealEngineAdapter` 空壳。
8. 新增 `ue.runtime_info` 空模块，先只验证菜单显示。

这批改动完成后，框架结构已经具备支持 UE4/UE5 的入口，但不会破坏当前 IL2CPP 功能。

## 23. 最终形态总结

最终项目应演进为：

```text
一个静态编译式多引擎注入框架。

Core 负责：配置、事件、日志、Hook、渲染、UI、模块管理。
IL2CPP Adapter 负责：Unity/IL2CPP 初始化、偏移、IL2CPP 事件桥。
Unreal Adapter 负责：UE4/UE5 对象系统、World、Tick、ProcessEvent。
Modules 负责：具体功能，声明自己支持哪个引擎。
Bootstrap 负责：读配置、选 Adapter、注册模块、启动框架。
```

这个方向能最大化复用现有项目，同时为 UE4/UE5 目标保留清晰扩展空间。
