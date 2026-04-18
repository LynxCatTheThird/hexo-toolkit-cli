# Hexo Tool CLI

> [!WARNING]
> 请确保您的编译器支持 C++20 标准（如 GCC 11+、Clang 14+ 或 MSVC 2022）。
>
> 这是作者的一个小练手，可能会有比较多的 Bug，请谨慎用于生产环境。

## 编译本项目

### 基础依赖

您至少安装完整的 C++ 编译工具链、CMake 构建系统以及 Git：

- **Arch Linux**:
  ```bash
  sudo pacman -S base-devel cmake git
  ```
- **Ubuntu / Debian**:
  ```bash
  sudo apt update
  sudo apt install build-essential cmake git
  ```

### 性能增强组件

为了更好的体验，建议安装以下增强工具集。安装后，CMake 脚本将自动识别并启用它们：

- `ninja`: 更快的构建后端引擎
- `ccache`: C++ 编译缓存工具
- `mold`: 更快的链接器
- `upx`: 二进制产物压缩工具

- **Arch Linux**:
  ```bash
  sudo pacman -S ninja ccache mold upx
  ```
- **Ubuntu / Debian**:
  ```bash
  sudo apt install ninja-build ccache mold upx-ucl
  ```

### 编译

进入项目根目录后，推荐使用 Ninja 生成器进行 Release 模式构建：

```bash
# 生成构建系统配置
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 执行多核并行编译
cmake --build build -j
```

<details>
<summary><b>可选：启用 PGO 优化</b></summary>
<br>
如果您追求极致的运行效率，可以通过 PGO (配置引导优化) 进行构建。

```bash
# 编译插桩版本
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_PGO_GEN=ON
cmake --build build -j

# 运行程序，提供真实负载以收集性能数据
./build/HexoTool b

# 使用收集到的数据重新进行优化编译
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_PGO_GEN=OFF -DENABLE_PGO_USE=ON
cmake --build build -j
```

</details>

## 配置文件说明

编译完成后，可在可执行文件同目录下创建 `config.yaml` 文件，以自定义工具行为。

```yaml
similarityThreshold: 0.85
dependenciesSearchingFile: "package.json"
additionalTools:
  - ["swpp", "hexo swpp"]
  - ["algolia", "hexo algolia"]
  - ["gulp", "gulp zip"]
```

### 参数详解

- **`similarityThreshold`** _(Float)_<br>字符串匹配或模糊搜索的相似度阈值。取值范围 `0.0 ~ 1.0`。数值越高，匹配要求越严格；默认设定为 `0.85`。
- **`dependenciesSearchingFile`** _(String)_<br>用于定位工作区根目录或检查依赖项的标志性文件。通常设为 Node.js 项目的 `package.json`。如果缺省，程序会自动检索 lock 文件以确保万无一失，但这可能会带来额外的轻微性能下降。
- **`additionalTools`** _(List[List])_<br>用于声明额外的 Hexo 插件或自定义 Shell 命令。格式为 `["指令别名", "实际执行的命令"]`。工具解析到对应别名时，将自动调用后方的系统命令。您可以设定为空以不执行任何插件命令。
