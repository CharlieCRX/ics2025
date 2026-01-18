# Test entry for NEMU

# 使用与 nemu 相同的 config / 编译环境
include $(NEMU_HOME)/include/config/auto.conf
include $(NEMU_HOME)/include/config/auto.conf.cmd

# 给测试一个独立的名字
NAME := test

# 测试用源文件
SRCS := \
  src/monitor/sdb/watchpoint.c \
  tests/test_watchpoint.c

# 不需要 difftest / am / run 逻辑
include $(NEMU_HOME)/scripts/build.mk

# 运行测试
test: app
	@echo "Running tests..."
	@$(BINARY)

.PHONY: test
