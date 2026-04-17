# Hexo 工具

### 编译使用

```bash
make all && make install
```

> [!WARNING]
> 请使用支持 C++20 的编译器，低版本编译器未经测试

### 配置文件

编译后可执行文件同目录下的 `config.yaml`

```yaml
similarityThreshold: 0.85
dependenciesSearchingFile: "package.json"
additionalTools:
  - ["swpp", "hexo swpp"]
  - ["gulp", "gulp zip"]
  - ["algolia", "hexo algolia"]
```
