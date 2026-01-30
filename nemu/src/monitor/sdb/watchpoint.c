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

#include "sdb.h"

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

typedef struct {
  bool triggered;                  // 是否至少有一个监视点触发
  int  count;                      // 触发监视点数量
  const WP *list;                  // 触发监视点只读信息数组
} WatchpointDiffResult;

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
  Log("Created new watchpoint NO=%d for expr='%s' with initial value=" FMT_WORD,
      wp->NO, wp->expr_str, wp->last_value);
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
 * @brief 检查当前指令执行后监视点是否触发，并收集触发信息
 *
 * @details
 * Contract:
 *  - When: 在每条指令执行完成后被调用
 *  - Behavior:
 *      1. 遍历 watchpoint 系统中所有当前启用的监视点，
 *         按照“最近创建优先”顺序处理；
 *      2. 对每个监视点，重新求值其表达式，
 *         并与上一次记录的值比较；
 *      3. 收集所有值发生变化的监视点信息。
 *  - Postcondition:
 *      - triggered: 如果至少有一个监视点触发，则 true，否则 false
 *      - count: 触发的监视点数量
 *      - list: 包含触发监视点的只读信息，顺序按最近创建优先
 *  - Invariant:
 *      - CPU / diff 层不关心内部存储结构
 *      - 内部遍历顺序与触发逻辑在 watchpoint 层保持一致
 *
 * @return WatchpointDiffResult 触发信息结构
 */
WatchpointDiffResult watchpoint_diff_and_collect(void);