#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""通过 NEMU 监视器观察单条指令执行前后的架构状态。"""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

MASK = 0xFFFFFFFF
NEMU_ROOT = Path(__file__).resolve().parents[2]


# ---------------------------------------------------------------- 32 位参考模型

def u32(v):
    return v & MASK


def s32(v):
    v &= MASK
    return v - (1 << 32) if v >> 31 else v


def m_sll(a, b):
    return u32(a) << (b & 31) & MASK


def m_srl(a, b):
    return u32(a) >> (b & 31)


def m_sra(a, b):
    return u32(s32(a) >> (b & 31))


def m_add(a, b):
    return u32(u32(a) + u32(b))


def m_sub(a, b):
    return u32(u32(a) - u32(b))


def m_and(a, b):
    return u32(a) & u32(b)


def m_or(a, b):
    return u32(a) | u32(b)


def m_xor(a, b):
    return u32(a) ^ u32(b)


def m_slt(a, b):
    return int(s32(a) < s32(b))


def m_sltu(a, b):
    return int(u32(a) < u32(b))


# ---------------------------------------------------------------- 用例表

CASES = []


def case(group, name, inst, setup, exp):
    """setup: [(寄存器, 值), ...]，在本条指令执行前装载；exp: 期望结果。"""
    CASES.append(dict(group=group, name=name, inst=inst,
                      setup=list(setup), exp=u32(exp)))


# --- 移位指令：移位量 0/31/32/33 覆盖「掩码」语义，负数覆盖算术右移 ---
case("shift", "sll 1<<1=2", "sll t0, t0, t1", [("t0", 1), ("t1", 1)], m_sll(1, 1))
case("shift", "sll 1<<31=0x80000000", "sll t0, t0, t1", [("t0", 1), ("t1", 31)], m_sll(1, 31))
case("shift", "sll 1<<32=1 (掩码)", "sll t0, t0, t1", [("t0", 1), ("t1", 32)], m_sll(1, 32))
case("shift", "sll 1<<33=2 (掩码)", "sll t0, t0, t1", [("t0", 1), ("t1", 33)], m_sll(1, 33))
case("shift", "sll 3<<30=0xc0000000", "sll t0, t0, t1", [("t0", 3), ("t1", 30)], m_sll(3, 30))
case("shift", "sll -1<<4", "sll t0, t0, t1", [("t0", 0xFFFFFFFF), ("t1", 4)], m_sll(0xFFFFFFFF, 4))

case("shift", "srl -16>>28=15", "srl t0, t0, t1", [("t0", -16), ("t1", 28)], m_srl(-16, 28))
case("shift", "srl -1>>1=0x7fffffff", "srl t0, t0, t1", [("t0", -1), ("t1", 1)], m_srl(-1, 1))
case("shift", "srl -1>>31=1", "srl t0, t0, t1", [("t0", -1), ("t1", 31)], m_srl(-1, 31))
case("shift", "srl INT_MIN>>31=1", "srl t0, t0, t1", [("t0", 0x80000000), ("t1", 31)], m_srl(0x80000000, 31))
case("shift", "srl 位移0", "srl t0, t0, t1", [("t0", -16), ("t1", 0)], m_srl(-16, 0))
case("shift", "srl 位移32 (掩码)", "srl t0, t0, t1", [("t0", -16), ("t1", 32)], m_srl(-16, 32))

case("shift", "sra -16>>28=-1", "sra t0, t0, t1", [("t0", -16), ("t1", 28)], m_sra(-16, 28))
case("shift", "sra -1>>1=-1", "sra t0, t0, t1", [("t0", -1), ("t1", 1)], m_sra(-1, 1))
case("shift", "sra INT_MIN>>31=-1", "sra t0, t0, t1", [("t0", 0x80000000), ("t1", 31)], m_sra(0x80000000, 31))
case("shift", "sra 3>>30=0", "sra t0, t0, t1", [("t0", 3), ("t1", 30)], m_sra(3, 30))
case("shift", "sra 位移33 (掩码)", "sra t0, t0, t1", [("t0", -16), ("t1", 33)], m_sra(-16, 33))

case("shift", "slli 1<<31", "slli t0, t0, 31", [("t0", 1)], m_sll(1, 31))
case("shift", "slli 3<<30", "slli t0, t0, 30", [("t0", 3)], m_sll(3, 30))
case("shift", "slli 位移0", "slli t0, t0, 0", [("t0", -16)], m_sll(-16, 0))
case("shift", "srli -16>>28=15", "srli t0, t0, 28", [("t0", -16)], m_srl(-16, 28))
case("shift", "srli -1>>31=1", "srli t0, t0, 31", [("t0", -1)], m_srl(-1, 31))
case("shift", "srli INT_MIN>>31=1", "srli t0, t0, 31", [("t0", 0x80000000)], m_srl(0x80000000, 31))
case("shift", "srai -16>>28=-1", "srai t0, t0, 28", [("t0", -16)], m_sra(-16, 28))
case("shift", "srai INT_MIN>>31=-1", "srai t0, t0, 31", [("t0", 0x80000000)], m_sra(0x80000000, 31))
case("shift", "srai 位移0", "srai t0, t0, 0", [("t0", -16)], m_sra(-16, 0))

# --- 基础指令：让工具可以顺带兜住「判定指令本身」的正确性 ---
case("basic", "add 溢出回绕", "add t0, t0, t1", [("t0", 0x7FFFFFFF), ("t1", 1)], m_add(0x7FFFFFFF, 1))
case("basic", "addi 负立即数", "addi t0, t0, -2048", [("t0", 0)], u32(-2048))
case("basic", "sub 0-1", "sub t0, t0, t1", [("t0", 0), ("t1", 1)], m_sub(0, 1))
case("basic", "and 掩码", "and t0, t0, t1", [("t0", 0xFF00FF00), ("t1", 0x0FF00FF0)], m_and(0xFF00FF00, 0x0FF00FF0))
case("basic", "or 掩码", "or t0, t0, t1", [("t0", 0xF0F00000), ("t1", 0x000F0F00)], m_or(0xF0F00000, 0x000F0F00))
case("basic", "xor 掩码", "xor t0, t0, t1", [("t0", 0xFFFFFFFF), ("t1", 0x12345678)], m_xor(0xFFFFFFFF, 0x12345678))
case("basic", "slt -1<1", "slt t0, t0, t1", [("t0", -1), ("t1", 1)], m_slt(-1, 1))
case("basic", "sltu -1<1", "sltu t0, t0, t1", [("t0", -1), ("t1", 1)], m_sltu(-1, 1))
case("basic", "slt INT_MIN<INT_MAX", "slt t0, t0, t1", [("t0", 0x80000000), ("t1", 0x7FFFFFFF)], m_slt(0x80000000, 0x7FFFFFFF))


# 更多译码场景：不同寄存器字段、立即数符号扩展及 x0 写入。
case("basic", "add 不同目标寄存器", "add t2, t0, t1", [("t0", 17), ("t1", 29), ("t2", 99)], 46)
case("basic", "sub 目标与 rs2 重合", "sub t1, t0, t1", [("t0", 9), ("t1", 4)], 5)
case("basic", "add 写入 x0", "add zero, t0, t1", [("t0", 4), ("t1", 5)], 0)
case("basic", "xori 负立即数", "xori t0, t0, -1", [("t0", 0x12345678)], 0xedcba987)
case("basic", "ori 负立即数", "ori t0, t0, -2048", [("t0", 1)], 0xfffff801)
case("basic", "andi 负立即数", "andi t0, t0, -2048", [("t0", -1)], 0xfffff800)
case("basic", "slti 有符号", "slti t0, t0, -1", [("t0", -2)], 1)
case("basic", "sltiu 立即数符号扩展", "sltiu t0, t0, -1", [("t0", 0xfffffffe)], 1)

MEM_ADDR = 0x80001000
MEM_INITIAL = bytes.fromhex("11 80 7f ff 00 80 34 12 78 56 34 12 aa bb cc dd")


def mem_case(name, inst, setup, expected, memory_after=MEM_INITIAL):
    CASES.append(dict(group="memory", name=name, inst=inst, setup=setup,
                      exp=u32(expected), memory_after=memory_after))


def stored(offset, width, value):
    data = bytearray(MEM_INITIAL)
    data[offset:offset + width] = (value & ((1 << (8 * width)) - 1)).to_bytes(width, "little")
    return bytes(data)


# t4 指向测试区第 8 字节，覆盖正、负偏移以及不同字节位置。
mem_case("load lb 负数符号扩展", "lb t0, -7(t4)", [], 0xffffff80)
mem_case("load lb 正数", "lb t0, -6(t4)", [], 0x7f)
mem_case("load lb -1", "lb t0, -5(t4)", [], 0xffffffff)
mem_case("load lbu 零扩展", "lbu t0, -7(t4)", [], 0x80)
mem_case("load lbu 高字节", "lbu t0, -5(t4)", [], 0xff)
mem_case("load lh 负数符号扩展", "lh t0, -4(t4)", [], 0xffff8000)
mem_case("load lh 正数", "lh t0, -2(t4)", [], 0x1234)
mem_case("load lhu 零扩展", "lhu t0, -4(t4)", [], 0x8000)
mem_case("load lw 零偏移", "lw t0, 0(t4)", [], 0x12345678)
mem_case("load lw 正偏移", "lw t0, 4(t4)", [], 0xddccbbaa)
mem_case("load lw 负偏移", "lw t0, -8(t4)", [], 0xff7f8011)
mem_case("load lw 写入 x0", "lw zero, 0(t4)", [], 0)
mem_case("store sb 负偏移 截断", "sb t0, -7(t4)", [("t0", 0x123456ab)], 0, stored(1, 1, 0x123456ab))
mem_case("store sb 正偏移", "sb t0, 3(t4)", [("t0", 0x42)], 0, stored(11, 1, 0x42))
mem_case("store sh 负偏移 截断", "sh t0, -4(t4)", [("t0", 0x1234abcd)], 0, stored(4, 2, 0x1234abcd))
mem_case("store sh 正偏移", "sh t0, 2(t4)", [("t0", 0x5678)], 0, stored(10, 2, 0x5678))
mem_case("store sw 零偏移", "sw t0, 0(t4)", [("t0", 0xdeadbeef)], 0, stored(8, 4, 0xdeadbeef))
mem_case("store sw 负偏移", "sw t0, -8(t4)", [("t0", 0x13579bdf)], 0, stored(0, 4, 0x13579bdf))
mem_case("store sw 正偏移", "sw t0, 4(t4)", [("t0", 0x01234567)], 0, stored(12, 4, 0x01234567))
mem_case("store sw x0", "sw zero, -4(t4)", [], 0, stored(4, 4, 0))

REGS = ("$0 ra sp gp tp t0 t1 t2 s0 s1 a0 a1 a2 a3 a4 a5 "
        "a6 a7 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 t3 t4 t5 t6").split()
ALIASES = {"zero": "$0", "x0": "$0"}
ALIASES.update({"x{}".format(i): name for i, name in enumerate(REGS)})


def canon_inst(text):
    parts = text.replace("\t", " ").split(None, 1)
    ops = []
    for op in parts[1].split(",") if len(parts) > 1 else []:
        op = op.strip()
        try:
            ops.append(int(op, 0))
        except ValueError:
            ops.append(ALIASES.get(op, op))
    return parts[0], tuple(ops)


def emit_asm(c):
    lines = [".section .text", ".globl _start", "_start:"]
    data = []
    if c["group"] == "memory":
        lines.append("  la t4, testdata+8")
    for reg, val in c["setup"]:
        val = s32(val)
        if -2048 <= val <= 2047:
            lines.append("  addi {}, zero, {}".format(reg, val))
        else:
            label = ".LC{}".format(len(data))
            data.append((label, u32(val)))
            lines += ["  la {}, {}".format(reg, label), "  lw {}, 0({})".format(reg, reg)]
    lines += [".globl tested", "tested:", "  " + c["inst"], "  ebreak", ".section .rodata", ".align 2"]
    lines += ["{}: .word 0x{:08x}".format(label, val) for label, val in data]
    if c["group"] == "memory":
        lines += ['.section .testdata,"aw"', 'testdata:',
                  '  .byte ' + ', '.join(str(b) for b in MEM_INITIAL)]
    return "\n".join(lines) + "\n"


def run(cmd, **kwargs):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=10, **kwargs)


def build(c, prefix, work, tag):
    src, obj, elf, binary = [work / (tag + ext) for ext in (".S", ".o", ".elf", ".bin")]
    src.write_text(emit_asm(c))
    commands = [
        [prefix + "as", "-march=rv32im", "-mabi=ilp32", "-o", str(obj), str(src)],
        [prefix + "ld", "-m", "elf32lriscv", "-Ttext=0x80000000", "--section-start=.testdata=0x80001000", "-e", "_start", "-o", str(elf), str(obj)],
        [prefix + "objcopy", "-O", "binary", str(elf), str(binary)],
        [prefix + "objdump", "-d", "-M", "no-aliases", str(elf)],
    ]
    for cmd in commands:
        result = run(cmd)
        if result.returncode:
            raise RuntimeError("{}: {}".format(cmd[0], result.stderr.strip()))
    match = re.search(r"^([0-9a-f]+) <tested>:\s*\n\s*([0-9a-f]+):\s+([0-9a-f]+)\s+([^\n]+)", result.stdout, re.M)
    if not match:
        raise RuntimeError("无法定位 tested 指令")
    pc = int(match.group(1), 16)
    got = canon_inst(match.group(4).split("#")[0].strip())
    want = canon_inst(c["inst"])
    if int(match.group(2), 16) != pc or len(match.group(3)) != 8 or got != want:
        raise RuntimeError("被测位置编码不符: 期望 {}, 反汇编 {}".format(want, got))
    if pc <= 0x80000000 or (pc - 0x80000000) % 4:
        raise RuntimeError("测试指令地址无效: 0x{:08x}".format(pc))
    return binary, pc


def snapshot(text):
    result = {}
    for name, val in re.findall(r"^([\w$]+)\s+(0x[0-9a-fA-F]+)\s", text, re.M):
        if name in REGS or name == "pc":
            result[name] = int(val, 16)
    if set(result) != set(REGS + ["pc"]):
        raise RuntimeError("无法解析完整寄存器快照，请检查 NEMU 的 info r 输出")
    return result


def execute(nemu, binary, pc, c):
    steps = (pc - 0x80000000) // 4
    memory_cmd = "x 4 0x{:x}\n".format(MEM_ADDR) if c["group"] == "memory" else ""
    commands = "si {}\ninfo r\n{}si 1\ninfo r\n{}q\n".format(steps, memory_cmd, memory_cmd)
    r = run([str(nemu), str(binary)], cwd=str(NEMU_ROOT), input=commands)
    output = r.stdout + r.stderr
    if r.returncode or "ABORT" in output or "HIT BAD TRAP" in output or "HIT GOOD TRAP" in output:
        raise RuntimeError("NEMU 异常退出或未到达测试位置 (退出码 {})".format(r.returncode))
    parts = output.split("(nemu) info r")
    if len(parts) != 3:
        raise RuntimeError("没有取得两次寄存器快照")
    before, after = snapshot(parts[1]), snapshot(parts[2])
    if before["pc"] != pc:
        raise RuntimeError("准备阶段 PC 错误: 0x{:08x}，期望 0x{:08x}".format(before["pc"], pc))
    for reg, value in c["setup"]:
        reg = ALIASES.get(reg, reg)
        if before[reg] != u32(value):
            raise RuntimeError("准备阶段 {} = 0x{:08x}，期望 0x{:08x}".format(reg, before[reg], u32(value)))
    memory = None
    if c["group"] == "memory":
        if before["t4"] != MEM_ADDR + 8:
            raise RuntimeError("准备阶段内存基址 t4 错误: 0x{:08x}".format(before["t4"]))
        dumps = re.findall(r"^0x80001000:\s+((?:0x[0-9a-fA-F]+\s+){4})", output, re.M)
        if len(dumps) != 2:
            raise RuntimeError("无法解析两次内存快照")
        memory = [b''.join(int(word, 16).to_bytes(4, 'little') for word in dump.split()) for dump in dumps]
        if memory[0] != MEM_INITIAL:
            raise RuntimeError("准备阶段内存与预期不符: {}".format(memory[0].hex(' ')))
    return before, after, memory


def compare(c, pc, before, after, expected):
    want = before.copy()
    rd = canon_inst(c["inst"])[1][0]
    if rd != "$0" and not c["inst"].startswith(("sb ", "sh ", "sw ")):
        want[rd] = expected
    want["$0"] = 0
    want["pc"] = u32(pc + 4)
    return ["{}: 实际 0x{:08x}, 期望 0x{:08x}".format(reg, after[reg], want[reg])
            for reg in REGS + ["pc"] if after[reg] != want[reg]]


def compare_memory(actual, expected):
    if actual == expected:
        return []
    return ["内存: 实际 {}, 期望 {}".format(actual.hex(' '), expected.hex(' '))]


def positive_int(value):
    try:
        number = int(value)
        if number > 0:
            return number
    except ValueError:
        pass
    raise argparse.ArgumentTypeError("并发数必须是正整数")


def run_case(index, c, args, nemu, work):
    start = time.perf_counter()
    try:
        binary, pc = build(c, args.prefix, work, "case{:03d}".format(index))
        before, after, memory = execute(nemu, binary, pc, c)
        errors = compare(c, pc, before, after, c["exp"])
        if memory is not None:
            errors.extend(compare_memory(memory[1], c["memory_after"]))
        if args.selftest:
            for reg in REGS + ["pc"]:
                changed = after.copy()
                changed[reg] ^= 1
                if not compare(c, pc, before, changed, c["exp"]):
                    errors.append("比较器未发现 {} 的错误".format(reg))
            if memory is not None:
                for offset in range(len(memory[1])):
                    changed = bytearray(memory[1])
                    changed[offset] ^= 1
                    if not compare_memory(changed, c["memory_after"]):
                        errors.append("比较器未发现内存偏移 {} 的错误".format(offset))
        status = "FAIL" if errors else "PASS"
        detail = "; ".join(errors)
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as e:
        status, detail = "ERROR", str(e)
    return index, c["name"], status, detail, time.perf_counter() - start


def main():
    ap = argparse.ArgumentParser(description="NEMU RV32 指令行为回归")
    ap.add_argument("--nemu", default=str(NEMU_ROOT / "build/riscv32-nemu-interpreter"))
    ap.add_argument("--prefix", default="riscv64-linux-gnu-")
    ap.add_argument("--only", help="筛选名称包含的子串")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--selftest", action="store_true", help="篡改实际快照，验证宿主机比较器能发现错误")
    ap.add_argument("--keep", action="store_true", help="保留临时产物")
    ap.add_argument("-j", "--jobs", type=positive_int, default=8, metavar="N",
                    help="最多同时运行 N 个用例（默认 8；-j1 串行）")
    args = ap.parse_args()
    cases = [c for c in CASES if not args.only or args.only in c["name"]]
    if not cases:
        print("没有匹配的用例", file=sys.stderr)
        return 2
    if args.list:
        for c in cases:
            result = c["memory_after"].hex(' ') if c["inst"].startswith(("sb ", "sh ", "sw ")) else "0x{:08x}".format(c["exp"])
            print("[{}] {} -> {}".format(c["group"], c["name"], result))
        return 0
    nemu = Path(args.nemu).resolve()
    missing = [args.prefix + t for t in ("as", "ld", "objcopy", "objdump") if not shutil.which(args.prefix + t)]
    if not nemu.is_file() or missing:
        print("环境不完整：NEMU={}，缺少工具={}".format(nemu, missing), file=sys.stderr)
        return 2
    inst_src = NEMU_ROOT / "src/isa/riscv32/inst.c"
    if inst_src.stat().st_mtime > nemu.stat().st_mtime:
        print("警告：inst.c 比 NEMU 更新，请先重新编译。")
    failures = 0
    case_seconds = 0.0
    start = time.perf_counter()
    print("运行 {} 个用例，并发数 {}".format(len(cases), args.jobs), flush=True)
    with tempfile.TemporaryDirectory(prefix="nemu-isa-test-") as tmp:
        work = Path(tmp)
        if args.keep:
            work = Path(tempfile.mkdtemp(prefix="nemu-isa-test-"))
            print("中间产物：{}".format(work))
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(run_case, i, c, args, nemu, work) for i, c in enumerate(cases, 1)]
            for future in as_completed(futures):
                i, name, status, detail, elapsed = future.result()
                failures += status != "PASS"
                case_seconds += elapsed
                print("[{}/{}] {} {} ({:.3f}s){}".format(
                    i, len(cases), status, name, elapsed, ": " + detail if detail else ""), flush=True)
    elapsed = time.perf_counter() - start
    print("结果：{} 个用例，{} 个失败；总耗时 {:.3f}s；用例平均耗时 {:.3f}s".format(
        len(cases), failures, elapsed, case_seconds / len(cases)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
