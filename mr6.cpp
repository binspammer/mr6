#include "mrt.hpp"

#include <poll.h>
#include <netinet/ip.h>

#include <iostream>

int main(int argc, char* argv[])
try {
	MRTctrl mrtd;
	mrtd.init();
	struct pollfd pfd[5];

	for(;;) {
		pfd[0].fd = mrtd.getMrt4Sock();
		pfd[0].events = POLLIN;
		pfd[1].fd = mrtd.getMrt6Sock();
		pfd[1].events = POLLIN;
		pfd[2].fd = mrtd.getIgmp4Sock();
		pfd[2].events = POLLIN;
		pfd[3].fd = mrtd.getIgmp6Sock();
		pfd[3].events = POLLIN;

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
catch(std::exception & e)
{
	std::cerr << e.what() <<std::endl;
}
