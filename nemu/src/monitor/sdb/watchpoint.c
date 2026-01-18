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

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;

  /* TODO: Add more members if necessary */

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

static int count_wp_list(WP *wp) {
  int cnt = 0;
  while (wp) {
    cnt++;
    wp = wp->next;
  }
  return cnt;
}

WP* new_wp() {
  assert(free_ != NULL);

  WP *wp = free_;
  free_ = free_->next;
  wp->next = NULL;

  wp->next = head;
  head = wp;

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