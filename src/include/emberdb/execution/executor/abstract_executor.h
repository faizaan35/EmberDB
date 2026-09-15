#pragma once

#include "emberdb/catalog/schema.h"
#include "emberdb/storage/record/record.h"

namespace emberdb {

/**
 * AbstractExecutor is the base class for Volcano-style iterator execution operators.
 */
class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;

    virtual void Init() = 0;
    virtual bool Next(Record* record, RID* rid) = 0;
    virtual const Schema& GetOutputSchema() const = 0;
};

} // namespace emberdb
