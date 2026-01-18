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


void test_free_wp_returns_to_free_list() {
  init_wp_pool();

  WP *wp = new_wp();
  int count_after_new = count_wp_list(free_);

  free_wp(wp);
  
  int count_after_free = count_wp_list(free_);

  assert(count_after_new + 1 == count_after_free);
  assert(head == NULL);
}

int main() {
  test_new_wp_reduce_free_list();
  test_new_wp_is_head();
  test_free_wp_returns_to_free_list();
  printf("test framework works!\n");
  return 0;
}