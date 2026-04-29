#ifndef LOGGER_H
#define LOGGER_H

#include "../../include/common.h"

void write_global_log(const char *message);
void write_session_log(const int session_id, const char *message);

#endif