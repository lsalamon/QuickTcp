#pragma once

#include "quicktcp/client/Platform.h"
#include "quicktcp/client/IAuthenticator.h"
#include "quicktcp/client/ServerInfo.h"
#include "quicktcp/client/PendingRequest.h"

#include <async_cpp/async/Async.h>
#include <boost/asio.hpp>
#include <condition_variable>
#include <future>
#include <queue>

namespace quicktcp {
namespace client {

class IAuthenticator;
template<class T> class IProcessor;

template<class TRESULT = utilities::ByteStream>
class Client : public std::enable_shared_from_this<Client<TRESULT>> {
public:
    Client(std::shared_ptr<boost::asio::io_service>,
        const ServerInfo& info,
        std::shared_ptr<IProcessor<TRESULT>> processor, 
        const size_t recvBufferSize = 2048,
        std::shared_ptr<IAuthenticator> authenticator = std::shared_ptr<IAuthenticator>());
    virtual ~Client();

    std::future<async_cpp::async::AsyncResult<TRESULT>> request(std::shared_ptr<utilities::ByteStream> stream);
    void connect();
    void disconnect();
    void waitForConnection();

    size_t bufferSize() const;
    bool isConnected() const;

protected:
    void authenticate(const boost::system::error_code& ec);
    void performRequest(std::shared_ptr<PendingRequest<TRESULT>> request);
    void onWriteComplete(std::shared_ptr<PendingRequest<TRESULT>> request, const boost::system::error_code& ec, std::size_t nbBytes);
    void onReceiveComplete(std::shared_ptr<PendingRequest<TRESULT>> request, const boost::system::error_code& ec, std::size_t nbBytes);

    std::queue<std::shared_ptr<PendingRequest<TRESULT>>> mPendingRequests;
    std::mutex mRequestsMutex;

    std::shared_ptr<IAuthenticator> mAuthenticator;
    std::shared_ptr<IProcessor<TRESULT>> mProcessor;
    ServerInfo mInfo;
    std::shared_ptr<boost::asio::ip::tcp::socket> mSocket;
    std::shared_ptr<boost::asio::io_service> mService;
    bool mIsConnected;
    std::condition_variable mConnectedSignal;
    size_t mReceiveBufferSize;
};

//inline implementations
//------------------------------------------------------------------------------
template<class TRESULT>
Client<TRESULT>::Client(std::shared_ptr<boost::asio::io_service> ioService,
    const ServerInfo& info,
    std::shared_ptr<IProcessor<TRESULT>> processor,
    const size_t recvBufferSize,
    std::shared_ptr<IAuthenticator> authenticator)
    : mInfo(info), mIsConnected(false), mService(ioService), mAuthenticator(authenticator), 
    mProcessor(processor), mReceiveBufferSize(recvBufferSize)
{
    assert(processor);
}

//------------------------------------------------------------------------------
template<class TRESULT>
Client<TRESULT>::~Client()
{
    disconnect();
}

//------------------------------------------------------------------------------
template<class TRESULT>
void Client<TRESULT>::connect()
{
    auto thisPtr = shared_from_this();
    mInfo.resolveAddress(*mService, [thisPtr, this](const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator iter) -> void {
        if(!ec)
        {
            mSocket = std::make_shared<boost::asio::ip::tcp::socket>(*mService);
            mSocket->async_connect(*iter, std::bind(&Client::authenticate, this, std::placeholders::_1));
        }
        else
        {
            mProcessor->handleErrorResolveAddress(ec.message());
        }   
    } );
}

//------------------------------------------------------------------------------
template<class TRESULT>
void Client<TRESULT>::authenticate(const boost::system::error_code& ec)
{
    if(!ec)
    {
        bool authenticated = true;
        if(mAuthenticator)
        {
            authenticated = mAuthenticator->authenticate(mSocket);
        }
        if(authenticated)
        {
            mIsConnected = true;
            mConnectedSignal.notify_all();
        }
    }
    else
    {
        mProcessor->handleErrorConnect(ec.message());
    }
}

//------------------------------------------------------------------------------
template<class TRESULT>
void Client<TRESULT>::disconnect()
{
    mIsConnected = false;
    if(mSocket)
    {
        mSocket->close();
    }
    mSocket.reset();

}

//------------------------------------------------------------------------------
template<class TRESULT>
void Client<TRESULT>::waitForConnection()
{
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    mConnectedSignal.wait(lock, [this]()->bool {
        return mIsConnected;
    } );
}

//------------------------------------------------------------------------------
template<class TRESULT>
std::future<async_cpp::async::AsyncResult<TRESULT>> Client<TRESULT>::request(std::shared_ptr<utilities::ByteStream> stream)
{
    std::future<async_cpp::async::AsyncResult<TRESULT>> result;
    if(mIsConnected)
    {
        if(!stream->hasEof())
        {
            stream->appendEof();
        }
        auto req = std::make_shared<PendingRequest<TRESULT>>(stream, mReceiveBufferSize);
        result = req->getFuture();
        std::lock_guard<std::mutex> lock(mRequestsMutex);
        if(mPendingRequests.empty())
        {
            performRequest(req);
        }
        mPendingRequests.emplace(req);
    }
    else
    {
        result = async_cpp::async::AsyncResult<TRESULT>("Client: Client is disconnected").asFulfilledFuture();
    }
    return result;
}

//------------------------------------------------------------------------------
template<class TRESULT>
void Client<TRESULT>::performRequest(std::shared_ptr<PendingRequest<TRESULT>> req)
{
    auto thisPtr = shared_from_this();
    mSocket->async_send(req->sendBuffers(), [req, thisPtr, this](const boost::system::error_code& ec, std::size_t nbBytes) -> void {
        onWriteComplete(req, ec, nbBytes);
    } );
}

//------------------------------------------------------------------------------
template<class TRESULT>
void Client<TRESULT>::onWriteComplete(std::shared_ptr<PendingRequest<TRESULT>> request, const boost::system::error_code& ec, std::size_t nbBytes)
{
    //begin read
    if(!ec)
    {
        if(request->wasSendValid(nbBytes))
        {
            auto thisPtr = shared_from_this();
            boost::asio::async_read(*mSocket, request->recvBuffers(), boost::asio::transfer_at_least(1), [request, thisPtr, this](const boost::system::error_code& ec, std::size_t nbBytes)
            {
                onReceiveComplete(request, ec, nbBytes);
            } );
        }
        else
        {
            disconnect();
            request->fail("Client: Failed to send complete message");
        }
    }
    else
    {
        disconnect();
        request->fail(ec.message());
    }
}

//------------------------------------------------------------------------------
template<class TRESULT>
void Client<TRESULT>::onReceiveComplete(std::shared_ptr<PendingRequest<TRESULT>> request, const boost::system::error_code& ec, std::size_t nbBytes)
{
    if(!ec || boost::asio::error::eof == ec.value())
    {
        if(nbBytes > 0)
        {
            request->appendData(nbBytes);
        }

        if(request->hasReceivedData())
        {
            auto thisPtr = shared_from_this();
            if(request->receivedEof() || boost::asio::error::eof == ec.value())
            {
                //request complete
                mService->post([thisPtr, this, request]()->void {
                    request->complete(mProcessor);
                } );
                std::shared_ptr<PendingRequest<TRESULT>> nextRequest;
                {
                    std::lock_guard<std::mutex> lock(mRequestsMutex);
                    mPendingRequests.pop();
                    if(!mPendingRequests.empty())
                    {
                        nextRequest = mPendingRequests.front();
                    }
                }
                if(nextRequest)
                {
                    performRequest(nextRequest);
                }
            }
            else
            {
                //read more
                boost::asio::async_read(*mSocket, request->recvBuffers(), boost::asio::transfer_at_least(1), [request, thisPtr, this](const boost::system::error_code& ec, std::size_t nbBytes)
                {
                    onReceiveComplete(request, ec, nbBytes);
                } );
            }
        }
        else
        {
            request->fail("Client: Received no data, disconnected from server");
            disconnect();
        }
    }
    else
    {
        request->fail(ec.message());
        disconnect();
    }
}

//------------------------------------------------------------------------------
template<class TRESULT>
bool Client<TRESULT>::isConnected() const
{
    return mIsConnected;
}

}
}