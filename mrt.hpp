#pragma once

#include <inttypes.h>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <netinet/ip.h>

typedef unsigned int uint32_t;
template<class K, class V>
typename std::multimap<K, V>::iterator
find_pair(std::multimap<K, V>& map, const typename std::multimap<K, V>::value_type& pair)
{
	 typedef typename std::multimap<K, V>::iterator it;
	 std::pair<it,it> range = map.equal_range(pair.first);
	 for (it p = range.first; p != range.second; ++p)
		  if (p->second == pair.second)
				return p;
	 return map.end();
}

template<class K, class V>
bool insert_uniq(std::multimap<K, V>& map, const std::pair<K, V>& pair)
{
	 if (find_pair(map, pair) == map.end()) {
		  map.insert(pair);
		  return true;
	 }
	 return false;
}

/**
 * @brief controls Multicast Routing Table
 * 
 * 
 */
class MRTctrl
{
public:
	MRTctrl();
	~MRTctrl();
	/**
	 * @brief getMtrSock provides socket fd to control MRT
	 * @return file descriptor
	 */
	int getMrt4Sock() { return mrt4Sock_; }
	int getMrt6Sock() { return mrt6Sock_; }
	/**
	 * @brief getIgmpSock provides socket fd to receive IGMP membership reports
	 * @return file descriptor
	 */
	int getIgmp4Sock() { return igmp4Sock_; }
	int getIgmp6Sock() { return igmp6Sock_; }
	/**
	 * @brief delete multicast route from MFC
	 *
	 * @param parent - interface where it arrived
	 * @param origin - origin of mcast source address
	 * @param mcastgrp - multicast group address
	 */
	void DeleteMFC(const std::string& parent, uint32_t origin, uint32_t mcastgrp);
	/**
	 * @brief delete all multicast routes for the particular group from MFC
	 *
	 * @param parent - interface where it arrived
	 * @param origin -  address of multicast data source
	 * @param mcastgrp - multicast group address
	 */
	void DeleteMFC(const std::string& parent, uint32_t mcastgrp);
	/**
	 * @brief add multicast route to MFC
	 *
	 * @param parent - input interface where
	 * @param origin -  address of multicast data source
	 * @param mcastgrp - multicast group address
	 */
	void AddMFC(const std::string& parent, uint32_t origin, uint32_t mcastgrp);
	/**
	 * @brief Join multicast group over the uplink interface
	 *
	 * @param mcastgrp - multicast group address
	 */
	void Join(uint32_t mcastgrp);
	/**
	 * @brief Leave multicast group on the uplink interface
	 *
	 * @param mcastgrp - multicast group address
	 */
	void Leave(uint32_t mcastgrp);
	/**
	 * @brief Update the receiver list on Join/Leave request
	 * [<0;86;16M
	 * @param buffer  - membership request IP packet
	 * @param channel - ChanCB_PS
	 */
	void handleIGMP(int igmpSock);
	/**
	 * @brief Update MFC on multicast datagram arrive
	 *
	 * @param mrtSock  - raw socket to control linux system MRT
	 */
	void handleMcastData(int mrtSock);

	void init();
	void initMRT();
	void initVIFs();
	void initIGMP();

protected:
	// default network interface where the multicast data arrive
	std::string parent_;
	// output network interface where the multicast data arrive
	std::string output_;

private:
	// Multicast Forwarding Cache <group,origin>
	typedef std::multimap<uint32_t, uint32_t> MFC;
	MFC mfcs_;
	// Virtual interfaces
	typedef std::vector<std::pair<std::string, u_int16_t> > Vif;
	Vif vifs_;
	// Proxy sockets forwarding IGMP reports to uplink
	typedef std::map<uint32_t, int> ForwardSock;
	ForwardSock msockets_;
	// Uplink interface address
	in_addr iifAddr4_;
	in6_addr iifAddr6_;
	// Raw socket to control linux system MRT
	int mrt4Sock_;
	int mrt6Sock_;
	// Raw socket to receive IGMP membership reports
	int igmp4Sock_;
	int igmp6Sock_;
};
