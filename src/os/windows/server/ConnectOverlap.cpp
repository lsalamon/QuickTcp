#include "os/windows/server/ConnectOverlap.h"
#include "os/windows/server/IEventHandler.h"
#include "os/windows/server/Socket.h"

#include "server/IResponder.h"

#include "utilities/ByteStream.h"

#include <minwinbase.h>
#include <iostream>
#include <sstream>

namespace quicktcp {
namespace os {
namespace windows {
namespace server {

//------------------------------------------------------------------------------
ConnectOverlap::ConnectOverlap(HANDLE mainIOCP, 
                               std::shared_ptr<IEventHandler> evHandler, std::shared_ptr<quicktcp::server::IResponder> resp,
                               std::shared_ptr<Socket> sckt, const size_t bufferSize) 
    : IOverlap(), socket(sckt), eventHandler(evHandler), responder(resp)
{
    isConnected = false;
    buffer = std::shared_ptr<char>(new char[bufferSize]);
    wsaBuffer.buf = &(buffer.get()[0]);
    wsaBuffer.len = bufferSize;

    if(mainIOCP != CreateIoCompletionPort((HANDLE)socket->socket(), mainIOCP, (ULONG_PTR)socket->socket(), 0))
    {
        int err = WSAGetLastError();
        throw(std::runtime_error("Io completion port error"));
    }
}

//------------------------------------------------------------------------------
ConnectOverlap::~ConnectOverlap()
{
    
}

//------------------------------------------------------------------------------
bool ConnectOverlap::handleIOCompletion(SOCKET sckt, const size_t nbBytes)
{
    flags = 0;
    if(WSAGetOverlappedResult(socket->socket(), this, &bytes, FALSE, &flags))
    {
        ResetEvent(hEvent);
        if(sckt != socket->socket())
        {
            //handle the connection
            handleConnection();
        }
        else
        {
            if(isConnected)
            {
                //receive when connected, no bytes means disconnect
                if(bytes == 0)
                {
                    reset();
                }
                else
                {
                    //handle read
                    transferBufferToStream(bytes);
                    eventHandler->handleResponse(socket->socket(), transferStream());
                    prepareToReceive();
                }
            }
            else
            {
                eventHandler->queueAccept(*this);
            }
        }
    }
    else
    {
        //i/o wasn't complete, see if it was due to error or buffer fulle
        int err = WSAGetLastError();
        if(WSA_IO_INCOMPLETE != err)
        {
            //TODO: log 
            std::cout << "incomplete io error" << std::endl;

        }
        else
        {
            //initial accept ex vs buffer full
            if(sckt == socket->socket())
            {
                if(isConnected)
                {
                    transferBufferToStream(bytes);
                }
                //else waiting on disconnectex
            }
        }
    }
    return false;
}

//------------------------------------------------------------------------------
void ConnectOverlap::reset()
{
    bool wasConnected = isConnected.exchange(false);
    if(wasConnected)
    {
        isConnected = false;
        socket->disconnect(*this);
        responder->connectionClosed();
    }
}

//------------------------------------------------------------------------------
void ConnectOverlap::handleConnection()
{
    isConnected = true;

    BOOL bOptVal = TRUE;
    int bOptLen = sizeof(BOOL);
    //update the socket context based on our server
    setsockopt(socket->socket(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*) &bOptVal, bOptLen);

    /**
    * When a connection is formed, we will be receiving information from that client at
    * some point in time. We queue a receive that will call back to a method that
    * can handle the connection information.
    */
    prepareToReceive();
}

//------------------------------------------------------------------------------
void ConnectOverlap::transferBufferToStream(const size_t nbBytes)
{
    assert(nbBytes > 0);
    std::shared_ptr<utilities::ByteStream> transferred(new utilities::ByteStream((void*)wsaBuffer.buf, nbBytes));
    if(nullptr == stream)
    {
        stream = std::shared_ptr<utilities::ByteStream>(transferred);
    }
    else
    {
        stream->append(transferred);
    }
}

//------------------------------------------------------------------------------
void ConnectOverlap::prepareToReceive()
{
    flags = 0;
    /**
     * We need to queue an asynchronous receive, but since we're queue'ing
     * a receive, there exists the possibility that there is data ready to be
     * received. We need to check for i/o pending if WSARecv returns SOCKET_ERROR.
     */
    while(SOCKET_ERROR != WSARecv(socket->socket(), &wsaBuffer, 1, &bytes, &flags, this, 0))
    {
        //if no bytes, connection is shutting down
        if(0 == bytes)
        {
            reset();
        }
        else
        {
            transferBufferToStream(bytes);
            eventHandler->handleResponse(socket->socket(), transferStream());
        }
    }

    int lastError = WSAGetLastError();

    if(WSA_IO_PENDING != lastError)
    {
        std::stringstream sstr;
        sstr << "Error prepping client socket for receive" << WSAGetLastError();
        //todo: logging
        std::cout << sstr << std::endl;
    }

    if(WSAECONNRESET == lastError) //client has shutdown, connection no longer needed
    {
        reset();
    }
}

}
}
}
}
