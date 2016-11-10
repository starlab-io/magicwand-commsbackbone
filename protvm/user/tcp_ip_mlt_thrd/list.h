
#ifndef __LIST_H__
#define __LIST_H__

#include <stdbool.h>

struct list;

typedef void* list_iterator;

/* Polymorphic setup here in the abstract list interface. 
 *  In the abstract layer, list members are voips.
 */

struct list_manager {

	void* (*create)(void);
	void (*copy)(void *src_member, void *dst_member);
	bool (*compare)(void *member_1, void *member_2);
	void (*remove)(void *member);
};

struct list * create_list(void);

void destroy_list(struct list **list);

void set_list_manager(
	struct list         *list,
	struct list_manager *manager);

int add_list_member(struct list *list, 
                    void *member);

void remove_list_member(struct list *list, void *member);

void remove_all_list_members(struct list *list);

void* find_list_member(struct list *list, void *member);

int count_list_members(struct list *list);

list_iterator get_list_iterator(struct list *list);

void get_next_list_member(list_iterator *iterator, void **member);

bool is_another_list_member_available(list_iterator iterator);

#endif

