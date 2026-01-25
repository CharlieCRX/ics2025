#include <assert.h>
#include <stdio.h>
#include "monitor/sdb/watchpoint.c"

// 链表辅助函数测试
void test_insert_active_list_head_LIFO() {
  init_wp_pool(); // 清空 active list 和 free list

  // 分配节点（这里可以直接从 pool 拿，不必初始化 expr）
  WP* wp1 = allocate_free_wp();
  WP* wp2 = allocate_free_wp();
  WP* wp3 = allocate_free_wp();

  // 插入 active list
  insert_active_list_head(wp1);
  insert_active_list_head(wp2);
  insert_active_list_head(wp3);

  // 遍历 active list 收集顺序
  WP* order[10];
  int idx = 0;
  for (WP* cur = head; cur != NULL; cur = cur->next) {
    order[idx++] = cur;
  }

  // 验证 LIFO
  assert(idx == 3);
  assert(order[0] == wp3); // 最近插入
  assert(order[1] == wp2);
  assert(order[2] == wp1); // 最早插入
}


// 测试不包括计算校验表达式的值
// --- 测试 1：创建单个监视点成功 ---
void test_new_wp_single_creation() {
  init_wp_pool();
  int before = count_enabled_wp();
  WP* wp = new_wp("1 + 2");
  int after = count_enabled_wp();
  assert(wp != NULL);
  assert(wp->enabled == true);
  assert(strcmp(wp->expr_str, "1 + 2") == 0);
  assert(wp->NO >= 0 && wp->NO < NR_WP);
  assert(before + 1 == after);
}

int main() {
  // 辅助函数测试
  test_insert_active_list_head_LIFO();

  // WP 功能测试
  test_new_wp_single_creation();
  printf("TEST OK!\n");
  return 0;
}