#pragma once

#include "import.h"
#include <cstring>

inline int sdk_http_status(const struct shared_ptr* http) {
    if (!http || !http->obj) return 0;
    // Verified by the offline guest fixture with HTTPMessage::responseWithStatus.
    int status = 0;
    memcpy(&status, static_cast<const char*>(http->obj) + 128, sizeof(status));
    return status >= 100 && status <= 599 ? status : 0;
}
