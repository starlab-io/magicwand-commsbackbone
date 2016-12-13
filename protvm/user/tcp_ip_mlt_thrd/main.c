#define SRUNNER_TEST(suite, create, srunner, failed) \
	suite = create(); \
	srunner = srunner_create(suite); \
	srunner_run_all(srunner, CK_NORMAL); \
	failed += srunner_ntests_failed(srunner); \
	srunner_free(srunner)

#include <stdio.h>
#include <stdlib.h>
#include "suites.h"

int
main(void)
{
    Suite   *suite;
    SRunner *srunner;
    int     failed = 0;

    // Create suite for testing list dynamics 
    SRUNNER_TEST(suite, create_sock_info_list_suite, srunner, failed);

    return (!failed) ? 0 : 1;
}
