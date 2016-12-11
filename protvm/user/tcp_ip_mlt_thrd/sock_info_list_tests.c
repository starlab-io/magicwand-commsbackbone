
#include "sock_info_list.h"

#include <check.h>
#include "suites.h"

#define ADD_TEST(x, y, z) \
	y = tcase_create(#x); \
	tcase_add_test(y, x ## _test); \
	suite_add_tcase(z, tcase) \

#define OK 0 

START_TEST(create_destroy_list_test)
{
	struct list *list_1;
	struct list *list_2;

	// Test list creation
	list_1 = create_sock_info_list();
	ck_assert(list_1 != NULL);

	// Create another 
	list_2 = create_sock_info_list();
	ck_assert(list_2 != NULL);
	ck_assert(list_2 != list_1);

	// Destroy non-existent list
	destroy_list(NULL);

	// Destroy lists one at a time
	destroy_list(&list_1);
	ck_assert(list_1 == NULL);

	destroy_list(&list_2);
	ck_assert(list_2 == NULL);
}
END_TEST


START_TEST(add_remove_sock_info_test)
{
	int           error;
	struct list  *list;
        sinfo_t       sock_info;
        /*
        sinfo_t sock_info_1;
        sinfo_t sock_info_2;
        sinfo_t sock_info_3;
        sinfo_t sock_info_4;
        */

        error = add_sock_info( NULL, &sock_info );
	ck_assert(error != OK);

	destroy_sock_info(NULL, 0);

	list = create_sock_info_list();
	ck_assert(list != NULL);
	sock_info.sockfd = 1;

	error = add_sock_info(list, &sock_info);
	ck_assert(error == OK);

	ck_assert(find_sock_info(list, sock_info.sockfd) != NULL);
	ck_assert(count_list_members(list) == 1);
	destroy_sock_info(list, sock_info.sockfd);

	ck_assert(find_sock_info(list, sock_info.sockfd) == NULL);
	ck_assert(count_list_members(list) == 0);

	destroy_list(&list);
	ck_assert(list == NULL);

}
END_TEST

Suite*
create_sock_info_list_suite(void)
{
	Suite *suite = suite_create("Sock Info List");
	TCase *tcase;

	ADD_TEST(create_destroy_list, tcase, suite);
	ADD_TEST(add_remove_sock_info, tcase, suite);
        /*
	ADD_TEST(remove_all_sock_infos, tcase, suite);
	ADD_TEST(find_sock_infos, tcase, suite);
	ADD_TEST(count_sock_infos, tcase, suite);
	ADD_TEST(list_iterator, tcase, suite);
        */
	return suite;
}
