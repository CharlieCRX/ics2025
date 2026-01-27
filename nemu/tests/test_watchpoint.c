#include <assert.h>
#include <stdio.h>
#include "monitor/sdb/watchpoint.c"
#include "assert_test_helper.h"

#ifdef ENABLE_ASSERT_TEST
// new_wp 的红灯测试清单
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


// new_wp 绿灯测试清单
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
void test_wp_no_should_be_unique_when_multiple_created() {
  init_wp_pool();

  WP* wp1 = new_wp("1");
  WP* wp2 = new_wp("2");
  WP* wp3 = new_wp("3");

  assert(wp1->NO != wp2->NO);
  assert(wp1->NO != wp3->NO);
  assert(wp2->NO != wp3->NO);
}

void test_wp_no_should_not_conflict_after_deletion() {
  init_wp_pool();

  WP* wp1 = new_wp("1");
  WP* wp2 = new_wp("2");

  int no2 = wp2->NO;

  free_wp(wp1);

  WP* wp3 = new_wp("3");

  // wp3 不得与仍存活的 wp2 冲突
  assert(wp3->NO != no2);

  // 若实现允许复用，也不能与已存活的冲突
}

void test_wp_no_should_be_stable_after_creation() {
  init_wp_pool();

  WP* wp = new_wp("1");
  int original_no = wp->NO;

  // 创建其他 wp
  new_wp("2");
  new_wp("3");

  assert(wp->NO == original_no);
}

void test_wp_no_should_not_change_due_to_lifo_insertion() {
  init_wp_pool();

  WP* wp1 = new_wp("1");
  int no1 = wp1->NO;

  new_wp("2"); // 插入头部

  assert(wp1->NO == no1);
}


void test_wp_no_should_increase_monotonically() {
  init_wp_pool();

  WP* wp1 = new_wp("1");
  WP* wp2 = new_wp("2");
  WP* wp3 = new_wp("3");

  assert(wp1->NO < wp2->NO);
  assert(wp2->NO < wp3->NO);
}

void test_wp_no_should_keep_increasing_after_deletion() {
  init_wp_pool();

  WP* wp1 = new_wp("1");
  WP* wp2 = new_wp("2");

  int no2 = wp2->NO;
  free_wp(wp1);

  WP* wp3 = new_wp("3");

  assert(wp3->NO > no2);
}

// 直接访问 head 是“可接受的过渡方案”，但不是终态
void test_new_wp_should_follow_LIFO_order() {
  init_wp_pool();

  WP* wp1 = new_wp("1");
  WP* wp2 = new_wp("2");
  WP* wp3 = new_wp("3");

  WP* order[10];
  int idx = 0;
  for (WP* cur = head; cur != NULL; cur = cur->next) {
    order[idx++] = cur;
  }

  assert(idx == 3);
  assert(order[0] == wp3);
  assert(order[1] == wp2);
  assert(order[2] == wp1);
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
  test_wp_no_should_be_unique_when_multiple_created();
  test_wp_no_should_not_conflict_after_deletion();
  test_wp_no_should_be_stable_after_creation();
  test_wp_no_should_not_change_due_to_lifo_insertion();
  test_wp_no_should_increase_monotonically();
  test_wp_no_should_keep_increasing_after_deletion();

  test_new_wp_should_follow_LIFO_order();

  printf("ALL TESTS PASSED!\n");
  return 0;
}