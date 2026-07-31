# Hexo Tool CLI

一个用于 [Hexo](https://hexo.io) 博客项目的命令行工具，提供构建、预览、清理、依赖更新和主题更新等命令。命令支持前缀匹配和相似度匹配。

> [!NOTE]
> 这是作者的一个小练手项目，可能存在一些 Bug，请谨慎用于生产环境。

## 功能特性

| 功能             | 说明                                                                           |
| ---------------- | ------------------------------------------------------------------------------ |
| **部署流程**     | `deploy` 依次执行 `clean → generate → 插件 → deploy → clean`，任一步失败即停止 |
| **本地预览**     | 从 4000 到 4100 查找可用端口，然后启动 `hexo server`                         |
| **依赖更新**     | 自动识别 npm / yarn / pnpm，执行对应的升级命令                                 |
| **主题更新**     | 通过 `git submodule update --remote --merge` 拉取最新主题                      |
| **命令匹配**     | 基于 Jaro-Winkler 算法，支持 `bui`、`dep` 等前缀或相似输入                   |
| **插件扩展**     | 按需执行 `hexo-swpp`、`hexo-algolia` 等附属工具                                |

## 命令参考

```
./HexoTool <command>
```

| 命令       | 别名示例           | 说明                           |
| ---------- | ------------------ | ------------------------------ |
| `deploy`   | `d`、`dep`         | 生成并部署静态文件             |
| `build`    | `b`、`bui`         | 仅生成本地静态文件，不执行部署 |
| `clean`    | `c`、`cle`         | 清理 Hexo 缓存和生成文件       |
| `server`   | `s`、`ser`、`srv`  | 启动本地预览服务器             |
| `packages` | `p`、`pkg`、`pack` | 更新 Node.js 依赖包            |
| `theme`    | `t`、`the`         | 通过 Git 子模块更新主题        |
| `help`     | `h`                | 显示帮助信息                   |

> [!TIP]
> 命令输入是目标命令的**前缀**，或与目标命令的相似度达到阈值（默认 `0.85`）时，程序会接受该输入。

### 通用选项

指定 Hexo 项目目录：

```bash
./HexoTool deploy --project /path/to/blog
```

只预览执行计划，不运行任何外部命令：

```bash
./HexoTool deploy --dry-run
```

使用 `--` 将后续参数传给 Hexo。构建与部署流程会将这些参数传给 `clean`、`generate` 和 `deploy`：

```bash
./HexoTool deploy -- --config staging.yml
./HexoTool server -- --draft --host 0.0.0.0
```

在 Windows 上，透传参数不能包含 `cmd.exe` 会二次解释的字符：`"`、`%`、`&`、`|`、`<`、`>`、`(`、`)`、`^`、`!`。这可避免参数被当作额外命令执行。

其他通用选项包括 `--quiet`、`--no-color`、`--log-file <路径>` 和 `--version`。`--log-file` 会追加保存构建、部署、清理、主题更新和依赖更新命令的 stdout/stderr；持续运行的 `server` 进程暂不写入该文件。命令失败时，终端显示退出码和输出尾部；如果输出包含常见错误关键词，还会附带一条可能原因提示。`-v` / `--verbose` 显示项目路径、配置和实际执行的命令；`-vv` / `--trace` 还会显示命令匹配、文件探测和计时信息。默认模式会隐藏外部命令的正常输出。帮助与版本信息输出到 stdout，运行状态和错误输出到 stderr。执行项目命令前，工具会检查目标目录中是否同时存在 `_config.yml` 与 `package.json`。

## 编译

### 基础依赖

您至少需要安装完整的 C++ 编译工具链、CMake 构建系统以及 Git。本项目需要包含完整 `std::format` 支持的 **C++20** 工具链（建议 GCC 13+、Clang 17+ 或最新 MSVC 2022）。

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

运行测试：

```bash
ctest --test-dir build --output-on-failure
```

<details>
<summary><b>可选：启用 PGO 优化</b></summary>
<br>

PGO（Profile-Guided Optimization）使用运行数据进行编译器优化。是否有收益取决于实际工作负载。

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
serverStartupTimeoutSeconds: 30
dependenciesSearchingFile: "package.json"
additionalTools:
  - ["hexo-swpp", "hexo swpp"]
  - ["hexo-algolia", "hexo algolia"]
```

### 参数说明

**`similarityThreshold`** _(Float, 默认 `0.85`)_

命令模糊匹配的相似度阈值，取值范围 `(0.0, 1.0]`。数值越高，匹配越严格。

**`dependenciesSearchingFile`** _(String, 默认自动检测)_

用于检测附属插件是否已安装的文件（程序通过搜索该文件内容来判断插件是否存在）。默认情况下，程序会根据当前目录下的 lock 文件自动识别包管理器（npm / yarn / pnpm）并设置对应路径。手动指定此项后，自动检测将被禁用。

**`serverStartupTimeoutSeconds`** _(Int, 默认 `30`)_

`server` 启动阶段的最长等待时间。若超过该时间仍未监听到端口，程序会返回错误，避免一直卡在启动等待阶段。

**`additionalTools`** _(List[List], 默认内置两个插件)_

在 `build` / `deploy` 流程中，`hexo generate` 完成后额外执行的命令。格式为 `["npm 包名", "实际命令"]`，程序会先检查 `dependenciesSearchingFile` 中是否包含该包名，存在才执行。设置为空列表可禁用所有插件：

```yaml
additionalTools: []
```

> [!WARNING]
> `additionalTools` 中的命令将以当前用户权限通过子进程直接执行，请勿填入不受信任的内容。任一附属工具执行失败都会中止部署。

## License

本项目以 [SSPLv1](LICENSE) 协议开源。
