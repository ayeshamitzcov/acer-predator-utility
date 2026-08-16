#pragma once

#include "acer/WmiSession.h"

namespace predator {

inline WmiSession& GetWmi() {
    static WmiSession session;
    static bool tried = false;
    if (!tried) {
        tried = true;
        session.Connect();
    }
    return session;
}

}  // namespace predator
