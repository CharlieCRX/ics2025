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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>

// this should be enough
static char buf[65536] = {};
static char code_buf[65536 + 128] = {}; // a little larger than `buf`
static char *code_format =
"#include <stdio.h>\n"
"int main() { "
"  unsigned result = %s; "
"  printf(\"%%u\", result); "
"  return 0; "
"}";

uint32_t choose(uint32_t n) {
  return (uint32_t)rand() % n;
}

uint32_t rand32_simple() {
  uint32_t b1 = (uint32_t)rand() & 0xFF; // 低8位
  uint32_t b2 = (uint32_t)rand() & 0xFF; // 次低8位
  uint32_t b3 = (uint32_t)rand() & 0xFF; // 次高8位
  uint32_t b4 = (uint32_t)rand() & 0xFF; // 高8位
  return (b4 << 24) | (b3 << 16) | (b2 << 8) | b1;
}

/**
 * @brief 生成一个随机的无符号32位整数
 * @details 
 *  1. 生成随机数：产生一个 uint32_t 类型的随机值。
 *  2. 填充缓冲区：将数字转换成字符串，分别存入 c_buf（带 u 后缀或强转）和 eval_buf。
 *  3. 更新预算：消耗一个 Token 额度。
 * @param[in] c_buf 给 C 编译器看的代码
 * @param[in] eval_buf 给 NEMU 读的表达式
 * @param[in] budget token剩余预算数量
 * @return 返回生成数字的 uint32_t 数值
 * @date 2025-12-28
 * @note 注意事项（可选）
 * @warning 尚未
 * @todo 尚未加入随机空格
 */
uint32_t gen_num(char *c_buf, char *eval_buf, int *budget) {

  uint32_t num = rand32_simple();

  // 生成 C 语言版本，带强转保险
  sprintf(c_buf, "(uint32_t)%uu", num);

  // 生成纯净版给 eval 读
  sprintf(eval_buf, "%u", num);

  (*budget) -= 1;

  return num;
}

char gen_rand_op() {
  switch (choose(4))
  {
  case 0: return '+';
  case 1: return '-';
  case 2: return '*';
  default: return '/';
  }
}


static uint32_t gen_rand_expr(char *c_buf, char *eval_buf, int *budget) {

  // 1. 判断预算是否触底（比如 < 3）
  if (budget < 3) {
    return gen_num(c_buf, eval_buf, budget);
  }


  // 2. 随机选择路径 (数字、括号、二元运算)
  switch (choose(3))
  {
  // 生成数字  
  case 0: return gen_num(c_buf, eval_buf, budget);

  // 生成 ( 表达式 )
  case 1: {
    char temp_c[512], temp_eval[512];
    // 1. 消耗括号占用的 2 个 Token
    *budget -= 2;

    // 2. 递归生成内部表达式
    uint32_t val = gen_rand_expr(temp_c, temp_eval, budget);

    // 3. 拼接字符串
    sprintf(eval_buf, "(%s)", temp_eval);
    sprintf(c_buf, "(%s)", temp_c);

    return val;
  }

  // 生成 val1 op val2 的格式
  default: {

    // 1. 生成临时 val1 op val2 字符串 (保证不除 0 )
    char l_c[512], r_c[512], l_eval[512], r_eval[512];

    *budget -= 1; // 消耗 op 占用的 1 个 Token

    uint32_t l_val = gen_rand_expr(l_c, l_eval, budget);
    char op = gen_rand_op();

    int temp_budget = *budget;
    uint32_t r_val = gen_rand_expr(r_c, r_eval, &temp_budget);

    // 过滤除 0 表达式
    while (op == '/' && r_val == 0) {
      temp_budget = *budget;
      r_val = gen_rand_expr(r_c, r_eval, temp_budget);
    }

    *budget = temp_budget;

    // 2. 拼接表达式字符串到最终缓冲区
    sprintf(c_buf, "(uint32_t)((uint32_t)%s %c (uint32_t)%s)", l_c, op, r_c);
    sprintf(eval_buf, "%s %c %s",  l_eval, op, r_eval);

    // 3. 计算结果并返回
    switch (op)
    {
    case '+': return (uint32_t)l_val + (uint32_t)r_val;
    case '-': return (uint32_t)l_val - (uint32_t)r_val;
    case '*': return (uint32_t)l_val * (uint32_t)r_val;
    case '/': return (uint32_t)l_val / (uint32_t)r_val;
    default: assert(0); return;
    }

  }
  }
}

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }
  int i;
  for (i = 0; i < loop; i ++) {
    // gen_rand_expr();

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
    if (ret != 0) continue;

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    int result;
    ret = fscanf(fp, "%d", &result);
    pclose(fp);

    printf("%u %s\n", result, buf);
  }
  return 0;
}
