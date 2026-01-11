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
#include <memory/vaddr.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

enum {
  TK_NOTYPE = 256, 
  TK_EQ,
  TK_DEC,
  TK_HEX,
  TK_REG,
  TK_IDENT,
  TK_AND,
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
  {"&&", TK_AND},       // 与运算
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
    case TK_AND:    return "TK_AND (与运算)";
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

#define MAX_TOKEN_STR_LEN 32
#define MAX_TOKEN_NUM 32
typedef struct token {
  int type;
  char str[MAX_TOKEN_STR_LEN];
} Token;

static Token tokens[MAX_TOKEN_NUM] __attribute__((used)) = {};
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

  // 1. 重置计数（原有逻辑保留，移到清空后或前都可）
  nr_token = 0;
  // 2. 清空整个tokens数组的内存（置0，消除脏数据）
  // memset：将tokens数组的所有字节设为0，覆盖type/str等字段
  memset(tokens, 0, sizeof(tokens));

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

        // ========== 核心新增：添加Token前检查容量 ==========
        if (nr_token >= MAX_TOKEN_NUM) {
          // 友好的错误提示：告知超限、当前位置、最大容量
          printf("Error: Token数量超出上限！最大支持%d个，当前尝试添加第%d个\n",
                  MAX_TOKEN_NUM, nr_token + 1);
          printf("出错位置：%d\n%s\n%*.s^\n", position, e, position, "");
          return false; // 终止解析，避免数组越界
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
            strncpy(tokens[nr_token].str, substr_start, substr_len); 
            tokens[nr_token].str[substr_len] = '\0'; // 添加字符串结束符
            break;
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

// 定义表达式求值错误码
typedef enum {
  EVAL_OK = 0,

  EVAL_ERR_INVALID_RANGE,      // p > q
  EVAL_ERR_BAD_EXPRESSION,     // 无法解析为合法表达式
  EVAL_ERR_PAREN_MISMATCH,     // 括号结构错误
  EVAL_ERR_PAREN_EMPTY,        // ()
  EVAL_ERR_DIV_ZERO,           // 除零
  EVAL_ERR_REG_NOT_FOUND,      // 找不到指定的寄存器
} EvalErrType;
EvalErrType eval(int p, int q, word_t *result);

word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  word_t result;
  EvalErrType eval_err = eval(0, nr_token - 1, &result);
  if (eval_err != EVAL_OK) {
    *success = false;
    return 0;
  }

  *success = true;
  return result;
}

/**
 * @brief 判断 Token 是否为一个主表达式（数字、寄存器等）
 */
static bool is_primary_expr(int p) {
  int type = tokens[p].type;
  return (type == TK_DEC || type == TK_HEX || type == TK_REG);
}

// 辅助函数：获取 Token 的数值
static word_t eval_primary_expr(int p, bool *success) {
  int type = tokens[p].type;
  *success = true;

  if (type == TK_DEC) {
    return (word_t)strtoul(tokens[p].str, NULL, 10);
  } else if (type == TK_HEX) {
    return (word_t)strtoul(tokens[p].str, NULL, 16);
  } else if (type == TK_REG) {
    // 调用现成接口，注意跳过 '$' 符号 (tokens[p].str + 1)
    return isa_reg_str2val(tokens[p].str + 1, success);
  }

  *success = false;
  return 0;
}

/* --- 括号检查错误类型 --- */
typedef enum {
  PAREN_ERR_NONE,
  PAREN_ERR_MISMATCH,    // 括号数量或顺序不匹配
  PAREN_ERR_EMPTY,       // 空括号 ()
  PAREN_ERR_NOT_WRAP,    // 不是(expr)类型：(1+2)-(3)或者 )1-2(
  PAREN_ERR_INVALID_RANGE
} ParenErrType;

/**
 * @brief 检查 [start, end] 范围内的 token 是否构成一个合法的 (expr) 结构
 * 逻辑：
 * 1. 首尾必须是 '(' 和 ')'
 * 2. 遍历过程中，左括号必须与右括号抵消
 * 3. 核心判定：在到达最后一个 token 前，括号 balance 不能提前归零（否则说明是 (a+b)+(c+d) 结构）
 */
static bool check_parentheses(int start, int end, ParenErrType *err_type) {
  *err_type = PAREN_ERR_NONE;

  if (start < 0 || end < 0 || start > end) {
    *err_type = PAREN_ERR_INVALID_RANGE;
    return false;
  }

  // 场景：首尾不匹配（直接判定不是 (expr)）
  if (tokens[start].type != '(' || tokens[end].type != ')') {
    *err_type = PAREN_ERR_NOT_WRAP;
    return false;
  }

  // 场景：空括号 ()
  if (start + 1 == end) {
    *err_type = PAREN_ERR_EMPTY;
    return false;
  }

  int balance = 0;
  for (int i = start; i <= end; i++) {
    if (tokens[i].type == '(') {
      balance++;
    } else if (tokens[i].type == ')') {
      balance--;

      // 场景：中途右括号多于左括号，如 (a+b))+(c
      if (balance < 0) {
        *err_type = PAREN_ERR_MISMATCH;
        return false;
      }

      // 核心判定：如果 balance 提前归零且还没到 end，说明最外层括号没包住全段
      // 例如：(1+2) + (3+4)，当处理到第一个 ')' 时 balance 为 0，但 i < end
      if (balance == 0 && i < end) {
        *err_type = PAREN_ERR_NOT_WRAP;
        return false;
      }
    }
  }

  // 最终 balance 必须为 0（处理多出左括号的情况）
  if (balance != 0) {
    *err_type = PAREN_ERR_MISMATCH;
    return false;
  }

  return true;
}


// 是否能作为一个二元运算符的前缀
static bool can_be_binary_prefix(int i, int start) {
  if (i < start) {
    return false;
  }

  // 前一个 Token 是否是一个操作数的合法结束标志。
  int type = tokens[i].type;
  switch (type)
  {
  case TK_DEC:
  case TK_HEX:
  case TK_IDENT:
  case TK_REG:
  case ')':
    return true;
  
  default:
    return false;
  }
}

static bool is_operator(int type) {
  switch (type)
  {
    case '+': case '-': case '*': case '/':
    case TK_AND:
    case TK_EQ:
      return true;
    default:
      return false;
  }
}

static int get_precedence(int type) {
  switch (type) {
    case TK_AND: return 1;
    case TK_EQ:  return 2;
    case '+': case '-': return 3;
    case '*': case '/': return 4;
    default: return 0;
  }
}

// 查找 [start, end] 范围内的主运算符
static int find_main_operator(int start, int end) {

  int op_index = -1;
  if (start > end) {
    return op_index;
  }

  int balance = 0;
  for (int i = start; i <= end; i++) {
    // 排除括号内的运算符
    if (tokens[i].type == '(') {
      balance++;
      continue;
    }

    if (tokens[i].type == ')') {
      balance--;
      continue;
    }

    if (balance == 0 && is_operator(tokens[i].type) && can_be_binary_prefix(i - 1, start)) {

      if (op_index == -1 || get_precedence(tokens[i].type) <= get_precedence(tokens[i].type)) {
        op_index = i;
      }
    }
  }

  return op_index;
}

// 应用运算符计算结果
static word_t apply_binary_operator(int op, word_t val1, word_t val2) {
  switch (tokens[op].type) {
    case '+': return val1 + val2;
    case '-': return val1 - val2;
    case '*': return val1 * val2;
    case '/': return val1 / val2;
    case TK_AND: return (val1 && val2);
    case TK_EQ:  return (val1 == val2);
    default:
      TODO();
      return 0;
    }
}

static EvalErrType eval_unary_expr(int p, int q, word_t *res) {
  word_t val;
  // 第一步：对运算符右边的表达式进行求值 [p + 1, q]
  EvalErrType err = eval(p + 1, q, &val);
  if (err != EVAL_OK) return err;

  // 第二步：根据 tokens[p].type 执行相应的逻辑
  switch (tokens[p].type) {
    case '-':
      *res = -val;
      return EVAL_OK;

    case '*':
      *res = vaddr_read(val, sizeof(word_t));
      return EVAL_OK;

    default:
      return EVAL_ERR_BAD_EXPRESSION;
  }
}

/**
 * @brief 核心抽象函数：递归求值表达式
 *
 * @param p 起始 Token 下标
 * @param q 结束 Token 下标
 * @param res 存储结果的指针
 * @return EvalErrType 错误码
 */
EvalErrType eval(int p, int q, word_t *res) {
  if (p > q) {
    return EVAL_ERR_INVALID_RANGE;
  }

  if (p == q) {
    if (!is_primary_expr(p)) {
      return EVAL_ERR_BAD_EXPRESSION;
    }
    bool success;
    *res = eval_primary_expr(p, &success);

    if (!success) {
      return EVAL_ERR_REG_NOT_FOUND; // 寄存器名解析失败
    }
    return EVAL_OK;
  }

  ParenErrType perr;
  bool is_paren = check_parentheses(p, q, &perr);

  if (is_paren) {
    return eval(p + 1, q - 1, res);
  }

  /* 括号相关的“致命结构错误” */
  if (perr == PAREN_ERR_MISMATCH) {
    return EVAL_ERR_PAREN_MISMATCH;
  }
  if (perr == PAREN_ERR_EMPTY) {
    return EVAL_ERR_PAREN_EMPTY;
  }
  if (perr == PAREN_ERR_INVALID_RANGE) {
    return EVAL_ERR_INVALID_RANGE;
  }

  /* NOT_WRAP → 普通表达式处理 */
  int op = find_main_operator(p, q);

  // 二元运算
  if (op != -1) {
    word_t val1, val2;
    EvalErrType err;
  
    err = eval(p, op - 1, &val1);
    if (err != EVAL_OK) {
      return err;
    }
  
    err = eval(op + 1, q, &val2);
    if (err != EVAL_OK) {
      return err;
    }
  
    if (tokens[op].type == '/' && val2 == 0) {
      return EVAL_ERR_DIV_ZERO;
    }
  
    *res = apply_binary_operator(op, val1, val2);
    return EVAL_OK;
  } else {
    return eval_unary_expr(p, q, res);
  }
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
    fprintf(stderr, "Error: Token 过长错误 (严格模式)：\n");
    fprintf(stderr, "  - Token 类型：%s\n", get_token_type_name(token_type));
    fprintf(stderr, "  - 实际长度：%d (上限：%d)\n", substr_len, MAX_TOKEN_STR_LEN);
    fprintf(stderr, "  - 起始位置：%d\n", current_position - substr_len); // 计算Token起始位置
    fprintf(stderr, "  - 超限内容：%.*s\n", substr_len, substr_start);
    fprintf(stderr, "  - 完整表达式：%s\n", expr);
    fprintf(stderr, "  - 错误标记：%.*s^\n", current_position - substr_len, "");
    return false; // 长度超限，返回不合法
  }

  // ========== 新增：数值长度检测 ==========
  if (token_type == TK_DEC) {
    // uint32_t十进制最大为10位（4294967295），超过则溢出
    if (substr_len > 10) {
      fprintf(stderr, "Error: 十进制数%s长度超过10位，超出uint32_t范围\n", substr_start);
      return false;
    }
    // 若长度等于10位，需进一步检查数值是否超过4294967295（避免"4294967296"这类10位溢出值）
    if (substr_len == 10 && strcmp(substr_start, "4294967295") > 0) {
      fprintf(stderr, "Error: 十进制数%s超出uint32_t最大值4294967295\n", substr_start);
      return false;
    }
  } else if (token_type == TK_HEX) {
    // uint32_t十六进制最大为8位（0xFFFFFFFF），超过则溢出（忽略0x前缀）
    int hex_digit_len = substr_len;
    if (substr_len >= 2 && substr_start[0] == '0' && (substr_start[1] == 'x' || substr_start[1] == 'X')) {
      hex_digit_len -= 2; // 去掉0x前缀
    }
    if (hex_digit_len > 8) {
      fprintf(stderr, "Error: 十六进制数%s长度超过8位，超出uint32_t范围\n", substr_start);
      return false;
    }
  }

  // 步骤4：长度合法，返回true
  return true;
}


FILE *expr_file_ptr = NULL;
/**
 * @brief 测试表达式求值模块
 *
 * @param 文件路径 格式为：结果 表达式
 */
void test_expr(const char *expr_file) {
  if (expr_file == NULL) {
    return;
  }

  expr_file_ptr = fopen(expr_file, "r");
  if (expr_file_ptr == NULL) {
    fprintf(stderr, "无法打开表达式文件：%s\n", expr_file);
    return;
  }

  char line[65635] = {0};
  int line_number = 0;
  bool success;

  while (fgets(line, sizeof(line), expr_file_ptr) != NULL) {
    line_number++;

    // 去除行尾换行符
    line[strcspn(line, "\n")] = '\0';

    // 分割预期结果和表达式
    char *space_pos = strchr(line, ' ');
    if (space_pos == NULL) {
      fprintf(stderr, "格式错误，缺少空格分隔符，行号：%d\n", line_number);
      continue;
    }

    *space_pos = '\0';
    char *expected_result_str = line;
    char *expression_str = space_pos + 1;

    // 求值表达式
    word_t result = expr(expression_str, &success);
    if (!success) {
      fprintf(stderr, "表达式求值失败，行号：%d\n", line_number);
      assert(0);
      continue;
    }

    // 转换预期结果
    word_t expected_result = (word_t)strtoul(expected_result_str, NULL, 10);

    // 比较结果
    if (result != expected_result) {
      fprintf(stderr, "结果不匹配，行号：%d，预期：%u，实际：%u\n",
              line_number, expected_result, result);
      assert(0);
    } else {
      printf("表达式通过，行号：%d\n", line_number);
    }
  }

  fclose(expr_file_ptr);
}
  