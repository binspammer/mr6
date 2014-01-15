// Multicasting

#include "mrt.hpp"

#include <linux/mroute.h>
#include <linux/igmp.h>
#include <netinet/igmp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>

using namespace std;

string inet_intoa(uint32_t addr)
{
	struct in_addr inAddr;
	inAddr.s_addr = addr;
	return string(inet_ntoa(inAddr));
}

MRTctrl::MRTctrl() try
: parent_("eth0")
, mrtSock_(-1)
, igmpSock_(-1)
{
	mrtSock_ = socket(AF_INET, SOCK_RAW, IPPROTO_IGMP);
	int enable (1);
	if (setsockopt(mrtSock_, IPPROTO_IP, MRT_INIT, &enable, sizeof(enable)) < 0)
		throw("Enable multicast forwarding error. Another mrouted instance may be running already.");
	struct vifctl vc;
	bzero(&vc, sizeof(vc));
	//	vc.vifc_flags |= VIFF_REGISTER;
	vc.vifc_flags = 0;
	vc.vifc_threshold = 1;
	vc.vifc_rate_limit = 0;
	// get the list of network interfaces
	struct ifaddrs *ifaddr;
	if (getifaddrs(&ifaddr) < 0)
		throw("Get network interfaces list error");
	// initialise virtual network interfaces to be used in multicast routing
	vifi_t vifi(0);
	for (struct ifaddrs* ifa(ifaddr); NULL != ifa; ifa = ifa->ifa_next) {
		// skip empty and local interfraces
		if (NULL == ifa->ifa_addr || 0 == memcmp(ifa->ifa_name,"lo", 2))
			continue;
		if (AF_INET == ifa->ifa_addr->sa_family) {
			in_addr addr (((sockaddr_in*)ifa->ifa_addr)->sin_addr);
			if (ifa->ifa_name == parent_)
				iifAddr_ = addr;
			vc.vifc_vifi = vifi;
			memcpy(&vc.vifc_lcl_addr, &addr, sizeof(vc.vifc_lcl_addr));
			if (setsockopt(mrtSock_, IPPROTO_IP, MRT_ADD_VIF, &vc, sizeof(vc)) < 0)
				continue;
			vifs_[ifa->ifa_name] = vifi;
			++vifi;
		}
	}
//	sleep(20);
	struct sockaddr_in uplink;
	bzero(&uplink, sizeof(uplink));
	uplink.sin_family = AF_INET;
	uplink.sin_port = htons(2152);
	uplink.sin_addr = iifAddr_;
	igmpSock_ = socket(AF_INET, SOCK_DGRAM, 0);
	if (bind(igmpSock_, (struct sockaddr *)&uplink, sizeof(uplink)) < 0)
		throw("Open IGMP socket error.");
}
catch(char const* e)
{
	cerr << e <<endl;
}

MRTctrl::~MRTctrl()
{
	// clear virtual multicast interfaces
	for (vifi_t vifi(0), maxvifs(vifs_.size()); vifi < maxvifs; ++vifi)
		setsockopt(mrtSock_, IPPROTO_IP, MRT_DEL_VIF, &vifi, sizeof(vifi));
	int enable(0); // disable multicast forwarding in the kernel
	if (setsockopt(mrtSock_, IPPROTO_IP, MRT_DONE, &enable, sizeof(enable)) < 0)
		throw("Close mrouted socket error");
	close(mrtSock_);
}

void MRTctrl::AddMFC(const string& parent, uint32_t origin, uint32_t mcastgrp)
{
	if (!insert_uniq<uint32_t,uint32_t>(mfcs_, MFC::value_type(mcastgrp, origin)))
		return;
	struct mfcctl mc;
	bzero(&mc, sizeof(mc));
	memcpy(&mc.mfcc_origin, &origin, sizeof(mc.mfcc_origin));
	memcpy(&mc.mfcc_mcastgrp, &mcastgrp, sizeof(mc.mfcc_mcastgrp));
	mc.mfcc_parent = vifs_[parent];
	for (int vifi(0), maxvifs(vifs_.size()); vifi < maxvifs; ++vifi)
		if (vifi != vifs_[parent])
			mc.mfcc_ttls[vifi] = 2;
	if (setsockopt(mrtSock_, IPPROTO_IP, MRT_ADD_MFC, &mc, sizeof(mc)) < 0)
		throw("Add multicast route error");
	std::cout <<"added mroute (" << inet_intoa(origin)  <<"," << inet_intoa(mcastgrp) <<")" <<std::endl;
}

void MRTctrl::DeleteMFC(const string& parent, uint32_t origin, uint32_t mcastgrp)
{
	MFC::iterator mfc = find_pair<uint32_t,uint32_t>(mfcs_, MFC::value_type(mcastgrp, origin));
	if (mfcs_.end() == mfc)
		return;
	mfcs_.erase(mfc);
	struct mfcctl mc;
	bzero(&mc, sizeof(mc));
	memcpy(&mc.mfcc_origin, &origin, sizeof(mc.mfcc_origin));
	memcpy(&mc.mfcc_mcastgrp, &mcastgrp, sizeof(mc.mfcc_mcastgrp));
	mc.mfcc_parent = vifs_[parent];
	for (int vifi(0), maxvifs(vifs_.size()); vifi < maxvifs; ++vifi)
		mc.mfcc_ttls[vifi] = 0;
	if (setsockopt(mrtSock_, IPPROTO_IP, MRT_DEL_MFC, &mc, sizeof(mc)) < 0)
		throw("Delete multicast route error");
	std::cout <<"deleted mroute (" << inet_intoa(origin)  <<"," << inet_intoa(mcastgrp) <<")" <<std::endl;
}

void MRTctrl::DeleteMFC(const string& parent, uint32_t mcastgrp)
{
	std::pair<MFC::iterator, MFC::iterator> origins = mfcs_.equal_range(mcastgrp);
	for (MFC::iterator it(origins.first); it != origins.second; ++it) {
		struct mfcctl mc;
		bzero(&mc, sizeof(mc));
		memcpy(&mc.mfcc_origin, &(it->second), sizeof(mc.mfcc_origin));
		memcpy(&mc.mfcc_mcastgrp, &mcastgrp, sizeof(mc.mfcc_mcastgrp));
		mc.mfcc_parent = vifs_[parent];
		for (int vifi(0), maxvifs(vifs_.size()); vifi < maxvifs; ++vifi)
			mc.mfcc_ttls[vifi] = 0;
		if (setsockopt(mrtSock_, IPPROTO_IP, MRT_DEL_MFC, &mc, sizeof(mc)) < 0)
			throw("Delete multicast route error");
		std::cout <<"deleted mroute (" << inet_intoa(it->second)  <<"," << inet_intoa(mcastgrp) <<")" <<std::endl;
	}
	mfcs_.erase(mcastgrp);
}

void MRTctrl::Join(uint32_t mcastgrp)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		throw ("Opening datagram socket error");
	// Enable SO_REUSEADDR to allow multiple instances of this
	// application to receive copies of the multicast datagrams
	int reuse(1);
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&reuse, sizeof(reuse)) < 0)
		throw("Setting SO_REUSEADDR error");
	// Join the multicast group
	struct ip_mreq recv;
	recv.imr_multiaddr.s_addr = mcastgrp;
	recv.imr_interface = iifAddr_;
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&recv, sizeof(recv)) < 0)
		throw("Join multicast group error");
	cout << " joined " << inet_intoa(mcastgrp) << " group" <<endl;
	msockets_[mcastgrp] = fd;
}

void MRTctrl::Leave(uint32_t mcastgrp)
{
	ForwardSock::iterator it (msockets_.find(mcastgrp)), end (msockets_.end());
	if (end == it)
		return;
	close(it->second);
	msockets_.erase(it);
	cout << " left " << inet_intoa(mcastgrp) << " group" <<endl;
}

void MRTctrl::handleIGMP(int igmpSock) try
{
	const int size(8096);
	static u_int8_t buf[size];
	if (read(igmpSock, &buf, size) < 0)
		return;
	const struct ip *ip = reinterpret_cast <const struct ip*> (buf);
	int lenght (ip->ip_hl << 2);
	const struct igmp *igmpv2 = reinterpret_cast <const struct igmp*>((buf + lenght));
	const struct igmpv3_report *igmpv3 = reinterpret_cast <const struct igmpv3_report*>((buf + lenght));
	switch (igmpv3->type)
	{
	case IGMPV3_HOST_MEMBERSHIP_REPORT:
	{
		const struct igmpv3_grec& grec = igmpv3->grec[0];
		const uint32_t& group = grec.grec_mca;
		switch (grec.grec_type)
		{
		case IGMPV3_CHANGE_TO_EXCLUDE: // join
			Join(group);
			break;
		case IGMPV3_CHANGE_TO_INCLUDE: // leave
			Leave(group);
			break;
		default:
			cerr << "ignoring unknown IGMPv3 message type " << grec.grec_type <<endl;
		}
		break;
	}
	case IGMP_V1_MEMBERSHIP_REPORT:
	case IGMP_V2_MEMBERSHIP_REPORT:
		Join(igmpv2->igmp_group.s_addr);
		break;
	case IGMP_V2_LEAVE_GROUP:
		Leave(igmpv2->igmp_group.s_addr);
		break;
	default:
		cerr << "ignoring unknown IGMP message type " << igmpv3->type <<endl;
	}
}
catch(exception& e)
{
	cerr << e.what() <<endl;
}

void MRTctrl::handleMcastData(int mrtSock) try
{
	static u_int8_t buf[8192];
	bzero(&buf, sizeof(buf));
	socklen_t dummy(0);
	if (recvfrom(mrtSock, buf, sizeof(buf), 0, NULL, &dummy) <0)
		return;
	struct ip* ip = (struct ip*) buf;
	u_int32_t origin(ip->ip_src.s_addr);
	u_int32_t group(ip->ip_dst.s_addr);
	AddMFC(parent_, origin, group);
}
catch(char const*e)
{
	cerr << e <<endl;
}
catch(exception& e)
{
	cerr << e.what() <<endl;
}
