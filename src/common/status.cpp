#include "forgedb/common/status.h"

namespace forgedb {

std::string Status::ToString() const {
    if (ok()) {
        return "OK";
    }

    std::string prefix;
    switch (code_) {
        case StatusCode::NotFound:
            prefix = "NotFound: ";
            break;
        case StatusCode::AlreadyExists:
            prefix = "AlreadyExists: ";
            break;
        case StatusCode::InvalidArgument:
            prefix = "InvalidArgument: ";
            break;
        case StatusCode::IOError:
            prefix = "IOError: ";
            break;
        case StatusCode::Corruption:
            prefix = "Corruption: ";
            break;
        case StatusCode::InvalidSyntax:
            prefix = "InvalidSyntax: ";
            break;
        case StatusCode::TypeMismatch:
            prefix = "TypeMismatch: ";
            break;
        case StatusCode::TransactionError:
            prefix = "TransactionError: ";
            break;
        case StatusCode::InternalError:
            prefix = "InternalError: ";
            break;
        default:
            prefix = "Unknown: ";
            break;
    }
    return prefix + message_;
}

} // namespace forgedb
