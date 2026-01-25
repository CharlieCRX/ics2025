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

int count_enabled_wp() {
  int cnt = 0;
  WP* p = head;
  while (p) {
    if (p->enabled) cnt++;
    p = p->next;
  }
  return cnt;
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
 * @brief 创建并返回一个新的监视点
 *
 * @details
 * Contract
 *
 *  - When: 在 watchpoint 系统需要创建一个新的监视点时调用
 *
 *  - Preconditions (前置条件):
 *      1. 调用者应确保至少有一个空闲监视点可用；
 *         空闲监视点由 allocate_free_wp() 提供；
 *      2. expr_str 字符串非空，长度不超过 WATCHPOINT_EXPR_MAX_LEN。
 *
 *  - Behavior (行为):
 *      1. 从空闲池中获取一个 WP 节点；
 *      2. 初始化节点：
 *         - 设置 enabled = true
 *         - 复制表达式字符串 expr_str
 *         - last_value 初始化为表达式求值模块返回的初值 - TDD 模式下不好处理
 *      3. 将节点插入 active list 的头部 (LIFO)；
 *         - 遍历 active list 时，最近创建的节点先访问
 *
 *  - Postconditions (后置条件):
 *      1. 返回值为创建好的 WP*；
 *      2. active list 链表头为新创建节点；
 *      3. 如果空闲池为空，则直接 assert；
 *      4. 其他节点保持原有顺序，未被破坏；
 *      5. 内部状态保持一致，方便后续 watchpoint_diff_and_collect 调用。
 *
 *  - Invariants (不变式):
 *      1. CPU / diff 层不直接访问链表或节点内部字段，只通过接口获取触发信息；
 *      2. LIFO 遍历语义保持不变；
 *      3. 每个节点唯一对应一个监视点编号 NO。
 *
 * @param expr_str 用户输入的监视表达式字符串
 * @return WP* 分配到 active 列表的监视点，若空闲池为空直接触发 assert
 */
WP* new_wp(const char* expr_str) {
  WP* wp = allocate_free_wp();
  assert(wp != NULL);               // 空闲池耗尽报错

  wp->enabled = true;
  strcpy(wp->expr_str, expr_str);
  // bool success = false;
  // wp->last_value = expr(wp->expr_str, &success);

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