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

#include <sdb.h>

#define NR_WP 32
#define WATCHPOINT_EXPR_MAX_LEN 128

static int next_wp_no = 0;

typedef bool (*wp_eval_func_t)(const char *expr, word_t *result);

static wp_eval_func_t wp_eval = NULL;

void wp_set_eval_func(wp_eval_func_t func) {
  wp_eval = func;
}

typedef struct watchpoint {
  int NO;                                  // 编号
  char expr_str[WATCHPOINT_EXPR_MAX_LEN];  // 原始表达式字符串
  word_t last_value;                       // 上一次求值结果
  struct watchpoint *next;                 // 链表指针
  bool enabled;                            // 是否启用
} WP;

static WP wp_pool[NR_WP] = {};
static WP *head = NULL, *free_ = NULL;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
  }

  head = NULL;
  free_ = wp_pool;
}

/**
 * @brief 从空闲监视点池分配一个可用节点
 *
 * @details
 * Contract:
 *  - When: 当 watchpoint 系统需要创建新的监视点时调用
 *  - Behavior:
 *      1. 从 free list 获取第一个可用的 WP 节点
 *      2. 不改变 active list 链表结构
 *      3. 不负责报错或 assert 空闲池为空
 *  - Postconditions:
 *      1. 返回值为 WP* 指向空闲节点
 *      2. 若空闲池为空，则返回 NULL
 *      3. 节点未初始化表达式和 last_value，需要由上层 new_wp() 进行初始化
 *  - Invariants:
 *      1. free list 结构保持正确
 *      2. 调用者负责处理空闲池耗尽的情况
 *
 * @return WP* 指向空闲节点，如果无可用节点返回 NULL
 */
static WP* allocate_free_wp(void) {
  if (free_ == NULL) return NULL;  // 空闲池空
  WP* wp = free_;                  // 取第一个空闲节点
  free_ = free_->next;             // 更新 free list
  wp->next = NULL;                 // 断开原链表
  return wp;
}

static void insert_active_list_head(WP* wp) {
  wp->next = head;
  head = wp;
}


/**
 * @brief 遍历当前所有 active 状态的监视点
 *
 * 语义说明：
 *  1. 仅遍历「active list」中的监视点：
 *     - 即已经创建、尚未释放的监视点；
 *     - 是否 enabled 不影响其是否被遍历。
 *
 *  2. 遍历顺序严格遵循 active list 的链表顺序：
 *     - 若 active list 使用头插法（LIFO），则最近创建的监视点最先被访问；
 *     - 顺序是确定且稳定的，可用于行为级测试。
 *
 *  3. 对每一个 active 的监视点 wp，调用一次访问函数：
 *
 *        fn(wp, user);
 *
 *     - wp 保证非 NULL，且在回调期间是有效对象；
 *     - wp 以 const WP * 形式传入，访问者不得修改监视点状态。
 *
 *  4. user 参数：
 *     - 原样透传给访问函数；
 *     - watchpoint 层不解释、不修改该指针；
 *     - 允许为 NULL。
 *
 *  5. 若当前不存在任何 active 的监视点：
 *     - 不调用 fn；
 *     - 不产生任何副作用；
 *     - 直接返回。
 *
 * 契约约束（Contract）：
 *  - fn 不得为 NULL：
 *      - fn == NULL 属于程序员错误（契约违约），
 *        在 Debug 构建下应通过 assert 及早暴露问题。
 *
 * 副作用说明：
 *  - 本函数自身不会：
 *      - 修改 active/free 链表结构；
 *      - 分配或释放监视点；
 *  - 访问函数 fn 的副作用由其自身负责。
 */
typedef void (*wp_visit_fn)(const WP *wp, void *user);
void wp_foreach_active(wp_visit_fn fn, void *user) {
  assert(fn != NULL);

  WP *cur = head;
  while (cur != NULL) {
    fn(cur, user);
    cur = cur->next;
  }
}

/**
 * @brief 创建并返回一个有明确监视对象的监视点
 *
 * @details
 * Contract
 *
 * - When (调用时机):
 *    在调试器系统（如 sdb）接收到用户新增监视点请求时调用。
 *
 * - Preconditions (前置条件):
 *    1. 输入的表达式字符串 `expr_str` 必须非空 (Non-NULL)；
 *    2. 表达式长度必须小于 `WATCHPOINT_EXPR_MAX_LEN`；
 *    3. 监视点求值函数指针 `wp_eval` 必须已成功注册/初始化；
 *    若违反上述硬性前置条件，函数将直接触发 assert 终止程序。
 *
 * - Behavior (内部行为):
 *    1. 首先尝试对表达式进行初次求值：
 *       - 若求值失败（如表达式语法错误），函数立即返回 NULL，不消耗空闲池资源；
 *    2. 从空闲池（free list）中申请一个 WP 节点：
 *       - 此时若空闲池为空，根据及早崩溃原则，直接触发 assert 终止程序；
 *    3. 节点初始化逻辑：
 *       - 分配当前唯一的 `next_wp_no` 并使全局计数自增；
 *       - 设置 `enabled` 状态为 true；
 *       - 物理拷贝 `expr_str` 并存储 `last_value` 以供后续 diff 使用；
 *    4. 结构维护：采用头插法（Head Insertion）将节点挂载至 active list。
 *    
 * - Postconditions (后置条件):
 *    1. 成功时：返回指向新 WP 的指针，且该节点已位于 active list 头部 (LIFO)；
 *    2. 失败时：若因表达式无效导致失败，返回 NULL，且系统状态（NO、链表）保持原样；
 *    3. 资源完整性：除了新插入的节点，active list 中原有节点的顺序和数据不被破坏。
 *
 * - Invariants (不变式):
 *    1. 唯一性：在任何时刻，active list 中不存在两个 NO 相同的监视点；
 *    2. 单调性：监视点的 NO 随创建时间严格单调递增，即便中间有节点被释放，NO 也不复用；
 *    3. 优先级：active list 的首个节点永远是时间戳上最新创建的监视点。
 *    4. 分层隔离：调用者（如 CPU 循环）仅通过暴露的接口感知变化，不应直接操作 WP 链表指针。
 *
 * @param expr_str 用户输入的监视表达式字符串（需符合表达式求值器语法）
 * @return WP* 成功则返回新节点指针；若表达式非法则返回 NULL；若资源耗尽则触发 assert。
 */
WP* new_wp(const char* expr_str) {
  assert(expr_str != NULL);                           // 非空表达式报错
  assert(strlen(expr_str) < WATCHPOINT_EXPR_MAX_LEN); // 表达式过长报错
  assert(wp_eval != NULL);                            // 未设置求值函数报错

  word_t init_value;
  bool ok = wp_eval(expr_str, &init_value);
  if (!ok) {
    return NULL;
  }

  WP* wp = allocate_free_wp();
  assert(wp != NULL);                                 // 空闲池耗尽报错

  wp->last_value = init_value;
  wp->NO = next_wp_no++;
  wp->enabled = true;
  strcpy(wp->expr_str, expr_str);

  insert_active_list_head(wp);
  return wp;
}



void free_wp(WP *wp) {
  // 1. 简化的移除逻辑：假设 wp 总是 head
  if (head == wp) {
    head = head->next;
  } else {
    WP *p = head;
    while (p != NULL && p->next != wp) {
      p = p->next;
    }

    assert(wp != NULL);
    p->next = wp->next;
  }

  // 2. 归还到 free_ 列表
  wp->next = free_;
  free_ = wp;
}

/**
 * @brief
 *  在指令边界上检测监视点状态变化，并收集变化结果。
 *
 * @details
 * Contract:
 *
 *  - When:
 *      - 在每条指令执行完成后被调用一次。
 *
 *  - Behavior:
 *      1. 遍历当前所有「启用的」监视点（enabled == true），
 *         遍历顺序为“最近创建的监视点优先”（LIFO 顺序）；
 *      2. 对每个启用的监视点，重新求值其表达式；
 *      3. 将本次求值结果与该监视点记录的 last_value 进行比较；
 *      4. 若值发生变化：
 *          - 将该监视点加入本次 diff 的变化列表；
 *      5. 在完成比较后，更新该监视点的 last_value 为本次求值结果；
 *      6. disabled 的监视点在本次 diff 中：
 *          - 不参与表达式求值；
 *          - 不更新其内部状态。
 *
 *  - Postcondition:
 *      - triggered:
 *          - 若本次 diff 中至少有一个监视点发生变化，则为 true；
 *          - 否则为 false。
 *      - count:
 *          - 表示本次 diff 中发生变化的监视点数量。
 *      - list:
 *          - 指向 watchpoint 层内部维护的只读数组，
 *            该数组包含所有发生变化的监视点指针；
 *          - 数组元素顺序与遍历顺序一致（最近创建优先）；
 *          - 该指针在下一次调用 watchpoint_diff_and_collect 之前保持有效；
 *          - 所有权始终归 watchpoint 子系统所有，调用者不得释放或修改。
 *
 *  - Invariant:
 *      - 本函数不负责任何输出、停机或控制流决策；
 *      - CPU / sdb / diff 上层仅通过返回值感知变化结果，
 *        不依赖 watchpoint 的内部存储结构；
 *      - watchpoint 的内部链表结构在本函数执行过程中保持一致。
 *      - 调用者负责确保在调用本函数前已正确注册求值函数 wp_eval。
 *      - 默认表达式合法且求值成功，求值失败视为程序错误。
 *      - head 指向的链表结构是按照最新创建的监视点优先排列的。
 *
 * @return
 *  WatchpointChanges
 *      - 描述本次指令执行后监视点系统观测到的所有状态变化。
 */
WatchpointChanges watchpoint_diff_and_collect(void) {
  assert(wp_eval != NULL);  // 未设置求值函数视为程序错误
  static WatchpointChange changes_buffer[NR_WP];
  int change_count = 0;

  WP *cur = head;
  while (cur != NULL) {
    if (!cur->enabled) {
      cur = cur->next;
      continue;
    }

    // 计算新值
    word_t new_value;
    bool ok = wp_eval(cur->expr_str, &new_value);
    assert(ok);  // 求值失败视为程序错误

    // 比较并记录变化
    if (new_value != cur->last_value) {
      assert(change_count < NR_WP);  // 变化数量不应超过监视点总数
      changes_buffer[change_count].wp = cur;
      changes_buffer[change_count].old_value = cur->last_value;
      changes_buffer[change_count].new_value = new_value;
      change_count++;
    }

    // 更新 last_value
    cur->last_value = new_value;
    cur = cur->next;
  }

  WatchpointChanges result;
  result.count = change_count;
  result.changes = changes_buffer;
  return result;
}

void wp_format(const WP *wp, char *buf, size_t len) {
  const int EXPR_COL_WIDTH = 24;

  char expr_buf[EXPR_COL_WIDTH + 1];

  size_t expr_len = strlen(wp->expr_str);
  if (expr_len <= EXPR_COL_WIDTH) {
    snprintf(expr_buf, sizeof(expr_buf), "%.*s", (int)expr_len, wp->expr_str);
  } else {
    snprintf(expr_buf, sizeof(expr_buf), "%.*s...",
             EXPR_COL_WIDTH - 3, wp->expr_str);
  }

  snprintf(buf, len,
           "%-4d  %-3c  %-24s  0x%x",
           wp->NO,
           wp->enabled ? 'y' : 'n',
           expr_buf,
           wp->last_value);
}

/* visitor：打印单个 watchpoint */
static void wp_display_visitor(const WP *wp, void *user) {
  (void)user;

  char line[128];
  wp_format(wp, line, sizeof(line));
  puts(line);
}

/* 对外接口：info w */
void watchpoints_display(void) {
  printf("Num   Enb  Expression        Value\n");
  printf("----------------------------------------\n");

  wp_foreach_active(wp_display_visitor, NULL);
}

bool wp_delete_by_no(int no) {
  WP *cur = head;
  while (cur != NULL) {
    if (cur->NO == no) {
      free_wp(cur);
      return true;
    }
    cur = cur->next;
  }
  return false;
}