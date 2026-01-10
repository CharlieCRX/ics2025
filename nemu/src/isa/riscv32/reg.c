/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include "local-include/reg.h"

const char *regs[] = {
  "$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
  "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
  "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
  "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

void isa_reg_display() {
  for (int i = 0; i < ARRLEN(regs); i++) {
    uint32_t val = gpr(i);

    char hexbuf[16];
    sprintf(hexbuf, "0x%x", val);   // 不补零

    printf("%s\t%-10s\t%d\n", reg_name(i), hexbuf, val);
  }

  char pcbuf[16];
  sprintf(pcbuf, "0x%x", cpu.pc);

  printf("pc\t%-10s\t%d\n", pcbuf, cpu.pc);
}

word_t isa_reg_str2val(const char *s, bool *success) {
  // 特殊的寄存器名称处理
  if (strcmp(s, "0") == 0) {
    *success = true;
    return 0;
  }

  for (int i = 0; i < ARRLEN(regs); i++) {
    if (strcmp(s, regs[i]) == 0) {
      *success = true;
      return (word_t)gpr(i);
    }
  }

  *success = false;
  return 0;
}
