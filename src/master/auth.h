#ifndef AUTH_H
#define AUTH_H

#include "../../include/common.h"

int auth_user(const char *username, const char *password, const char *expected_role);
void master_admin();

#endif