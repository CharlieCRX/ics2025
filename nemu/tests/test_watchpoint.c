#include <assert.h>
#include <stdio.h>

void init_wp_pool(void);

int main() {
  init_wp_pool();
  assert(1);
  printf("test framework works!\n");
  return 0;
}