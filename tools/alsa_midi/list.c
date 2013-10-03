/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*****************************************************************************
 *
 *   list_sort() adapted from linux kernel.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *****************************************************************************/

#include <assert.h>

#include "list.h"

/* list sort from Mark J Roberts (mjr@znex.org) */
void
__list_sort(
  struct list_head *head,
  int member_offset,
  int (*cmp)(void * a, void * b))
{
  struct list_head *p, *q, *e, *list, *tail, *oldhead;
  int insize, nmerges, psize, qsize, i;

  list = head->next;
  list_del(head);
  insize = 1;
  for (;;) {
    p = oldhead = list;
    list = tail = NULL;
    nmerges = 0;

    while (p) {
      nmerges++;
      q = p;
      psize = 0;
      for (i = 0; i < insize; i++) {
        psize++;
        q = q->next == oldhead ? NULL : q->next;
        if (!q)
          break;
      }

      qsize = insize;
      while (psize > 0 || (qsize > 0 && q)) {
        if (!psize) {
          e = q;
          q = q->next;
          qsize--;
          if (q == oldhead)
            q = NULL;
        } else if (!qsize || !q) {
          e = p;
          p = p->next;
          psize--;
          if (p == oldhead)
            p = NULL;
        } else if (cmp((void *)p - member_offset, (void *)q - member_offset) <= 0) {
          e = p;
          p = p->next;
          psize--;
          if (p == oldhead)
            p = NULL;
        } else {
          e = q;
          q = q->next;
          qsize--;
          if (q == oldhead)
            q = NULL;
        }
        if (tail)
          tail->next = e;
        else
          list = e;
        e->prev = tail;
        tail = e;
      }
      p = q;
    }

    tail->next = list;
    list->prev = tail;

    if (nmerges <= 1)
      break;

    insize *= 2;
  }

  head->next = list;
  head->prev = list->prev;
  list->prev->next = head;
  list->prev = head;
}

struct test_list_el {
  int value;
  struct list_head test_list_node;
};

int test_list_sort_comparator(struct test_list_el * e1, struct test_list_el * e2)
{
  return e1->value - e2->value;
}

void test_list_sort(void)
{
  struct list_head test_list;
  struct test_list_el *el, *next;
  struct test_list_el te1 = {.value = 1};
  struct test_list_el te2 = {.value = 2};
  struct test_list_el te3 = {.value = 3};
  struct test_list_el te4 = {.value = 4};
  struct test_list_el te5 = {.value = 5};
  struct test_list_el te6 = {.value = 6};
  struct test_list_el te7 = {.value = 7};

  const int expected[] = {1, 2, 3, 4, 5, 6, 7};
  int i;

  INIT_LIST_HEAD(&test_list);
  list_add_tail(&te2.test_list_node, &test_list);
  list_add_tail(&te6.test_list_node, &test_list);
  list_add_tail(&te4.test_list_node, &test_list);
  list_add_tail(&te5.test_list_node, &test_list);
  list_add_tail(&te7.test_list_node, &test_list);
  list_add_tail(&te1.test_list_node, &test_list);
  list_add_tail(&te3.test_list_node, &test_list);

  list_sort(&test_list, struct test_list_el, test_list_node, test_list_sort_comparator);

  i = 0;
  list_for_each_entry_safe(el, next, &test_list, test_list_node) {
    assert(el->value == expected[i]);
    i++;
  }
}
