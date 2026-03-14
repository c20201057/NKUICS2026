#include "monitor/watchpoint.h"
#include "monitor/expr.h"

#define NR_WP 32

static WP wp_pool[NR_WP];
static WP *head, *free_;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = &wp_pool[i + 1];
    wp_pool[i].expr[0] = '\0';
    wp_pool[i].last_val = 0;
  }
  wp_pool[NR_WP - 1].next = NULL;

  head = NULL;
  free_ = wp_pool;
}

WP* new_wp(char *expr_str) {
  bool success = true;
  uint32_t value = 0;
  WP *wp = NULL;

  Assert(expr_str != NULL, "Watchpoint expression can not be NULL");
  while (*expr_str == ' ' || *expr_str == '\t') {
    expr_str ++;
  }
  Assert(*expr_str != '\0', "Watchpoint expression can not be empty");
  Assert(free_ != NULL, "No free watchpoints. Increase NR_WP if necessary.");

  value = expr(expr_str, &success);
  Assert(success, "Invalid watchpoint expression: %s", expr_str);

  wp = free_;
  free_ = free_->next;

  wp->next = head;
  head = wp;

  strncpy(wp->expr, expr_str, sizeof(wp->expr) - 1);
  wp->expr[sizeof(wp->expr) - 1] = '\0';
  wp->last_val = value;

  printf("Watchpoint %d: %s\n", wp->NO, wp->expr);
  printf("Value = 0x%08x (%u)\n", wp->last_val, wp->last_val);

  return wp;
}

bool free_wp(int no) {
  WP *prev = NULL;
  WP *cur = head;

  while (cur != NULL) {
    if (cur->NO == no) {
      if (prev == NULL) {
        head = cur->next;
      }
      else {
        prev->next = cur->next;
      }

      cur->next = free_;
      cur->expr[0] = '\0';
      cur->last_val = 0;
      free_ = cur;

      printf("Watchpoint %d deleted\n", no);
      return true;
    }

    prev = cur;
    cur = cur->next;
  }

  printf("No watchpoint number %d\n", no);
  return false;
}

void print_watchpoints(void) {
  WP *cur = head;

  if (cur == NULL) {
    printf("No watchpoints.\n");
    return;
  }

  printf("Num\tValue\t\tWhat\n");
  while (cur != NULL) {
    printf("%d\t0x%08x\t%s\n", cur->NO, cur->last_val, cur->expr);
    cur = cur->next;
  }
}

bool check_watchpoints(void) {
  bool triggered = false;
  WP *cur = head;

  while (cur != NULL) {
    bool success = true;
    uint32_t new_val = expr(cur->expr, &success);
    Assert(success, "Watchpoint %d has an invalid expression: %s", cur->NO, cur->expr);

    if (new_val != cur->last_val) {
      printf("Watchpoint %d: %s\n", cur->NO, cur->expr);
      printf("Old value = 0x%08x (%u)\n", cur->last_val, cur->last_val);
      printf("New value = 0x%08x (%u)\n", new_val, new_val);
      cur->last_val = new_val;
      triggered = true;
    }

    cur = cur->next;
  }

  return triggered;
}

