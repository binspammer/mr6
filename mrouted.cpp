#include "mrt.hpp"

#include <poll.h>
#include <netinet/ip.h>

#include <iostream>

int main(int argc, char* argv[])
try {
	MRTctrl mrtd;
	struct pollfd pfd[3];

	for(;;) {
		pfd[0].fd = mrtd.getMrtSock();
		pfd[0].events = POLLIN;
		pfd[1].fd = mrtd.getIgmpSock();
		pfd[1].events = POLLIN;

		int ret =  poll(pfd, 2, -1);
		if (-1 == ret)
			continue;
		if (pfd[0].revents & POLLIN)
			mrtd.handleMcastData(pfd[0].fd);
		if (pfd[1].revents & POLLIN)
			mrtd.handleIGMP(pfd[1].fd);
	}

}
catch(const char* s)
{
	std::cerr << s <<std::endl;
}
catch(std::exception& e)
{
	std::cerr << e.what() <<std::endl;
}
