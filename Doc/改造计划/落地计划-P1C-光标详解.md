# P1 · C 子线详解 · 光标控制

> 关联：[落地计划-P0-P2](./落地计划-P0-P2-Unity侧重构.md) §4.3 的逐文件展开。三条子线里最轻的一条。
>
> 铁律：**行为等价**。核心难点不是代码量，而是**与 P2 基类改造干净解耦**——P1-C 只新增 `ICursorController`/`UnityCursor` 并让 `GenshinCM` 委托给它，**不动 `CheatManagerBase` 的纯虚签名**（那是 P2 §5.2 的事）。

---

## 一、C 子线的范围

**动**：把光标显隐/锁定的实现，从 `GenshinCM` 的两个 override 抽到 `unity-il2cpp` 适配器的 `UnityCursor` 背后。

**不动 / 明确留置**：
- `CheatManagerBase` 的纯虚 `CursorSetVisibility/GetVisibility` 签名与调用点（`CheatManagerBase.cpp:496-501`）——**P1-C 一律不碰**，留给 P2 §5.2（注入 `ICursorController*`、删纯虚）。
- `GenshinCM` 仍 `override` 这两个函数（不删），只是函数体改为**委托** `UnityCursor`。这层"薄委托"是 P1→P2 的桥。
- `GenshinCM` 其余职责（账号/Profile 绑定等）——与光标无关，不动。

**一句话边界**：P1-C 把"怎么控制 Unity 光标"这件引擎事搬进适配器；"基类如何使用光标"维持原样，等 P2 再收口。

---

## 二、涉及的真实签名（已核对源码）

| 符号 | 位置 | 说明 |
|------|------|------|
| `CursorSetVisibility` / `CursorGetVisibility`（纯虚） | `CheatManagerBase.h:36-37` | 基类留给子类的两个纯虚 |
| 基类调用点 | `CheatManagerBase.cpp:496-501`（`OnWndProc`） | 菜单开→记住旧态并强制显示；菜单关→还原 |
| `GenshinCM` 实现 | `GenshinCM.cpp:16-25` | 现有实现（下引） |
| `app::Cursor_set_visible` | IL2CPP 绑定 | `void(bool, MethodInfo*)`，即 `UnityEngine.Cursor.visible` setter |
| `app::Cursor_get_visible` | IL2CPP 绑定 | `bool(MethodInfo*)` |
| `app::Cursor_set_lockState` | IL2CPP 绑定 | `void(CursorLockMode__Enum, MethodInfo*)` |
| `app::CursorLockMode__Enum` | IL2CPP 绑定 | `None` / `Locked`（`GenshinCM.cpp:19` 用到） |

### 现有实现（逐字，作为等价基准）

```cpp
// GenshinCM.cpp:16-25
void GenshinCM::CursorSetVisibility(bool visibility)
{
    app::Cursor_set_visible(visibility, nullptr);
    app::Cursor_set_lockState(visibility ? app::CursorLockMode__Enum::None
                                          : app::CursorLockMode__Enum::Locked, nullptr);
}
bool GenshinCM::CursorGetVisibility()
{
    return app::Cursor_get_visible(nullptr);
}
```

### 基类使用方式（等价基准）

```cpp
// CheatManagerBase.cpp:494-501（OnWndProc 内）
if (s_IsMenuShowed)
{
    m_IsPrevCursorActive = CursorGetVisibility();     // 记住打开菜单前光标是否可见
    if (!m_IsPrevCursorActive)
        CursorSetVisibility(true);                    // 之前不可见→强制显示
}
else if (!m_IsPrevCursorActive)
    CursorSetVisibility(false);                       // 关菜单且之前不可见→还原为隐藏
```

**不变量**：
- **不变量 A**：`CursorSetVisibility(v)` = 设 `visible=v` **且** 设 `lockState=(v?None:Locked)`，两步顺序不变。
- **不变量 B**：`CursorGetVisibility()` = 读 `Cursor.visible`。
- **不变量 C**：基类 `OnWndProc` 的开合逻辑（记忆/还原旧态）完全不变——P1-C 不碰基类。

> 关键观察：这两个函数用的是 `app::Cursor_*`，即 **`UnityEngine.Cursor`**——是 **Unity 引擎级 API，而非 Genshin 专属**。故 `UnityCursor` 天然属于 `unity-il2cpp` 适配器，且**可跨任意 Unity(IL2CPP) 游戏复用**（前提：目标引用了 UnityEngine 的 Cursor 类，绝大多数带鼠标交互的 Unity 游戏都有）。

---

## 三、接口签名（C 子线交付的接口）

放 `cheat-base/src/cheat-base/runtime/ICursorController.h`，引擎无关，**不含任何 `app::`**。

```cpp
#pragma once

namespace runtime
{
    // 光标显隐/锁定的引擎无关契约。
    // Unity: 经 UnityEngine.Cursor(app::Cursor_*)实现。
    // UE5  : 由 UE 侧实现（P3，游戏各自方式）。
    class ICursorController
    {
    public:
        virtual ~ICursorController() = default;

        // 设置光标可见性。语义约定：可见时解锁(自由移动)，不可见时锁定。
        virtual void SetVisibility(bool visible) = 0;

        // 读取当前光标是否可见。
        virtual bool GetVisibility() = 0;
    };
}
```

---

## 四、适配器实现（`cheat-library/src/framework/adapters/unity-il2cpp/`）

### `UnityCursor.{h,cpp}`

```cpp
// UnityCursor.h
#pragma once
#include <cheat-base/runtime/ICursorController.h>

namespace runtime::unity
{
    class UnityCursor : public ICursorController
    {
    public:
        void SetVisibility(bool visible) override;
        bool GetVisibility() override;
    };
}
```

实现（`.cpp`，include `pch-il2cpp.h`、`il2cpp-appdata.h`）——**逐字搬自 `GenshinCM.cpp:16-25`**：

```cpp
void UnityCursor::SetVisibility(bool visible)
{
    app::Cursor_set_visible(visible, nullptr);
    app::Cursor_set_lockState(visible ? app::CursorLockMode__Enum::None
                                       : app::CursorLockMode__Enum::Locked, nullptr);
}
bool UnityCursor::GetVisibility()
{
    return app::Cursor_get_visible(nullptr);
}
```

**不变量核对**：A（visible + lockState 两步同序）✅、B（读 visible）✅——与原实现逐字同构。无 SEH 包裹（原实现也没有），保持等价。

---

## 五、`GenshinCM` 改造前后对照

### 改造前（现状）

```cpp
// GenshinCM.h:61-62
void CursorSetVisibility(bool visibility) final;
bool CursorGetVisibility() final;

// GenshinCM.cpp:16-25  —— 直接调 app::Cursor_*
void GenshinCM::CursorSetVisibility(bool v) { app::Cursor_set_visible(v,nullptr); app::Cursor_set_lockState(...); }
bool GenshinCM::CursorGetVisibility()       { return app::Cursor_get_visible(nullptr); }
```

### 改造后（C 子线目标：薄委托，签名不变）

```cpp
// GenshinCM.h —— 签名不变，仍 override；新增一个 UnityCursor 成员
#include <adapters/unity-il2cpp/UnityCursor.h>
...
    void CursorSetVisibility(bool visibility) final;
    bool CursorGetVisibility() final;
private:
    runtime::unity::UnityCursor m_cursor;   // 新增：委托目标(单例成员，长生命周期)

// GenshinCM.cpp —— 函数体改为委托，app::Cursor_* 已移入 UnityCursor
void GenshinCM::CursorSetVisibility(bool visibility) { m_cursor.SetVisibility(visibility); }
bool GenshinCM::CursorGetVisibility()                { return m_cursor.GetVisibility(); }
```

**改造前后对齐表**：

| 项 | 改造前 | 改造后 | 等价? |
|----|--------|--------|-------|
| `CursorSetVisibility` 行为 | 直接调 `app::Cursor_*` 两步 | 委托 `m_cursor.SetVisibility`（同两步） | ✅ |
| `CursorGetVisibility` 行为 | 直接读 `Cursor.visible` | 委托 `m_cursor.GetVisibility` | ✅ |
| 基类 `OnWndProc` 多态调用 | 调 `GenshinCM::Cursor*` | 不变（仍多态到 `GenshinCM::Cursor*`，只是内部转发） | ✅ |
| 纯虚签名/基类 | 不变 | 不变（P2 才改） | ✅ |

---

## 六、关键：为何 P1-C 保留基类纯虚、只做薄委托

设计终局（P2 §5.2）是：`CheatManagerBase` 持有 `ICursorController*`（注入），删掉两个纯虚，`OnWndProc` 直接用注入的控制器；`GenshinCM` 不再 override。

**但 P1-C 不一步到位**，理由：

1. **与 P2 解耦、缩小风险面**：改基类签名 + 改注入路径属于"框架内核改动"，牵涉 `CheatManagerBase` 构造/`Init` 与 bootstrap（P2 才引入）。若在 P1 就动，会把 A/B/C 的适配器抽取与内核重构耦合进同一批提交，回归定位困难。
2. **薄委托是无损桥**：P1-C 后，`app::Cursor_*` 已全部移入 `UnityCursor`（引擎代码归位到位），`GenshinCM` 只剩两行转发。行为逐字等价，零风险。
3. **P2 收口顺滑**：P2 §5.2 时，`UnityCursor` 已就绪，只需：基类加 `ICursorController*` 成员 + 改 `OnWndProc` 用它 → 删 `GenshinCM` 两个 override 和 `m_cursor` 成员 → bootstrap 把 `UnityCursor` 注入基类。`UnityCursor` 实现本身**一行不用再改**。

**P1→P2 所有权流转**：P1-C 中 `UnityCursor` 是 `GenshinCM` 的成员（单例，常驻）；P2 改由 bootstrap 持有并注入基类——与 A（`UnityLifecycle`/`UnityBinding`）、B（`UnityHeartbeat`）的"P1 就近持有、P2 交 bootstrap"归口一致。

---

## 七、C 子线提交拆分

| commit | 内容 | 验证 |
|--------|------|------|
| C-1 | 新增 `ICursorController.h`（core）+ `UnityCursor.{h,cpp}`（adapter），加入工程。**暂不接线** | 编译通过（新类无人调用） |
| C-2 | `GenshinCM` 增 `m_cursor` 成员；两个 `Cursor*` 函数体改为委托 `m_cursor`（删除其中的 `app::Cursor_*` 直调） | 编译 + 注入 Genshin，开合菜单光标行为与旧版一致 |

> C-1 纯加代码（安全）；C-2 只把两行实现改成转发（回归面极小），失败可单步 `revert`。

---

## 八、C 子线专属验收清单

注入 Genshin 后逐项确认（对照改造前）：
1. **开菜单**：按 F1 打开菜单 → 光标出现且可自由移动（`visible=true` + `lockState=None`）。
2. **关菜单**：再按 F1 关闭 → 光标恢复到打开前状态（若之前被游戏锁定/隐藏，则重新隐藏+锁定）。
3. **反复开合**：多次开合，`m_IsPrevCursorActive` 记忆/还原逻辑正常，不出现"关了菜单光标还赖着"或"开菜单点不到控件"。
4. **与游戏内视角联动**：关菜单后游戏内转视角/操作正常（证明 `lockState=Locked` 正确恢复）。

---

## 九、C 子线待定微决策（动工前拍板）

1. **`UnityCursor` 归属目录**：确认放 `adapters/unity-il2cpp/`（它是 Unity 引擎级、可跨 Unity 游戏复用），而非 `games/genshin/`。除非该实现将来要掺 Genshin 专有逻辑（目前没有），否则放适配器更合理。
2. **`m_cursor` 承载形态**：P1 用 `GenshinCM` 成员（单例常驻）。确认 P2 改由 bootstrap 持有注入基类——与 A/B 归口一致。
3. **include 前缀**：沿用 A 子线已定风格——`GenshinCM` 引用适配器写 `#include <adapters/unity-il2cpp/UnityCursor.h>`。

---

> 一句话：C 子线把 `app::Cursor_*` 两个实现搬进 `UnityCursor`，`GenshinCM` 的两个 override 改成两行委托、签名不变、基类不碰——行为逐字等价。刻意保留基类纯虚作为 P1→P2 的无损桥，P2 再统一收口为"基类注入 `ICursorController*`"。分 C-1/C-2 两个 commit，回归面极小。
