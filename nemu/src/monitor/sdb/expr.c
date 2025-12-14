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

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>
#define MAX_TOKEN_STR_LEN 32

enum {
  TK_NOTYPE = 256, 
  TK_EQ,
  TK_DEC,
  TK_HEX,
  TK_REG,
  TK_IDENT
};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  {" +", TK_NOTYPE},                   // spaces
  {"0[xX][0-9a-fA-F]+", TK_HEX},       // hex
  {"\\$(0|[a-zA-Z]+[0-9]*)", TK_REG},  // reg
  {"[A-Za-z_][A-Za-z0-9_]*", TK_IDENT},  // 标识符
  {"[0-9]+", TK_DEC},   // decimal
  {"\\+", '+',},        // plus
  {"\\-", '-'},         // 减
  {"\\*", '*'},         // 乘
  {"\\/", '/'},         // 除
  {"\\(", '('},         // 左括号
  {"\\)", ')'},         // 右括号
  {"==", TK_EQ},        // equal
};

// 辅助函数：获取 Token 类型的可读名称 (用于报错)
static const char* get_token_type_name(int token_type) {
  switch (token_type) {
    case TK_DEC:    return "TK_DEC(十进制数字)";
    case TK_HEX:    return "TK_HEX(十六进制数字)";
    case TK_REG:    return "TK_REG(寄存器)";
    case TK_IDENT:  return "TK_IDENT (标识符)";
    case '+':       return "加号";
    case '-':       return "减号/负号";
    case '*':       return "乘号/解引用";
    case '/':       return "除号";
    case '(':       return "左括号";
    case ')':       return "右括号";
    case TK_EQ:     return "TK_EQ (等于号)";
    default:        return "未知类型";
  }
}

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[MAX_TOKEN_STR_LEN];
} Token;

static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;
// 辅助函数：检查 Token 长度(严格模式)
static bool check_token_length(int token_type, 
                               const char *substr_start, 
                               int substr_len, 
                               const char *expr, 
                               int current_position);

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        if (rules[i].token_type == TK_NOTYPE) {
          break;
        }

        // ========== 调用抽象函数：检查 Token 长度 ==========
        if (!check_token_length(rules[i].token_type, substr_start, substr_len, e, position)) {
          return false; // 长度超限，终止解析
        }

        tokens[nr_token].type = rules[i].token_type;

        switch (rules[i].token_type) {
          case TK_DEC: 
          case TK_HEX:
          case TK_REG:
          case TK_IDENT:
            strncpy(tokens[nr_token].str, substr_start, substr_len); break;
          default:;
        }
        nr_token++;

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}


word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  /* TODO: Insert codes to evaluate the expression. */
  TODO();

  return 0;
}

// 3. 核心抽象函数：检查 Token 长度 (严格模式)
// 返回值：true = 长度合法；false = 长度超限 (需终止解析)
static bool check_token_length(int token_type, 
                               const char *substr_start, 
                               int substr_len, 
                               const char *expr, 
                               int current_position) {
  // 步骤1：仅对需要存储字符串的 Token 做长度检查 (运算符/括号无需检查)
  bool need_check = (token_type == TK_DEC || 
                     token_type == TK_HEX || 
                     token_type == TK_REG || 
                     token_type == TK_IDENT);
  if (!need_check) {
    return true; // 无需检查，直接返回合法
  }

  // 步骤2：严格检查长度是否超限
  if (substr_len > MAX_TOKEN_STR_LEN) {
    // 步骤3：打印结构化错误信息 (精准定位问题)
    fprintf(stderr, "❌ Token 过长错误 (严格模式)：\n");
    fprintf(stderr, "  - Token 类型：%s\n", get_token_type_name(token_type));
    fprintf(stderr, "  - 实际长度：%d (上限：%d)\n", substr_len, MAX_TOKEN_STR_LEN);
    fprintf(stderr, "  - 起始位置：%d\n", current_position - substr_len); // 计算Token起始位置
    fprintf(stderr, "  - 超限内容：%.*s\n", substr_len, substr_start);
    fprintf(stderr, "  - 完整表达式：%s\n", expr);
    fprintf(stderr, "  - 错误标记：%.*s^\n", current_position - substr_len, "");
    return false; // 长度超限，返回不合法
  }

  // 步骤4：长度合法，返回true
  return true;
}