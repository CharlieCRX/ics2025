#include <assert.h>
#include <stdio.h>
#include "monitor/sdb/watchpoint.c"


void test_new_wp_reduce_free_list() {
  init_wp_pool();

  int before = count_wp_list(free_);
  WP *wp = new_wp();
  int after = count_wp_list(free_);

  assert(wp != NULL);
  assert(before - 1 == after);
}

void test_new_wp_is_head() {
  init_wp_pool();

  WP *wp = new_wp();
  assert(wp == head);
}
int main() {
  test_new_wp_reduce_free_list();
  test_new_wp_is_head();
  printf("test framework works!\n");
  return 0;
}