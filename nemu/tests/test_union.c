#include <stdio.h>
#include <stdint.h>

typedef union {
  struct { uint8_t rs : 2, rt : 2, op : 4; } rtype;
  struct { uint8_t addr : 4      , op : 4; } mtype;
  uint8_t inst;
} inst_t;

// 辅助函数：以二进制形式打印字节（高位 -> 低位）
void print_bits(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        printf("%d", (byte >> i) & 1);
        if (i == 4) printf(" "); // 高低4位加个空格便于观察
    }
}

int main() {
    inst_t ins;

    printf("=========================================================\n");
    printf("【校验项 1】内存占用 (零开销)\n");
    printf("=========================================================\n");
    printf("sizeof(inst_t) = %zu 字节 (预期: 1 字节)\n\n", sizeof(ins));

    // ----------------------------------------------------------------
    printf("=========================================================\n");
    printf("【校验项 2 & 3】重叠映射 与 类型双关 (共享同一块内存)\n");
    printf("=========================================================\n");
    
    // 测试 A：通过 uint8_t 写入，通过 struct 读出 (类型双关)
    ins.inst = 0xAB;  // 二进制: 1010 1011
    printf("1. 设置 ins.inst = 0x%02X (二进制: ", ins.inst);
    print_bits(ins.inst);
    printf(")\n");

    printf("   通过 rtype 读取:\n");
    printf("     rtype.op (高4位) = 0x%X (预期: 0xA)\n", ins.rtype.op);
    printf("     rtype.rs (bit0-1) = %u (预期: 3, 因为 bit0-1 为 11)\n", ins.rtype.rs);
    printf("     rtype.rt (bit2-3) = %u (预期: 2, 因为 bit2-3 为 10)\n", ins.rtype.rt);
    
    printf("   通过 mtype 读取:\n");
    printf("     mtype.addr (低4位) = 0x%X (预期: 0xB, 与 rs+rt 组成的 1011 重叠)\n", ins.mtype.addr);
    printf("     mtype.op (高4位)   = 0x%X (预期: 0xA, 与 rtype.op 完全重叠)\n\n", ins.mtype.op);

    // 测试 B：通过 struct 写入，验证 uint8_t 和另一个 struct 同步变化 (共享内存)
    printf("2. 现在修改 rtype 字段: rs=1, rt=2, op=0xF (即二进制 1111 1001)\n");
    ins.rtype.rs = 1;   // 01
    ins.rtype.rt = 2;   // 10
    ins.rtype.op = 0xF; // 1111
    
    printf("   此时通过 inst (uint8_t) 查看原始字节: 0x%02X (二进制: ", ins.inst);
    print_bits(ins.inst);
    printf(")\n");
    printf("   此时通过 mtype.addr 查看低4位: 0x%X (预期: 0x9, 因为低4位为 1001)\n", ins.mtype.addr);
    printf("   验证了 rs+rt 组合的低4位就是 addr，三者共享同一块内存。\n\n");

    // ----------------------------------------------------------------
    printf("=========================================================\n");
    printf("【校验项 4】致命缺陷：位域填充顺序 (编译器/ABI 依赖)\n");
    printf("=========================================================\n");
    printf("根据上面的输出结果 (ins.inst = 0xAB 时):\n");
    printf("  - 如果你的环境输出 op=0xA, rs=3, rt=2 (即低比特优先填充)\n");
    printf("    -> 表示编译器采用 **LSB-first (小端位域顺序)**，如 x86/gcc/clang。\n");
    printf("  - 如果你的环境输出 op=0xB, rs=2, rt=2 (即高比特优先填充)\n");
    printf("    -> 表示编译器采用 **MSB-first (大端位域顺序)**，如某些 PowerPC 或 ARM 大端模式。\n");
    printf("\n");
    printf("【严重警告】C 标准规定位域分配方向由实现定义，\n");
    printf("           因此这段代码的解析结果 **不具备跨平台可移植性**！\n");
    printf("           若需安全移植，请使用位运算宏代替位域结构体。\n");

    return 0;
}
