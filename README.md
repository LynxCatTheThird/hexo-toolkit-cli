# Hexo Tool CLI

一个为 [Hexo](https://hexo.io) 博客工作流设计的命令行辅助工具。它将高频操作（构建、预览、更新依赖等）整合为简短指令，并通过**模糊匹配**容错输入，无需精确记忆命令全名。

> [!NOTE]
> 这是作者的一个小练手项目，可能存在一些 Bug，请谨慎用于生产环境。

## 功能特性

| 功能             | 说明                                                         |
| ---------------- | ------------------------------------------------------------ |
| **一键部署**     | 自动完成 `clean → generate → 插件 → deploy → clean` 全流程   |
| **本地预览**     | 自动寻找可用端口，启动 `hexo server` 并实时显示状态          |
| **依赖更新**     | 自动识别 npm / yarn / pnpm，执行对应的升级命令               |
| **主题更新**     | 通过 `git submodule update --remote --merge` 拉取最新主题    |
| **模糊命令匹配** | 基于 Jaro-Winkler 算法，输入 `bui`、`dep` 等缩写均可识别意图 |
| **插件扩展**     | 按需执行 `hexo-swpp`、`algolia`、`gulp` 等附属工具           |

## 命令参考

```
./HexoTool <command>
```

| 命令               | 别名示例           | 说明                    |
| ------------------ | ------------------ | ----------------------- |
| `build` / `deploy` | `b`、`bui`、`dep`  | 构建并部署静态文件      |
| `server`           | `s`、`ser`、`srv`  | 启动本地预览服务器      |
| `packages`         | `p`、`pkg`、`pack` | 更新 Node.js 依赖包     |
| `theme`            | `t`、`the`         | 通过 Git 子模块更新主题 |
| `help`             | `h`                | 显示帮助信息            |

> [!TIP]
> 命令匹配具有一定容错性：只要输入内容是目标命令的**前缀**，或与其**相似度**达到阈值（默认 0.85），即可被正确识别。

## 编译

### 基础依赖

您至少需要安装完整的 C++ 编译工具链、CMake 构建系统以及 Git。本项目需要 **C++20** 支持（GCC 11+、Clang 14+ 或 MSVC 2022）。

- **Arch Linux**:
  ```bash
  sudo pacman -S base-devel cmake git
  ```
- **Ubuntu / Debian**:
  ```bash
  sudo apt update && sudo apt install build-essential cmake git
  ```

### 性能增强组件（可选）

安装后，CMake 脚本将自动识别并启用它们：

| 工具     | 作用                       |
| -------- | -------------------------- |
| `ninja`  | 更快的构建后端             |
| `ccache` | 编译结果缓存，加速二次构建 |
| `mold`   | 更快的链接器               |

- **Arch Linux**:
  ```bash
  sudo pacman -S ninja ccache mold
  ```
- **Ubuntu / Debian**:
  ```bash
  sudo apt install ninja-build ccache mold
  ```

### 构建

进入项目根目录后，推荐使用 Ninja 生成器进行 Release 构建：

```bash
# 生成构建系统配置
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 多核并行编译
cmake --build build -j
```

编译产物位于 `./build/HexoTool`。

<details>
<summary><b>可选：启用 PGO 优化</b></summary>
<br>

PGO（Profile-Guided Optimization）通过真实运行数据指导编译器优化，可进一步提升运行效率。

```bash
# 第一步：编译插桩版本
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_PGO_GEN=ON
cmake --build build -j

# 第二步：运行程序以收集性能数据（用真实场景操作）
./build/HexoTool build

# 第三步：使用收集到的数据重新优化编译
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_PGO_GEN=OFF -DENABLE_PGO_USE=ON
cmake --build build -j
```

</details>

## 配置文件

在**可执行文件同目录**下创建 `config.yaml`，可自定义工具行为。若文件不存在，程序将使用内置默认值。

```yaml
similarityThreshold: 0.85
dependenciesSearchingFile: "package.json"
additionalTools:
  - ["swpp", "hexo swpp"]
  - ["algolia", "hexo algolia"]
  - ["gulp", "gulp zip"]
```

### 参数说明

**`similarityThreshold`** _(Float, 默认 `0.85`)_

命令模糊匹配的相似度阈值，取值范围 `(0.0, 1.0]`。数值越高，匹配越严格。

**`dependenciesSearchingFile`** _(String, 默认自动检测)_

用于检测附属插件是否已安装的文件（程序通过搜索该文件内容来判断插件是否存在）。默认情况下，程序会根据当前目录下的 lock 文件自动识别包管理器（npm / yarn / pnpm）并设置对应路径。手动指定此项后，自动检测将被禁用。

**`additionalTools`** _(List[List], 默认内置三个插件)_

在 `build` / `deploy` 流程中，`hexo generate` 完成后额外执行的命令。格式为 `["关键字", "实际命令"]`，程序会先检查 `dependenciesSearchingFile` 中是否包含该关键字，存在才执行。设置为空列表可禁用所有插件：

```yaml
additionalTools: []
```

> [!WARNING]
> `additionalTools` 中的命令将以当前用户权限通过 `std::system()` 直接执行，请勿填入不受信任的内容。

## License

本项目以 [SSPLv1](LICENSE) 协议开源。
