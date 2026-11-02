# Hexo Tool CLI

用于 Hexo 博客项目的构建、预览、部署、依赖更新和主题更新工具。

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
| `packages` | `p`、`pkg` | 升级依赖及其版本声明，可能包含主版本升级 |
| `theme` | `t` | git submodule update --remote --merge |
| `help` | `h` | 帮助 |

命令接受唯一前缀；拼写错误只提示候选，不会自动执行。任一步失败即停止并返回失败码。部署前使用项目自身的 Hexo 加载配置并检查所有部署类型及实际注册的插件；未配置部署器、插件缺失或部署失败时返回失败码，保留生成文件供排查。多个部署器须全部通过检查才开始部署。

```bash
./HexoTool build --project ~/blog
./HexoTool deploy --dry-run --project ~/blog
./HexoTool deploy -- --config staging.yml
./HexoTool server -- --port 5000 --host 0.0.0.0 --draft
./HexoTool build --help
```

未指定 server 端口时，从 4000–4100 中选择可绑定的 IPv4 端口；指定 `--port` / `-p` 时直接交给 Hexo。实际启动状态和地址由 Hexo 输出，不会根据其他进程的监听状态宣布启动成功。server 不再自动清理生成文件；需要时可先执行 clean。Ctrl+C 停止预览并正常退出。POSIX 交互终端支持 Ctrl+Z 暂停，以及 shell 的 `fg` / `bg` 恢复。

构建与部署会把 `--` 后的参数传给 clean、generate 和 deploy。以 `hexo` 开头的附属命令同时继承 `--config`、`--cwd`、`--safe`，包括 `--config=文件` 和逗号分隔的多配置文件；其他附属命令保留自身参数。Windows 透传参数不能包含 cmd.exe 会再次解释的字符：`"`、`%`、`&`、`|`、`<`、`>`、`(`、`)`、`^`、`!` 或换行。

## 选项与日志

| 选项 | 说明 |
| --- | --- |
| `--project <路径>` | Hexo 项目目录，要求存在 _config.yml 和 package.json |
| `--dry-run` | 仅显示执行计划，不运行命令、不创建日志 |
| `--quiet` | 隐藏工具状态信息；非交互运行只显示警告、错误及失败输出尾部 |
| `--no-color` | 禁用工具自身的彩色输出 |
| `--log-file <路径>` | 追加保存所有命令的 stdout/stderr，包括 server |
| `-v` / `--verbose` | 显示项目、执行命令和子进程实时输出 |
| `-vv` / `--trace` | 额外显示内部诊断 |
| `--version` | 版本 |
| `-h` / `--help` | 帮助，可放在命令前或后 |

相对日志路径以启动工具时的目录为准，不随 --project 改变。日志无法打开时不会执行外部命令；运行中写日志失败会报告错误。stdin 连接终端时始终实时显示子进程输出，包括 `--quiet`，以便看到输入提示。非交互运行默认隐藏构建的成功输出，失败显示最多约 8 KB 的尾部；deploy、server、packages 和 theme 默认实时显示输出。帮助与版本写入 stdout，状态、子进程输出和错误写入 stderr。

子进程继承输入和环境变量；工具不会强制设置 CI 模式。可以继续使用 `HEXO_TOOLKIT_DEBUG`、`HEXO_TOOLKIT_TRACE` 环境变量打开诊断。

## 包管理器

优先读取 package.json 的 `packageManager`，支持 npm、pnpm 和 Yarn；未声明时根据 package-lock.json / npm-shrinkwrap.json、pnpm-lock.yaml、yarn.lock 识别，无 lock 文件默认 npm。存在不同包管理器的 lock 文件且没有明确声明时，工具会报错。

npm 执行 Hexo 时使用 `npx --no-install`，请先安装项目依赖。仅 packages 命令会显式调用 npm-check-updates 更新版本声明并安装。Yarn 1 使用 `yarn upgrade --latest`，新版 Yarn 使用 `yarn up "*" "@*/*"`；优先按声明版本区分，否则根据 yarn.lock 的 metadata 识别。

## 配置

可在**可执行文件同目录**创建 config.yaml；不存在则使用默认值。配置格式或字段值错误时停止操作，不会静默改用默认配置继续部署。

```yaml
similarityThreshold: 0.85
dependenciesSearchingFile: "package.json"
additionalTools:
  - ["hexo-swpp", "hexo swpp"]
  - ["hexo-algolia", "hexo algolia"]
```

- `similarityThreshold`：候选命令提示的最低相似度，范围 `(0, 1]`；不影响命令是否执行。
- `dependenciesSearchingFile`：用于插件检测的 manifest，默认 package.json。自定义文件也须使用 dependencies / devDependencies / optionalDependencies / peerDependencies 字段；不再匹配 lock 文件里的文本。
- `additionalTools`：构建后执行的 `[包名, 命令]` 列表，包名须在 manifest 中直接声明。命令通过选定的包管理器执行；空列表 `[]` 禁用全部附属工具。这里的命令属于可执行配置，仅填写可信命令。

旧的 `serverStartupTimeoutSeconds` 已停用，保留该字段会提示移除。服务器由 Hexo 自身报告状态，不再通过端口轮询判定启动超时。

## 编译与测试

需要 CMake 3.24+、Git 和支持完整 C++20 / std::format 的编译器。测试还需要 Python 3。CMake 获取固定版本的 spdlog 和 rapidyaml；可选 ccache 会自动用于编译缓存。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

产物为 build/HexoTool；Windows 多配置生成器通常位于 build/Release/HexoTool.exe。不运行测试时可加 `-DBUILD_TESTING=OFF`，无需 Python。Debug、Release 和 MinSizeRel 的优化级别由 CMake 管理；不再提供 PGO、native arch、强制 linker、Unity Build、PCH 或自动 LTO 配置。

项目自身的可执行文件与测试启用编译器警告，包括 GCC/Clang 的转换、符号和遮蔽检查。安装 clang-tidy 后可使用仓库内的检查配置：

```bash
clang-tidy -p build main.cpp tests/core_tests.cpp
```

默认测试使用本地模拟命令验证流程，不会安装 Node 依赖或执行真实部署。还可以启用真实 Hexo 集成测试：

```bash
npm ci --ignore-scripts --no-audit --no-fund --prefix tests/fixtures/real-hexo
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHEXO_INTEGRATION_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

集成测试使用固定版本 Hexo 及临时项目，部署器仅写本地测试文件；覆盖缺失或无效部署配置、多个部署器、部署异常和多配置传递。CI 在 Linux 和 Windows 上启用这些测试。

## License

[SSPLv1](LICENSE)
