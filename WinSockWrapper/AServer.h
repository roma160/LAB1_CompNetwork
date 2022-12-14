#pragma once

#include <mutex>
#include <queue>
#include <set>
#include <thread>

#include "APeer.h"
#include "WinSockWrapper.h"

class AServer
{
protected:
	std::atomic<bool> working;
	SOCKET listenSocket = INVALID_SOCKET;

	class Peer : public APeer
	{
	private:
		AServer* server;
		std::thread* thread;

		std::unique_ptr<ASockResult> loopEnd(std::unique_ptr<ASockResult> reason) override;
		std::unique_ptr<ASockResult> loopStart(std::string& receiveBuffer, std::string& sendBuffer) override;

	public:
		Peer(AServer* server, SOCKET socket);
		virtual ~Peer();

		HandlerResponse messageHandler(AContext** context, std::string message) override;
	};

	std::set<Peer*> peers;
	std::mutex finishedPeersMutex;
	std::queue<Peer*> finishedPeers;

	static void resolveAddress(const std::string& address, const std::string& port, addrinfo*& result);

public:
	AServer(std::string address, std::string port);

	void loop();

	virtual APeer::HandlerResponse messageHandler(APeer::AContext** context, std::string message) = 0;
};

