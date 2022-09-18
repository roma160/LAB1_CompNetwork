#include "pch.h"
#include "APeer.h"

#include <iostream>
#include <stdexcept>

using namespace std;

DEFINE_ENUM_FLAG_OPERATORS(APeer::HandlerResponse::Flags)


// https://www.geeksforgeeks.org/pure-virtual-destructor-c/
APeer::AContext::~AContext() = default;

unique_ptr<APeer::ASockResult> APeer::contact(ContactType type, char* data, int data_len) const
{ return contact(socket, type, data, data_len); }

unique_ptr<APeer::ASockResult> APeer::contact(const SOCKET& socket, ContactType type, char* data, int data_len)
{
    int result = 0, code, i = 0;
    do
    {
        if (type == RECEIVE) result = recv(socket, data, data_len, 0);
        else if (type == SEND) result = send(socket, data, data_len, 0);
        code = WSAGetLastError();

#ifdef _DEBUG
        if (type == RECEIVE) cout << "r" << result << "\n";
        else if (type == SEND) cout << "s" << result << "\n";
#endif

        if (result == data_len) return make_unique<ASockResult>(ASockResult::OK);

        // https://learn.microsoft.com/uk-ua/windows/win32/winsock/windows-sockets-error-codes-2?redirectedfrom=MSDN
        if (result == 0 || result == WSAESHUTDOWN || code == WSAECONNRESET)
            return make_unique<ASockResult>(ASockResult::SHUTDOWN);

        i++;
    } while (i < RETRY_NUM);
    return make_unique<ErrSockResult>(code);
}

unique_ptr<APeer::ASockResult> APeer::loopIterBody(string& receiveBuffer, string& sendBuffer)
{
    unique_ptr<ASockResult> buff_res;

    // Receiving phase
    if ((buff_res = contact(RECEIVE, (char*) receiveBuffer.c_str(), 1)
        )->type != ASockResult::OK || (unsigned char)receiveBuffer[0] < 1)
        return buff_res;

    receiveBuffer.resize((unsigned char) receiveBuffer[0]);
    if ((buff_res = contact(RECEIVE, (char*) receiveBuffer.c_str(), (unsigned char) receiveBuffer[0])
        )->type != ASockResult::OK)
        return buff_res;

    // Processing phase
    HandlerResponse response = messageHandler(context, receiveBuffer);
    sendBuffer = move(response.message);
    if (response.flags & HandlerResponse::SHUTDOWN)
    {
        // In case, we need to shut down the connection
        if (response.flags & HandlerResponse::MESSAGE)
        {
            sendBuffer = (char) sendBuffer.size() + sendBuffer;
            if ((buff_res = contact(SEND, (char*) sendBuffer.c_str(), sendBuffer.size())
                )->type != ASockResult::OK)
                return buff_res;
        }
        return make_unique<ASockResult>(ASockResult::SHUTDOWN);
    }

    // Answering phase
    sendBuffer = (char) sendBuffer.size() + sendBuffer;
    if ((buff_res = contact(SEND, (char*) sendBuffer.c_str(), sendBuffer.size())
        )->type != ASockResult::OK)
        return buff_res;

    return buff_res;
}

void APeer::loopEnd(std::unique_ptr<ASockResult> reason)
{
    // Shutting down the connection, since we're done
    if (shutdown(socket, SD_SEND) == SOCKET_ERROR) {
        throw runtime_error("shutdown failed with error: " + to_string(WSAGetLastError()));
    }
    closesocket(socket);
}

void APeer::initNonSocket()
{
    context = new AContext*(nullptr);
    working = false;
}

APeer::APeer(SOCKET socket): socket(socket)
{
	initNonSocket();
}

APeer::APeer(std::string connectionAddress, std::string connectionPort)
{
    WinSockWrapper::ensureInit();
    socket = WinSockWrapper::getSocketForAddress(move(connectionAddress), move(connectionPort));
    initNonSocket();
}

APeer::~APeer() { delete (*context); }

void APeer::loop()
{
    working = true;

    unique_ptr<ASockResult> buffRes;

    string receiveBuffer, sendBuffer;
    receiveBuffer.reserve(MAX_COMMAND_SIZE);
    sendBuffer.reserve(MAX_COMMAND_SIZE);
    receiveBuffer.resize(START_MESSAGE_SIZE);

    // Starting
    if ((buffRes = loopStart(receiveBuffer, sendBuffer)
        )->type != ASockResult::OK)
    {
        loopEnd(move(buffRes));
        return;
    }

    // The Loop
    while(working)
    {
	    if((buffRes = loopIterBody(receiveBuffer, sendBuffer)
            )->type != ASockResult::OK)
            break;
    }

    // The ending
    loopEnd(move(buffRes));
}

APeer::HandlerResponse::HandlerResponse(std::string message):
	message(message), flags(MESSAGE) {}

APeer::HandlerResponse::HandlerResponse(bool is_shutdown, std::string message): flags(EMPTY)
{
    if (!message.empty())
    {
        this->message = message;
        flags |= MESSAGE;
    }
    if (is_shutdown) flags |= SHUTDOWN;
}

