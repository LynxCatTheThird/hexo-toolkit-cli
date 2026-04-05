# 自动检测 CPU 核心数支持并行编译 (兼容 Linux 和 macOS)
NPROCS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)
MAKEFLAGS += -j$(NPROCS)

# 编译器与核心参数 (?= 允许外部环境变量覆盖)
CXX      ?= g++
CXXFLAGS ?= -march=native -O2 -Wall -Wextra -Wpedantic -std=c++26

# 预处理器参数 (包含目录、自动生成头文件依赖)
CPPFLAGS := -I_includes -MMD -MP

# 链接器参数 (依赖多线程库)
LDFLAGS  := 
LDLIBS   := -pthread

# 源文件与目标文件
SRCS := main.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)
BIN  := t

# 安装路径前缀 (按标准使用 PREFIX 和 DESTDIR，方便打包和分发)
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

# 声明伪目标
.PHONY: all clean install uninstall

# 默认目标
all: $(BIN)

# 链接可执行文件
$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# 编译 C++ 源码为中间对象文件
%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# 安装目标
install: $(BIN)
	@mkdir -p $(DESTDIR)$(BINDIR)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	@echo "安装成功: $(DESTDIR)$(BINDIR)/$(BIN)"

# 卸载目标
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	@echo "已卸载: $(DESTDIR)$(BINDIR)/$(BIN)"

# 清理构建产物
clean:
	rm -f $(BIN) $(OBJS) $(DEPS)

# 引入自动生成的依赖文件 (包含头文件更改追踪)
-include $(DEPS)