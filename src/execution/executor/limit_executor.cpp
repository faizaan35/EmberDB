#include "emberdb/execution/executor/limit_executor.h"

namespace emberdb {

LimitExecutor::LimitExecutor(AbstractExecutor* child, size_t limit)
    : child_(child), limit_(limit) {}

void LimitExecutor::Init() {
    child_->Init();
    count_ = 0;
}

bool LimitExecutor::Next(Record* record, RID* rid) {
    if (count_ < limit_ && child_->Next(record, rid)) {
        ++count_;
        return true;
    }
    return false;
}

} // namespace emberdb
