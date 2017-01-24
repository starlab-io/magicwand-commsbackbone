/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    list.c
 * @author  Mark Mason 
 * @date    4 November 2016
 * @version 0.1
 * @brief   List Abstraction 
 */

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

#include "list.h"

struct list_member {

	void                *member;
	struct list_member  *next_member;
	struct list_member  *previous_member;
};

struct list {

	struct list_member   *head;
	int                   length;
	struct list_manager  *manager;
};

//
// A dummy voip implementation to enable the 
// list manager to innocuously/safely do nothing
//
static void 
*create_voip( void );

static void  
copy_voip( void *source_voip, 
           void *dest_voip );

static bool  
compare_voips( void *voip_1, 
               void *voip_2 );

static void  
remove_voip( void *voip );

static struct list_manager voip_manager = {

	.create     = create_voip,
	.copy       = copy_voip,
	.compare    = compare_voips,
	.remove     = remove_voip
};


static void*
create_voip (void )
{
	return NULL;
}

static void
copy_voip( void *dummy_voip_1,
	   void *dummy_voip_2)
{
}

static bool
compare_voips( void *voip_1,
	       void *voip_2 )
{
	return voip_1 == voip_2;
}

static void
remove_voip( void *dummy_voip )
{
}

static struct list_member *
create_member( void )
{
	struct list_member *member;

	member = calloc( 1, sizeof(struct list_member) );

	return member;
}

static void
destroy_member( struct list_member *member )
{

	if (member != NULL)
		free(member);
}

static bool
is_empty( struct list *list )
{
	bool empty;

	assert(list != NULL);
	empty = (list->head == NULL);

        // List cannot be both empty and have non-zero length
	assert( !(empty && (list->length > 0) ));

	return empty;
}

static void
prepend_member_to_non_empty_list( struct list        *list,
	                          struct list_member *member )
{
	assert(list != NULL);
	assert(member != NULL);
	assert(!is_empty(list));

	member->next_member     = list->head;
	member->previous_member = NULL;

	list->head->previous_member = member;

	list->head = member;
}

static bool
are_list_members_equal( struct list *list,
	                void        *member_1,
	                void        *member_2 )
{
	struct list_manager *manager;

	assert(list != NULL);

	manager = list->manager;
	assert(manager != NULL);

	return manager->compare(member_1, member_2);
}

static void
destroy_list_member( struct list *list,
	             void        *member )
{
	struct list_manager *manager;

	assert(list != NULL);

	manager = list->manager;
	assert(manager != NULL);

	manager->remove(member);
}

static struct list_member *
find_member( struct list *list,
	     void        *search_member )
{
	struct list_member *member;
	bool                found_match;

	if (list == NULL)
		return NULL;

	member = list->head;

	while (member != NULL)
        {

		found_match = are_list_members_equal(
			list,
			member->member,
			search_member);

		if ( found_match )
			return member;

		member = member->next_member;
	}

	return NULL;
}

struct list *
create_list( void )
{
	struct list *list;

	list = calloc(1, sizeof(struct list));
	if (list == NULL)
		return NULL; 

	set_list_manager(list, &voip_manager);

	return list;
}

void
destroy_list( struct list **list )
{
	if (list == NULL)
		return;

	if (*list != NULL) {

		remove_all_list_members(*list);

		free(*list);

		*list = NULL;
	}

}

void set_list_manager( struct list         *list,
	               struct list_manager *manager )
{
	if (list == NULL)
		return;

	if (manager == NULL)
		return;

	assert(manager->create != NULL);
	assert(manager->copy != NULL);
	assert(manager->compare != NULL);
	assert(manager->remove != NULL);

	list->manager = manager;

}

static bool
does_member_exist( struct list *list,
	           void        *member )
{
	void *found_member;

	found_member = find_list_member(list, member);

	return (found_member != NULL);
}

static int 
add_new_list_member( struct list *list,
	             void        *new_member )
{
    struct list_member  *member;
    struct list_manager *manager;

    assert(list != NULL);
    assert(new_member != NULL);

    member = create_member();
    if (member == NULL)
        return 1; 

    manager = list->manager;
    assert(manager != NULL);

    member->member = manager->create();
    if ( member->member == NULL )
        return 1; 

    manager->copy(new_member, member->member);

    if ( is_empty(list) )
    {

        assert(member->next_member == NULL);
        assert(member->previous_member == NULL);

	list->head = member;

    } else {

        prepend_member_to_non_empty_list(list, member);
    }

    ++list->length;

    return 0;
}

static int 
modify_existing_member(	struct list *list,
	                void        *existing_member )
{
	void                *old_member;
	struct list_manager *manager;

	old_member = find_list_member(list, existing_member);
	if (old_member == NULL)
		return 1; 

	manager = list->manager;
	assert( manager != NULL );

	manager->copy( existing_member, old_member );

	return 0;
}

int
add_list_member( struct list *list,
	         void        *new_member )
{
	int error;

	if (list == NULL)
		return 1; 

	if (new_member == NULL)
		return 1; 

	if (does_member_exist(list, new_member))
		error = modify_existing_member(list, new_member);
	else
		error = add_new_list_member(list, new_member);

	return error;
}

void
remove_list_member( struct list *list,
	            void        *old_member )
{
	struct list_member *member;

	if (list == NULL)
		return;

	if (old_member == NULL)
		return;

	member = find_member(list, old_member);
	if (member == NULL)
		return;

	if ( member == list->head )
        {

		list->head = list->head->next_member;

	}
        else
        {

		member->previous_member->next_member = member->next_member;

		if (member->next_member != NULL)
			member->next_member->previous_member = member->previous_member;
	}

	destroy_list_member( list, member->member );
	destroy_member( member );

	--list->length;

        assert( !(list->length < 0) );
}

void
remove_all_list_members( struct list *list )
{
	struct list_member *current_member;
	struct list_member *next_member;

	if (list == NULL)
		return;

	current_member = list->head;

	while (current_member != NULL)
        {

		next_member = current_member->next_member;

		destroy_list_member(list, current_member->member);
		destroy_member(current_member);

		current_member = next_member;
	}

	list->head   = NULL;
	list->length = 0;
}

void *
find_list_member( struct list *list,
	          void        *search_member )
{
	struct list_member *member;

	if (list == NULL)
		return NULL;

	member = find_member(list, search_member);

	if (member == NULL)
		return NULL;

	return member->member;
}

int
count_list_members( struct list *list )
{
	if (list == NULL)
		return 0;

	return list->length;
}

list_iterator
get_list_iterator( struct list *list )
{
	if (list == NULL)
		return NULL;

	return list->head;
}

void
get_next_list_member( list_iterator  *iterator,
	              void          **next_member )
{
	struct list_member *member;

	assert(iterator != NULL);
	assert(next_member != NULL);

	member = (struct list_member *)*iterator;
	assert(member != NULL);

	*iterator = member->next_member;

	*next_member = member->member;
}

bool
is_another_list_member_available( list_iterator iterator )
{
	if (iterator == NULL)
		return false;

	return iterator != NULL;
}

