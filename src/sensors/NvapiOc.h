#pragma once

#include <string>

namespace predator {

class NvapiOc {
public:
    bool Init();
    bool Ok() const { return ok_; }
    std::string LastError() const { return err_; }
    bool SetOffsetsMhz(int core, int memory);
    bool GetOffsetsMhz(int& core, int& memory);

private:
    bool ok_ = false;
    std::string err_;
};

}  // namespace predator
