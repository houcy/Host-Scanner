#include "icmppinger.h"
#include <iostream>

using namespace std;

unsigned short IcmpPinger::sequence = 0;

void IcmpPinger::Scan(Service* service)
{
	initSocket(service);

	int iters = timeout / 10;

	for (int i = 0; i <= iters; i++)
	{
		if (i != 0)
		{
			sleep(10);
		}

		pollSocket(service, i == iters - 1);

		if (service->reason != AR_InProgress)
		{
			break;
		}
	}
}

void IcmpPinger::Scan(Services* services)
{
	for (auto service : *services)
	{
		initSocket(service);
	}

	int iters = timeout / 10;
	int left = services->size();

	for (int i = 0; i <= iters; i++)
	{
		if (i != 0)
		{
			sleep(10);
		}

		for (auto service : *services)
		{
			if (service->reason != AR_InProgress)
			{
				continue;
			}

			pollSocket(service, i == iters - 1);

			if (service->reason != AR_InProgress)
			{
				left--;
			}
		}

		if (left <= 0)
		{
			break;
		}
	}
}

void IcmpPinger::initSocket(Service* service)
{
	// parse address

	struct addrinfo hint, *info = nullptr;
	memset(&hint, 0, sizeof(struct addrinfo));
	hint.ai_family = AF_UNSPEC; // allow both v4 and v6
	hint.ai_flags = AI_NUMERICHOST; // disable DNS lookups

	getaddrinfo(service->address, "echo", &hint, &info);

	// create raw socket

	auto sock = socket(info->ai_family, SOCK_RAW, IPPROTO_ICMP);

	if (sock < 0)
	{
		// admin rights are required for raw sockets

		service->reason = AR_ScanFailed;
		return;
	}

	auto data = new IcmpScanData();
	service->data = data;
	data->socket = sock;

	service->reason = AR_InProgress;

	// max out TTL

	u_char ttl = 255;
	setsockopt(sock, IPPROTO_IP, IP_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));

	// set it to non-blocking

	u_long mode = 1;
	ioctlsocket(sock, FIONBIO, &mode);

	// construct the payload

	struct IcmpEchoRequest pkt;
	memset(&pkt, 0, sizeof(pkt));

	pkt.type = ICMP_ECHO_REQUEST;
	pkt.id   = sock;
	pkt.seq  = sequence++;

	for (int i = 0; i < sizeof(pkt.data); i++)
	{
		pkt.data[i] = rand() % 256;
	}

	pkt.checksum = checksum(reinterpret_cast<unsigned short*>(&pkt), sizeof(pkt));

	// "connect", then send probe packet
	// the connect function in case of ICMP just stores the address and port,
	// so send()/recv() will work without them, no need to store the addrinfo

	connect(sock, reinterpret_cast<struct sockaddr*>(info->ai_addr), info->ai_addrlen);
	send(sock, reinterpret_cast<char*>(&pkt), sizeof(pkt), 0);
}

void IcmpPinger::pollSocket(Service* service, bool last)
{
	if (service->reason != AR_InProgress || service->data == nullptr)
	{
		return;
	}

	auto data = reinterpret_cast<IcmpScanData*>(service->data);

	char buf[1024];
	int buflen = 1024;

	// see if any responses were received

	auto res = recv(data->socket, buf, buflen, 0);

	service->alive = res > 0;

	if (res > 0)
	{
		service->reason = AR_ReplyReceived;
	}
	else
	{
		if (last)
		{
			service->reason = AR_TimedOut;
		}
		else
		{
			return;
		}
	}

	// clean-up

	service->data = nullptr;

	closesocket(data->socket);

	delete data;
}

unsigned short IcmpPinger::checksum(unsigned short* buf, int len)
{
	unsigned int sum = 0;

	for (sum = 0; len > 1; len -= 2)
	{
		sum += *buf++;
	}

	if (len == 1)
	{
		sum += *reinterpret_cast<unsigned char*>(buf);
	}

	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += sum >> 16;

	return ~sum;
}

IcmpPinger::~IcmpPinger()
{
}