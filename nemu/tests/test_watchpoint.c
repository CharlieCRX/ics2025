#include <assert.h>
#include <stdio.h>
#include "monitor/sdb/watchpoint.c"
#include "assert_test_helper.h"

#ifdef ENABLE_ASSERT_TEST
// 程序员违约测试
void test_new_wp_null_expr_should_assert() {
  init_wp_pool();
  EXPECT_ASSERT(
    new_wp(NULL);
  );
}

void test_new_wp_expr_too_long_should_assert() {
  init_wp_pool();

  char buf[1024];
  memset(buf, 'a', sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';

  EXPECT_ASSERT(
    new_wp(buf);
  );
}


void test_allocate_wp_when_pool_exhausted_should_assert() {
  init_wp_pool();

  for (int i = 0; i < NR_WP; i++) {
    allocate_free_wp();
  }

  EXPECT_ASSERT(
    new_wp("1 + 2");
  );
}

#endif


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

// --- 测试 2：编号唯一性 ---
void test_new_wp_unique_no() {
  WP* wp1 = new_wp("expr1");
  WP* wp2 = new_wp("expr2");
  assert(wp1->NO != wp2->NO);
}

int main() {
  #ifdef ENABLE_ASSERT_TEST
  printf("Running ASSERT tests...\n");
  // ===== 程序员违约测试 =====
  test_new_wp_null_expr_should_assert();
  test_new_wp_expr_too_long_should_assert();
  test_allocate_wp_when_pool_exhausted_should_assert();
  printf("ASSERT tests passed!\n");
  #endif

  // ===== 辅助结构测试 =====
  test_insert_active_list_head_LIFO();

  // ===== 正常语义测试 =====
  test_new_wp_single_creation();
  test_new_wp_unique_no();

  printf("ALL TESTS PASSED!\n");
  return 0;
}