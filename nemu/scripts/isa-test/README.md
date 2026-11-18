# NEMU RV32 指令行为回归测试

这个测试用于在实现指令时快速检查具体输入下的译码和执行结果，适合辅助红绿重构：先添加有明确预期的用例，确认当前实现失败，再实现指令并重新运行。它不是严格隔离的 C 单元测试：每个用例都通过 NEMU 取指、译码和执行，并依赖监视器观察状态。`cpu-tests` 本身也包含结果断言，只是本工具更适合单独定位具体场景。

## 运行

需要 Python 3、`riscv64-linux-gnu-{as,ld,objcopy,objdump}` 和已构建的 `build/riscv32-nemu-interpreter`。在 `nemu` 目录下：

```bash
make NEMU_HOME="$PWD"  # 修改 NEMU 后先重新编译
python3 scripts/isa-test/run_isa_test.py
python3 scripts/isa-test/run_isa_test.py -j8  # 最多同时运行 8 个用例
python3 scripts/isa-test/run_isa_test.py -j1  # 串行运行
python3 scripts/isa-test/run_isa_test.py --only sra
python3 scripts/isa-test/run_isa_test.py --only lui
python3 scripts/isa-test/run_isa_test.py --only auipc
python3 scripts/isa-test/run_isa_test.py --only load
python3 scripts/isa-test/run_isa_test.py --only store
python3 scripts/isa-test/run_isa_test.py --group mul  # M 扩展的乘法（68 个）
python3 scripts/isa-test/run_isa_test.py --group div  # M 扩展的除法与取余（66 个）
python3 scripts/isa-test/run_isa_test.py --list
python3 scripts/isa-test/run_isa_test.py --selftest
python3 scripts/isa-test/run_isa_test.py --keep
```

`--nemu` 和 `--prefix` 可指定可执行文件和交叉工具链前缀。退出码 0 表示全部通过，1 表示有用例失败或运行出错，2 表示环境不完整或没有匹配用例。`--keep` 会打印临时产物目录；默认自动清理。若 `inst.c` 比二进制新会给出警告，但其他源码或配置变化也可能要求重新编译。

默认并发运行 8 个用例，可用 `-j N` 或 `--jobs N` 调整。每个用例独立编译并启动 NEMU，输出按完成顺序显示，编号对应用例原始顺序。每行的时间包含该用例的编译、执行和检查；最后的总耗时是实际等待时间，平均耗时是各用例耗时的平均值，并发时两者不能直接相乘比较。并发数过高会增加内存占用。

## 如何判断

每个场景汇编成独立的裸机程序，交叉工具链生成机器码；脚本使用 objdump 核对 `tested` 标签处的指令和操作数。NEMU 执行准备代码后，脚本用 `info r` 检查输入寄存器和 PC；然后单步执行被测指令，在宿主机比较目标寄存器、其余寄存器、x0 和 PC。错误会显示实际值和期望值，不依赖被测程序执行减法或分支来判定结果。大常数通过 `la` 和 `lw` 装载，准备过程依赖这些指令和 NEMU 监视器的正确性；准备失败会报告错误。

访存用例使用单独初始化的 16 字节测试区，监视器 `x` 在执行前后读取整个区域；宿主机检查初始值以及执行后的每个字节，包括未被写入的邻近字节。监视器读取也依赖 NEMU 的内存读接口，因此不是完全独立的内存实现。

`--selftest` 在宿主机逐一篡改执行后的每个寄存器、PC 和访存用例中的每个内存字节，检查比较器能否检测到变化。它验证比较器的失败路径，不能证明指令测试覆盖充分或参考模型正确。期望值来自脚本中的 Python 32 位模型或明确写出的常量；增加用例时应对照 RISC-V 规范独立检查边界值。

## 添加用例

在 `CASES` 区添加一行，例如：

```python
case("shift", "srl 位移 7", "srl t0, t0, t1",
     [("t0", 0x12345678), ("t1", 7)], m_srl(0x12345678, 7))
```

`case` 适用于第一操作数是目标寄存器、执行后 PC 顺序加 4 且不修改内存的指令。`mem_case` 适用于已有测试区内的 load/store，`t4` 指向测试区第 8 字节；存储期望值可用 `stored(字节偏移, 宽度, 值)` 创建。可设置不同的目标寄存器、源寄存器重合和 x0 写入。用例输入、目标寄存器、期望值应互相核对；观察到准备阶段失败时，先检查装载依赖和监视器，再判断被测指令。

目前包含 217 个场景，覆盖部分整数算术、逻辑、比较、移位、M 扩展乘除法、访存和 U 型指令。其中 `lui` 和 `auipc` 各有 7 个场景，覆盖立即数边界、最高位、不同目标寄存器和 x0；`auipc` 的预期结果由被测指令的地址加上 U 型立即数，按 32 位回绕计算。访存用例覆盖符号扩展、正负偏移、截断、字节序和相邻内存不变。

### M 扩展（mul / mulh / mulhu / mulhsu / div / divu / rem / remu）

共 134 个场景，分两组：`--group mul`（68 个）和 `--group div`（66 个）。可用 `--group` 按分组筛选，`--only` 则按名称子串筛选。

**乘法。** `mul` 只保留低 32 位、与符号无关，所以重点选“高位会被丢掉”的输入：`2^30 * 4`、`2^16 * 2^16`、`INT_MIN * 2`、`INT_MAX * INT_MAX`、`INT_MIN * (-1)` 等，共 17 对操作数。

`mulh` / `mulhu` / `mulhsu` 使用**同一组 16 对操作数**，三条指令的期望值分别由模型计算，便于横向对照“谁做了符号扩展”：

- `0xffffffff * 0xffffffff` 区分 `mulh`（有符号 1，高位 0）与 `mulhu`（无符号，高位 `0xfffffffe`）；
- `0x80000000 * 0x80000000` 区分 `mulhsu`（高位 `0xc0000000`）与前两者（高位 `0x40000000`）；
- `0x80000000 * 0x7fffffff` 与互换 rs1/rs2 的一对，覆盖符号位不同且不满足交换对称的情形。

**除法与取余。** 16 对操作数同时喂给 `div` / `divu` / `rem` / `remu`，覆盖规范里的三类特例：

- 除零 4 组（正数、负数、`INT_MIN`、`0/0`）：`div` 与 `divu` 返回全 1，`rem` 与 `remu` 返回被除数；
- `INT_MIN / -1`：唯一的有符号溢出，商为 `INT_MIN`、余数为 0；
- 向零截断：`7 / -2`、`-7 / 2`、`-7 / -2` 等组合，检验余数符号跟随被除数。

Python 的 `//` 和 `%` 是向下取整（`-7 // 2 == -4`），与 C/RISC-V 的向零截断不同，因此模型里用 `_trunc_div` 单独实现商，再用 `x - q*y` 反推余数。

另有 5 个寄存器字段用例：写入 x0、`rd` 与 `rs2` 重合、`rd` 不是 `t0`。

**排错提示。** 若 `divu` / `rem` / `remu` 的用例全部报 `ERROR`（NEMU 异常退出），而 `div` 正常，通常是 `INSTPAT` 的 funct3 写错：`mul=000`、`mulh=001`、`mulhsu=010`、`mulhu=011`、`div=100`、`divu=101`、`rem=110`、`remu=111`。模式与 `mul` 重复时后续几条永远不会被匹配到。


这些用例不能证明所有输入和译码组合正确，也尚未测试非对齐访问、分支与跳转、异常、非法编码、CSR 等；这些需要扩展生成器和状态比较。特别是不能只用结果相同的输入判断两条指令是否被正确区分。随着实验进度，继续添加能区分相邻编码及边界条件的用例，并结合 cpu-tests 和 difftest 使用。
