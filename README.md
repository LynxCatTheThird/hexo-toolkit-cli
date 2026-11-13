# Hexo Tool CLI

Linux 上的 Hexo 博客工具，提供构建、预览、部署、依赖更新和主题更新。

## 使用

```bash
./HexoTool <命令> [选项] [-- Hexo 参数]
```

| 命令 | 缩写 | 行为 |
| --- | --- | --- |
| `build` | `b`、`bui` | clean → generate → 附属工具 |
| `deploy` | `d`、`dep` | clean → generate → 附属工具 → deploy → clean |
| `clean` | `c` | 清理缓存和生成文件 |
| `server` | `s`、`srv` | 启动预览，实时显示 Hexo 输出 |
| `packages` | `p`、`pkg` | 升级依赖及版本声明，可能包含主版本升级 |
| `theme` | `t` | git submodule update --remote --merge |
| `help` | `h` | 显示帮助 |

命令支持唯一前缀；拼写错误时返回错误并提示相似命令。

```bash
./HexoTool build --project ~/blog
./HexoTool deploy --dry-run --project ~/blog
./HexoTool deploy -- --config staging.yml
./HexoTool server -- --port 5000 --host 0.0.0.0 --draft
./HexoTool build --help
```

预览默认从 4000–4100 选择可绑定的 IPv4 端口，也可通过 `--port` / `-p` 指定。Hexo 输出启动状态和访问地址。预览保留生成文件，按需执行 `clean` 清理。Ctrl+C 停止预览；交互终端支持 Ctrl+Z 暂停和 `fg` / `bg` 恢复。

## 工作流与参数

工具检查项目目录、工具配置、包管理器和附属工具的依赖声明。部署直接调用项目的 `hexo deploy`，部署配置、插件加载及多个部署器的执行顺序由 Hexo 负责。

各步骤按退出码推进：非零时停止流程并传递失败码；部署返回 0 后执行最终清理，返回非零时保留产物；最终清理失败也返回失败码。

Hexo 对空部署配置或缺失的部署插件可能返回 0，这些情况同样会进入最终清理。

已知问题：构建或部署的子进程捕获中断并返回 0 时，后续步骤仍会执行。

`--project` 指定项目目录，目录中须有 `_config.yml` 和 `package.json`。`--` 后可指定一次 `--cwd <目录>` 或 `--cwd=<目录>`，相对路径以 `--project` 为基准，缺省时以启动目录为基准。工具将目标目录统一用于项目检测和全部子命令。

构建与部署按以下规则分配其余 Hexo 参数：

| 执行步骤 | 接收的参数 |
| --- | --- |
| generate、deploy | 全部透传参数 |
| 流程中的 clean、以 `hexo` 开头的附属命令 | `--config`、`--safe` |
| 其他附属命令 | 配置中的自身参数 |

`--config` 支持空格形式、`--config=文件` 和逗号分隔的多配置文件。

build/deploy 使用固定流程，会拒绝 `--deploy` / `-d`、`--generate` / `-g`、`--watch` / `-w` 和透传的 `--help` / `-h`。帮助使用 `HexoTool build --help`。短选项分别书写，例如 `-- -b -f -c 4`；watch 流程可直接运行 Hexo。

## 选项与日志

| 选项 | 说明 |
| --- | --- |
| `--project <路径>` | 指定 Hexo 项目目录 |
| `--dry-run` | 仅预览执行计划 |
| `--quiet` | 工具仅显示警告、错误；非交互子进程仅显示失败输出尾部 |
| `--no-color` | 使用纯文本工具日志 |
| `--log-file <路径>` | 追加保存子进程 stdout/stderr |
| `-v` / `--verbose` | 显示项目、命令和子进程实时输出 |
| `-vv` / `--trace` | 显示内部诊断 |
| `--version` | 显示版本 |
| `-h` / `--help` | 显示帮助，可放在命令前或后 |

相对日志路径以启动目录为基准。日志打开失败时停止操作，运行中写入失败会报告错误。帮助与版本写入 stdout，状态、子进程输出和错误写入 stderr。

交互终端始终实时显示子进程输出，便于回答输入提示。非交互构建默认仅在失败时显示约 8 KB 的输出尾部；deploy、server、packages 和 theme 默认实时显示输出。

子进程继承输入和环境变量。`HEXO_TOOLKIT_DEBUG`、`HEXO_TOOLKIT_TRACE` 可用于开启诊断日志。

## 包管理器

优先采用 `package.json` 的 `packageManager` 声明，其次按 package-lock.json / npm-shrinkwrap.json、pnpm-lock.yaml、yarn.lock 识别，缺省使用 npm。多个包管理器的锁文件同时存在时，须明确声明 `packageManager`。

npm 通过 `npx --no-install` 执行 Hexo，运行前请安装项目依赖。packages 命令使用 npm-check-updates 更新版本声明并安装依赖。Yarn 1 使用 `yarn upgrade --latest`，新版 Yarn 使用 `yarn up "*" "@*/*"`；版本按声明识别，缺省时依据 yarn.lock 的 metadata 判断。

## 配置

工具读取可执行文件同目录的 `config.yaml`；目录定位失败时改用当前工作目录。配置解析失败时停止操作，文件缺省时使用下列默认值：

```yaml
similarityThreshold: 0.85
dependenciesSearchingFile: "package.json"
additionalTools:
  - ["hexo-swpp", "hexo swpp"]
  - ["hexo-algolia", "hexo algolia"]
```

- `similarityThreshold`：相似命令提示的阈值，范围 `(0, 1]`。
- `dependenciesSearchingFile`：附属工具的依赖来源，支持 dependencies、devDependencies、optionalDependencies、peerDependencies 四类字段。
- `additionalTools`：构建后执行的 `[包名, 命令]` 列表。包名在依赖来源中直接声明时执行对应命令；`[]` 禁用附属工具。命令通过选定的包管理器执行，请填写可信命令。

## 编译与测试

需要 CMake 3.24+、Git 和支持 C++20 / std::format 的编译器，测试需要 Python 3。CMake 获取固定版本的 spdlog、rapidyaml，并自动使用已安装的 ccache。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

产物为 `build/HexoTool`。`-DBUILD_TESTING=OFF` 可关闭测试；优化级别由 CMake 的构建类型控制。

安装 clang-tidy 后可运行静态检查：

```bash
clang-tidy -p build main.cpp tests/core_tests.cpp
```

默认测试使用本地模拟命令。真实 Hexo 集成测试使用本地部署器，启用方式如下：

```bash
npm ci --ignore-scripts --no-audit --no-fund --prefix tests/fixtures/real-hexo
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHEXO_INTEGRATION_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

测试覆盖工作流、产物清理、配置与参数透传、项目切换、端口选择和终端交互。CI 在 Linux 上运行这些测试。

## License

[SSPLv1](LICENSE)
