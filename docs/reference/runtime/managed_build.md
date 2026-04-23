# Managed Build v0

这份文档描述当前 `managed` 构建模式已经接通的最小稳定子集。

当前入口是：

```bash
lona-ir --emit mbc input.lo output-managed.bc
```

`mbc` 的目标不是“完整托管语言后端”，而是先把：

- managed 编译开关
- 基础指针限制
- 托管运行时分配入口
- 分配点类型 metadata

接成一条可被 [lona-MVM](https://github.com/Lona-Lang/lona-MVM) 消费的 bitcode 路径。

## 1. 当前范围

`--emit mbc` 当前仍然输出单最终 linked bitcode。

也就是说：

- 输出容器形式和 `--emit linked-bc` 一样
- 模块级中间产物仍然默认缓存到 `./lona_cache/`
- 当前重点在“managed 语义约束”和“运行时约定”
- 不是另一套完全独立的 bitcode 文件格式

当前已经稳定接通的能力包括：

- `lona-ir --emit mbc`
- managed 模式下的基础指针操作限制
- generic `#[extern "C"]` 托管运行时分配入口
- 分配点上的 `!lona.alloc.type` metadata

## 2. 当前指针限制

`mbc` 当前只额外限制两类指针操作。

### 2.1 禁止指针 cast

任何涉及 `T*` / `T[*]` 的 `cast[T](...)` 都会报错。

也就是说，下面这类写法会被拒绝：

```lona
var ptr u8* = null
var bits usize = cast[usize](ptr)
```

以及：

```lona
def f(items u8[*]) usize {
    ret cast[usize](items)
}
```

### 2.2 禁止对 `T[*]` 元素取地址

下面这类写法会被拒绝：

```lona
def first(items u8[*]) u8* {
    ret &items(0)
}
```

当前规则只禁止“对 indexable pointer 的元素取地址”，不禁止正常传递整个 pointer / indexable-pointer 值。

### 2.3 函数调用当前不做托管态传播

函数参数目前仍按普通 pointer 传参处理。

也就是说：

- 编译器当前不做托管态传播
- 不做额外的托管参数检查
- 托管运行时相关的解释交给 `lona-MVM`

## 3. 托管运行时分配入口

当前 `mbc` 路径允许通过 generic `#[extern "C"]` 导入声明托管运行时分配入口。

最小约定是：

```lona
#[extern "C"]
def __mvm_malloc[T]() T*

#[extern "C"]
def __mvm_array_malloc[T](element_count usize) T[*]
```

这里依赖的是“generic C 导入只保留一个 C 符号实例”的规则。

也就是说：

- `__mvm_malloc[T]` 的所有具体 `T` 共享同一个 `__mvm_malloc` C 符号
- `__mvm_array_malloc[T]` 的所有具体 `T` 共享同一个 `__mvm_array_malloc` C 符号
- 编译器只是在静态类型层面帮你恢复出 `T*` / `T[*]`
- 不会额外生成 `__inst__...` 之类的泛型实例符号

这里的当前约定是：

- `__mvm_malloc[T]` 不显式传入内存大小
- `lona-MVM` 根据 `alloc type` 完成对象分配
- `__mvm_array_malloc[T]` 只接收一个 `element_count` 参数

这条规则的主要用途，是让 managed 路径在不依赖显式指针 cast 的情况下，仍然能把分配结果恢复成静态类型指针，并把具体对象 / 元素类型交给运行时处理。

## 4. 分配点 Metadata

在 `mbc` 下，编译器会识别对 `__mvm_malloc[T]` / `__mvm_array_malloc[T]` 的 direct call，并在对应 LLVM `call` 指令上附加：

- `!lona.alloc.type`

metadata 内容是 concrete type 的 canonical 名称字符串。

例如：

```llvm
%obj = call ptr @__mvm_malloc(), !lona.alloc.type !0
%arr = call ptr @__mvm_array_malloc(i64 %count), !lona.alloc.type !1
!0 = !{!"pkg.Foo"}
!1 = !{!"pkg.Foo"}
```

当前约定是：

- metadata 只记录具体类型名
- 不引入隐藏 type-id 参数
- 不引入额外 RTTI 对象
- `__mvm_array_malloc` 不额外记录“数组态”标签

也就是说，数组对象这个事实由运行时根据调用目标 `__mvm_array_malloc` 本身识别，而不是通过 metadata 再编码一遍。

## 5. 编译器与 `lona-MVM` 的分工

当前分工是明确收口的：

- 编译器负责：
  - managed 构建入口
  - 基础指针限制
  - 把 concrete type 名贴到分配点上
- `lona-MVM` 负责：
  - 对象头生成
  - 数组对象识别
  - 类型描述生成
  - 后续 GC 所需运行时语义
  - 对 managed IR 的运行时侧注入和改造

因此，当前编译器不会：

- 传隐藏 type-id 参数
- 生成正式 RTTI 表
- 负责对象头布局
- 在 metadata 里编码完整 GC 描述

## 6. 由 `lona-MVM` 负责的部分

下面这些部分由 `lona-MVM` 在 runtime 侧继续完成：

- 对 IR 的运行时侧注入与改造
- 对象头与数组对象的具体布局
- 类型描述生成
- 完整 GC root / safepoint 协议
- 反射、backtrace 等运行时能力
- 更完整的 managed 运行时语义

当前编译器侧的目标是提供“managed 模式最小功能”，把必要的约束和分配点类型信息交给 `lona-MVM`。

这份文档只描述“编译器现在已经提供的最小 managed 构建语义”，不是长期设计总览。长期目标模式分层见 [../../internals/runtime/target_modes.md](../../internals/runtime/target_modes.md)。
