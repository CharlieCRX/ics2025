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
#include <cpu/cpu.h>
#include <memory/vaddr.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"

static int is_batch_mode = false;

void init_regex();

// 监视点相关的公共接口
typedef struct watchpoint WP;
void init_wp_pool();
WP* new_wp(const char *expr_str);
void free_wp(WP *wp);
void wp_set_eval_func(bool (*func)(const char *expr, word_t *result));
void watchpoints_display(void);
bool wp_delete_by_no(int no);

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

/* command handlers */
static int cmd_help(char *args);
static int cmd_c(char *args);
static int cmd_si(char *args);
static int cmd_q(char *args);
static int cmd_info(char *args);
static int cmd_x(char *args);
static int cmd_p(char *args);
static int cmd_w(char *args);
static int cmd_delete_wp_by_no(char *args);

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "si", "Step [N] instructions (default 1) of the program", cmd_si },
  { "info", "Display CPU or watchpoint info: info r / info w", cmd_info },
  { "q", "Exit NEMU", cmd_q },
  { "x", "Evaluate EXPR as start address, output N consecutive 4-byte values in hex: x N EXPR", cmd_x},
  { "p", "Evaluate the expression EXPR and print the result: p EXPR", cmd_p },
  { "w", "Set a watchpoint for expression EXPR: w EXPR", cmd_w },
  { "d", "Delete the watchpoint with number N: d N", cmd_delete_wp_by_no },
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}

static int cmd_si(char *args) {
  int n = 1; // 默认的步数 = 1
  if (args != NULL) {
    n = atoi(args); // 将参数转换为整数
    if (n <= 0) {
      printf("Invalid number of steps: %s\n", args);
      return 0;
    }
  }

  cpu_exec(n); // 执行 n 步
  return 0;
}


static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  return -1;
}

static int cmd_info(char *args) {
  if (args == NULL) {
    printf("Usage: info r/w\n");
    return 0;
  }

  if (strcmp(args, "r") == 0) {
    isa_reg_display();
  }
  else if (strcmp(args, "w") == 0) {
    watchpoints_display();
  }
  else {
    printf("Unknown info command '%s'\n", args);
  }
  return 0;
}

static int cmd_x(char *args) {
  if (args == NULL) {
    printf("Usage: x N EXPR\n");
    return 0;
  }

  // 解析 N
  char *N_str = strtok(args, " ");
  if (N_str == NULL) {
    printf("Missing N!\n");
    return 0;
  }
  int N_num;
  if (sscanf(N_str, "%d", &N_num) != 1  || N_num <= 0) {
    printf("Invalid N!\n");
    return 0;
  }

  // 解析 EXPR
  char *expr = strtok(NULL, " ");
  if (expr == NULL) {
    printf("Missing EXPR!\n");
    return 0;
  }

  vaddr_t vaddr;
  if (sscanf(expr, "%x", &vaddr) != 1) {
    printf("Invalid EXPR address!\n");
    return 0;
  }

  // 打印解析的 N 和 addr
  printf("N = %d, addr = " FMT_WORD "\n", N_num, vaddr);

  // 每行输出格式：地址 + 4 个 word_t (sizeof(word_t) 字节) 的数据
  for (int i = 0; i < N_num; i++)
  {
    // 行首输出地址
    if (i % 4 == 0) {
      printf(FMT_WORD ": ", vaddr);
    }

    // 获取一个 word_t 长度的数据
    word_t data = vaddr_read(vaddr, sizeof(word_t));
    vaddr += sizeof(word_t);

    // 打印数据
    printf(FMT_WORD "  ", data);

    // 每打印 4 个数据换行，或者在最后一个数据后换行
    if ((i + 1) % 4 == 0 || i == N_num - 1) {
      printf("\n");
    }
  }
  
  return 0;

}

static int cmd_p(char *args) {
  if (args == NULL) {
    printf("Usage: p EXPR\n");
    return 0;
  }

  bool success = true;
  word_t result = expr(args, &success);
  if (!success) {
    printf("Failed to evaluate expression: %s\n", args);
    return 0;
  }

  printf("Result: " FMT_WORD "(%u)\n", result, result);
  return 0;
}

// 监视点表达式求值函数适配器
static bool sdb_eval(const char *expr_str, word_t *result) {
  bool success = true;
  *result = expr((char *)expr_str, &success);
  return success;
}

/* Set a watchpoint for the given expression */
static int cmd_w(char *args) {
  if (args == NULL) {
    printf("Usage: w EXPR\n");
    return 0;
  }
  
  WP *wp = new_wp(args);
  if (wp == NULL) {
    printf("Failed to set watchpoint for expression: %s\n", args);
    return 0;
  }

  printf("Watchpoint created successfully\n");
  return 0;
}

static int cmd_delete_wp_by_no(char *args) {
  if (args == NULL) {
    printf("Usage: d N\n");
    return 0;
  }

  int no = atoi(args);
  if (no < 0) {
    printf("Invalid watchpoint number: %s\n", args);
    return 0;
  }

  bool success = wp_delete_by_no(no);
  if (!success) {
    printf("No watchpoint with number %d\n", no);
    return 0;
  }

  printf("Watchpoint %d deleted successfully\n", no);
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();

  /* Set the expression evaluation function for watchpoints. */
  wp_set_eval_func(sdb_eval);
}
