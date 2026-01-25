#include <assert.h>
#include <stdio.h>
#include "monitor/sdb/watchpoint.c"

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
  test_new_wp_single_creation();
  printf("TEST OK!\n");
  return 0;
}