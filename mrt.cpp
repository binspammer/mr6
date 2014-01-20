// Multicasting

#include "mrt.hpp"

#include <linux/mroute.h>
#include <linux/mroute6.h>
#include <linux/igmp.h>
#include <netinet/igmp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>

#include <utility>
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

MRTctrl::MRTctrl()
	: parent_("eth0")
	, mrt4Sock_(-1)
	, igmp4Sock_(-1)
{}

MRTctrl::~MRTctrl()
{
	// clear virtual multicast interfaces
	for (vifi_t vifi(0), maxvifs(vifs_.size()); vifi < maxvifs; ++vifi)
		setsockopt(mrt4Sock_, IPPROTO_IP, MRT_DEL_VIF, &vifi, sizeof(vifi));
	int enable(0); // disable multicast forwarding in the kernel
	if (setsockopt(mrt4Sock_, IPPROTO_IP, MRT_DONE, &enable, sizeof(enable)) < 0)
		throw("Close mrouted socket error");
	if (setsockopt(mrt6Sock_, IPPROTO_IPV6, MRT6_DONE, &enable, sizeof(enable)) < 0)
		throw("Close mrouted socket error");
	close(mrt4Sock_);
	close(mrt6Sock_);
}

void MRTctrl::init()
{
	initMRT();
	initVIFs();
	initIGMP();
}

void MRTctrl::initMRT()
{
	mrt4Sock_ = socket(AF_INET, SOCK_RAW, IPPROTO_IGMP);
	if (-1 == mrt4Sock_)
		throw("Open mrouted raw socket error.");
	mrt6Sock_ = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (-1 == mrt6Sock_)
		throw("Open mrouted IPv6 raw socket error.");
	int enable (1);
	if (setsockopt(mrt4Sock_, IPPROTO_IP, MRT_INIT, &enable, sizeof(enable)) < 0)
		throw("Enable multicast forwarding error.");
	if (setsockopt(mrt6Sock_, IPPROTO_IPV6, MRT6_INIT, &enable, sizeof(enable)) < 0)
		throw("Enable IPv6 multicast forwarding error.");
}

void MRTctrl::initVIFs()
{
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
		// IPv4
		if (AF_INET == ifa->ifa_addr->sa_family) {
			struct vifctl vc;
			bzero(&vc, sizeof(vc));
			//	vc.vifc_flags |= VIFF_REGISTER;
			vc.vifc_flags = 0;
			vc.vifc_threshold = 1;
			vc.vifc_rate_limit = 0;
			vc.vifc_vifi = vifi++;
			in_addr addr (((sockaddr_in*)ifa->ifa_addr)->sin_addr);
			memcpy(&vc.vifc_lcl_addr, &addr, sizeof(vc.vifc_lcl_addr));
			if (setsockopt(mrt4Sock_, IPPROTO_IP, MRT_ADD_VIF, &vc, sizeof(vc)) < 0)
				continue;
			if (ifa->ifa_name == parent_)
				iifAddr4_ = addr;
		}
		// IPv6
		else if (AF_INET6 == ifa->ifa_addr->sa_family) {
			struct mif6ctl mc;
			bzero(&mc, sizeof(mc));
			mc.mif6c_mifi = vifi++;
			mc.vifc_threshold = 1;
			mc.mif6c_flags = 0;
			mc.vifc_rate_limit = 0;
			mc.mif6c_pifi = if_nametoindex(parent_.c_str());
			if (setsockopt(mrt6Sock_, IPPROTO_IPV6, MRT6_ADD_MIF, (void *)&mc, sizeof(mc)) < 0)
				continue;
			if (ifa->ifa_name == parent_)
				iifAddr6_ = (((sockaddr_in6*)ifa->ifa_addr)->sin6_addr);
		}
		vifs_.push_back(make_pair(ifa->ifa_name, ifa->ifa_addr->sa_family));
	}
	freeifaddrs(ifaddr);
}

void MRTctrl::initIGMP()
{
	//	IPv4
	struct sockaddr_in uplink4;
	bzero(&uplink4, sizeof(uplink4));
	uplink4.sin_family = AF_INET;
	uplink4.sin_port = htons(2152);
	uplink4.sin_addr = iifAddr4_;
	igmp4Sock_ = socket(AF_INET, SOCK_DGRAM, 0);
	if (bind(igmp4Sock_, (struct sockaddr *)&uplink4, sizeof(uplink4)) < 0)
		throw("Open IGMP v4 socket error.");
	//	IPv6
	struct sockaddr_in6 uplink6;
	bzero(&uplink6, sizeof(uplink6));
	uplink6.sin6_family = AF_INET6;
	uplink6.sin6_port = htons(2152);
	uplink6.sin6_addr = iifAddr6_;
	igmp6Sock_ = socket(AF_INET6, SOCK_DGRAM, 0);
	// sleep(7);
	// int err = bind(igmp6Sock_, (const struct sockaddr*)&uplink6, sizeof(uplink6));
	std::cerr << "bind error: " <<err <<std::endl;
	if (0 != err)
		throw("bind IGMP v6 socket error.");
}

void MRTctrl::AddMFC(const string & parent, uint32_t origin, uint32_t mcastgrp)
{
	if (!insert_uniq<uint32_t,uint32_t>(mfcs_, MFC::value_type(mcastgrp, origin)))
		return;
	struct mfcctl mc;
	bzero(&mc, sizeof(mc));
	memcpy(&mc.mfcc_origin, &origin, sizeof(mc.mfcc_origin));
	memcpy(&mc.mfcc_mcastgrp, &mcastgrp, sizeof(mc.mfcc_mcastgrp));
	mc.mfcc_parent = 0; //vifs_[parent];
	for (int vifi(0), maxvifs(vifs_.size()); vifi < maxvifs; ++vifi)
		if (vifi != 0) //vifs_[parent])
			mc.mfcc_ttls[vifi] = 2;
	if (setsockopt(mrt4Sock_, IPPROTO_IP, MRT_ADD_MFC, &mc, sizeof(mc)) < 0)
		throw("Add multicast route error");
	std::cout <<"added (" << inet_intoa(origin)  <<"," << inet_intoa(mcastgrp) <<")" <<std::endl;
}

void MRTctrl::DeleteMFC(const string & parent, uint32_t origin, uint32_t mcastgrp)
{
	MFC::iterator mfc = find_pair<uint32_t,uint32_t>(mfcs_, MFC::value_type(mcastgrp, origin));
	if (mfcs_.end() == mfc)
		return;
	mfcs_.erase(mfc);
	struct mfcctl mc;
	bzero(&mc, sizeof(mc));
	memcpy(&mc.mfcc_origin, &origin, sizeof(mc.mfcc_origin));
	memcpy(&mc.mfcc_mcastgrp, &mcastgrp, sizeof(mc.mfcc_mcastgrp));
	mc.mfcc_parent = 0; //vifs_[parent];
	for (int vifi(0), maxvifs(vifs_.size()); vifi < maxvifs; ++vifi)
		mc.mfcc_ttls[vifi] = 0;
	if (setsockopt(mrt4Sock_, IPPROTO_IP, MRT_DEL_MFC, &mc, sizeof(mc)) < 0)
		throw("Delete multicast route error");
	std::cout <<"deleted (" << inet_intoa(origin)  <<"," << inet_intoa(mcastgrp) <<")" <<std::endl;
}

void MRTctrl::DeleteMFC(const string & parent, uint32_t mcastgrp)
{
	std::pair<MFC::iterator, MFC::iterator> origins = mfcs_.equal_range(mcastgrp);
	for (MFC::iterator it(origins.first); it != origins.second; ++it) {
		struct mfcctl mc;
		bzero(&mc, sizeof(mc));
		memcpy(&mc.mfcc_origin, &(it->second), sizeof(mc.mfcc_origin));
		memcpy(&mc.mfcc_mcastgrp, &mcastgrp, sizeof(mc.mfcc_mcastgrp));
		mc.mfcc_parent = 0; //vifs_[parent];
		for (int vifi(0), maxvifs(vifs_.size()); vifi < maxvifs; ++vifi)
			mc.mfcc_ttls[vifi] = 0;
		if (setsockopt(mrt4Sock_, IPPROTO_IP, MRT_DEL_MFC, &mc, sizeof(mc)) < 0)
			throw("Delete multicast route error");
		std::cout <<"deleted (" << inet_intoa(it->second)  <<"," << inet_intoa(mcastgrp) <<")" <<std::endl;
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
	recv.imr_interface = iifAddr4_;
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
	case IGMP_V1_MEMBERSHIP_REPORT:
	case IGMP_V2_MEMBERSHIP_REPORT:
		Join(igmpv2->igmp_group.s_addr);
		break;
	case IGMP_V2_LEAVE_GROUP:
		Leave(igmpv2->igmp_group.s_addr);
		break;
	case IGMPV3_HOST_MEMBERSHIP_REPORT:
	{
		const struct igmpv3_grec& grec = igmpv3->grec[0];
		const uint32_t& group = grec.grec_mca;
		switch (grec.grec_type)
		{
		case IGMPV3_CHANGE_TO_EXCLUDE:
			Join(group);
			break;
		case IGMPV3_CHANGE_TO_INCLUDE:
			Leave(group);
			break;
		default:
			cerr << "ignoring unknown IGMPv3 message type " << grec.grec_type <<endl;
		}
		break;
	}
	default:
		cerr << "ignoring unknown IGMP message type " << igmpv3->type <<endl;
	}
}
catch(exception & e)
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
catch(exception & e)
{
	cerr << e.what() <<endl;
}
