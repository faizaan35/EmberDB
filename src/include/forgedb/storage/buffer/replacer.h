#pragma once

#include "forgedb/common/config.h"

namespace forgedb {

class Replacer {
public:
    Replacer() = default;
    virtual ~Replacer() = default;

    virtual bool Victim(frame_id_t* frame_id) = 0;
    virtual void Pin(frame_id_t frame_id) = 0;
    virtual void Unpin(frame_id_t frame_id) = 0;
    virtual size_t Size() = 0;
};

} // namespace forgedb
