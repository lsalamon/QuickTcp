#pragma once

#include "quicktcp/server/Platform.h"

#include <future>
#include <memory>

namespace async_cpp {
namespace async {
class AsyncResult;
}
}

namespace quicktcp {

namespace utilities {
class ByteStream;
}

namespace server {

class SERVER_API IResponder {
public:       
    virtual bool authenticateConnection(std::shared_ptr<utilities::ByteStream> stream) = 0;
    virtual async_cpp::async::AsyncResult respond(std::shared_ptr<utilities::ByteStream> stream) = 0;
    virtual void handleErrorSendingResponse(async_cpp::async::AsyncResult& result) = 0;
    virtual void handleConnectionClosed() = 0;
};

//inline implementations
//------------------------------------------------------------------------------

}
}