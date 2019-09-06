/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
#ifndef __LWIP_NETIF_H__
#define __LWIP_NETIF_H__

#include "lwip/opt.h"

#define ENABLE_LOOPBACK (LWIP_NETIF_LOOPBACK || LWIP_HAVE_LOOPIF)

#include "lwip/err.h"

#include "lwip/ip_addr.h"

#include "lwip/inet.h"
#include "lwip/pbuf.h"
#if LWIP_DHCP
struct dhcp;
#endif
#if LWIP_AUTOIP
struct autoip;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Throughout this file, IP addresses are expected to be in
 * the same byte order as in IP_PCB. */

/** must be the maximum of all used hardware address lengths
    across all types of interfaces in use */
#define NETIF_MAX_HWADDR_LEN 6U

/** TODO: define the use (where, when, whom) of netif flags */

/** whether the network interface is 'up'. this is
 * a software flag used to control whether this network
 * interface is enabled and processes traffic.
 */
#define NETIF_FLAG_UP           0x01U
/** if set, the netif has broadcast capability */
#define NETIF_FLAG_BROADCAST    0x02U
/** if set, the netif is one end of a point-to-point connection */
#define NETIF_FLAG_POINTTOPOINT 0x04U
/** if set, the interface is configured using DHCP */
#define NETIF_FLAG_DHCP         0x08U
/** if set, the interface has an active link
 *  (set by the network interface driver) */
#define NETIF_FLAG_LINK_UP      0x10U
/** if set, the netif is an device using ARP */
#define NETIF_FLAG_ETHARP       0x20U
/** if set, the netif has IGMP capability */
#define NETIF_FLAG_IGMP         0x40U

/*链路层：
1.网卡地址就是数据包的发送地址和接收地址，也就是帧首部所包含的MAC地址
2.链路层的主要工作就是对电信号进行分组并形成具有特定意义的数据帧，然后以广播的形式通过物理介质发送给接收方。
*/

/**网络层引入三个协议
IP协议：用于判断两台主机是否属于同一个子网
	以C类地址192.168.24.1为例，其中前24位就是网络地址，后8位就是主机地址。每一个10进制数是8位(0~255)
	发送者和接收者的IP地址是已知的(应用层的协议会传入)， 因此我们只要通过子网掩码对两个IP地址进行AND运算后就能够判断双方是否在同一个子网
ARP协议：(同一子网下)ARP 协议的基本功能是使用目标主机的 IP 地址，查询其对应的 MAC 地址，以保证底层链路上数据包通信的进行。
	功能流程：先查询ARP表，如果表中有目标IP地址，就直接得到目标MAC地址
	如果没有，就发送一个广播(ARP请求，目的地址0xFFFFFFFFFFFF)，请求同一网段内所有主机都会收到一条询问信息：“目标IP的MAC地址是多少”
路由协议：使ARP的MAC寻址不再局限在同一子网中。
	在同一个子网，就通过ARP协议查询对应的MAC地址，然后以广播的形式向该子网内的主机发送数据包；
	如果不在同一个子网，以太网会将该数据包转发给本子网的网关进行路由。网关是互联网上子网与子网之间的桥梁，所以网关会进行多次转发，
	最终将该数据包转发到目标IP所在的子网中，然后再通过ARP获取目标机MAC，最终也是通过广播形式将数据包发送给接收方
网络层的主要工作是定义网络地址，区分网段，子网内MAC寻址，对于不同子网的数据包进行路由。
*/

/*传输层
链路层定义了主机的身份，即MAC地址， 而网络层定义了IP地址，明确了主机所在的网段，有了这两个地址，数据包就从可以从一个主机发送到另一台主机。
但实际上数据包是从一个主机的某个应用程序发出，然后由对方主机的应用程序接收。当数据包被发送到主机上以后，是传输层主要决定哪个应用程序要接收这个包
传输层的主要工作是定义端口，标识应用程序身份，实现端口到端口的通信。UCP只发包不建立连接，而TCP协议可以保证数据传输的可靠性(三次对话的确认机制)
*/

/*应用层
有了以上三层协议的支持，数据已经可以从一个主机上的应用程序传输到另一台主机的应用程序了，但此时传过来的数据是字节流，不能很好的被程序识别，操作性差。
因此，应用层定义了各种各样的协议来规范数据格式，常见的有 HTTP、FTP、SMTP 等
应用层的主要工作就是定义数据格式并按照对应的格式解读数据
*/

/** Generic data structure used for all lwIP network interfaces.
 *  The following fields should be filled in by the initialization
 *  function for the device driver: hwaddr_len, hwaddr[], mtu, flags */

struct netif { //network interfaces
  /** pointer to next in linked list */
  struct netif *next;

  /** IP address configuration in network byte order */
  //IP地址和子网掩码通过按位与运算后就可以得到网络地址
  struct ip_addr ip_addr; //网络接口的IP地址
  struct ip_addr netmask; //子网掩码
  struct ip_addr gw;	  //网关地址

  /** This function is called by the network device driver
   *  to pass a packet up the TCP/IP stack. */
  err_t (* input)(struct pbuf *p, struct netif *inp); //在lwip_init里赋值ip_input
  /** This function is called by the IP module(模块) when it wants
   *  to send a packet on the interface. This function typically
   *  first resolves(解析) the hardware address, then sends the packet. */
  err_t (* output)(struct netif *netif, struct pbuf *p,
       struct ip_addr *ipaddr); //在jif_init里初始化成jif_output
  /** Address Resolution Protocol
   *  This function is called by the ARP module when it wants
   *  to send a packet on the interface. This function outputs
   *  the pbuf as-is on the link medium. */
  err_t (* linkoutput)(struct netif *netif, struct pbuf *p);//在jif_init里赋值low_level_output
#if LWIP_NETIF_STATUS_CALLBACK
  /** This function is called when the netif state is set to up or down
   */
  void (* status_callback)(struct netif *netif);
#endif /* LWIP_NETIF_STATUS_CALLBACK */
#if LWIP_NETIF_LINK_CALLBACK
  /** This function is called when the netif link is set to up or down
   */
  void (* link_callback)(struct netif *netif);
#endif /* LWIP_NETIF_LINK_CALLBACK */
  /** This field can be set by the device driver and could point
   *  to state information for the device. */
  void *state; //设备状态信息，由驱动程序设置
#if LWIP_DHCP
  /** the DHCP client state information for this netif */
  struct dhcp *dhcp;
#endif /* LWIP_DHCP */
#if LWIP_AUTOIP
  /** the AutoIP client state information for this netif */
  struct autoip *autoip;
#endif
#if LWIP_NETIF_HOSTNAME
  /* the hostname for this netif, NULL is a valid value */
  char*  hostname;
#endif /* LWIP_NETIF_HOSTNAME */
  /** number of bytes used in hwaddr */
  u8_t hwaddr_len;
  /** link level hardware address of this interface */
  u8_t hwaddr[NETIF_MAX_HWADDR_LEN];
  /** maximum transfer unit (in bytes) */
  u16_t mtu;
  /** flags (see NETIF_FLAG_ above) */
  u8_t flags;
  /** descriptive abbreviation */
  char name[2];
  /** number of this interface */
  u8_t num;
#if LWIP_SNMP
  /** link type (from "snmp_ifType" enum from snmp.h) */
  u8_t link_type;
  /** (estimate) link speed */
  u32_t link_speed;
  /** timestamp at last change made (up/down) */
  u32_t ts;
  /** counters */
  u32_t ifinoctets;
  u32_t ifinucastpkts;
  u32_t ifinnucastpkts;
  u32_t ifindiscards;
  u32_t ifoutoctets;
  u32_t ifoutucastpkts;
  u32_t ifoutnucastpkts;
  u32_t ifoutdiscards;
#endif /* LWIP_SNMP */
#if LWIP_IGMP
  /* This function could be called to add or delete a entry in the multicast filter table of the ethernet MAC.*/
  err_t (*igmp_mac_filter)( struct netif *netif, struct ip_addr *group, u8_t action);
#endif /* LWIP_IGMP */
#if LWIP_NETIF_HWADDRHINT
  u8_t *addr_hint;
#endif /* LWIP_NETIF_HWADDRHINT */
#if ENABLE_LOOPBACK
  /* List of packets to be queued for ourselves. */
  struct pbuf *loop_first;
  struct pbuf *loop_last;
#if LWIP_LOOPBACK_MAX_PBUFS
  u16_t loop_cnt_current;
#endif /* LWIP_LOOPBACK_MAX_PBUFS */
#endif /* ENABLE_LOOPBACK */
};

#if LWIP_SNMP
#define NETIF_INIT_SNMP(netif, type, speed) \
  /* use "snmp_ifType" enum from snmp.h for "type", snmp_ifType_ethernet_csmacd by example */ \
  netif->link_type = type;    \
  /* your link speed here (units: bits per second) */  \
  netif->link_speed = speed;  \
  netif->ts = 0;              \
  netif->ifinoctets = 0;      \
  netif->ifinucastpkts = 0;   \
  netif->ifinnucastpkts = 0;  \
  netif->ifindiscards = 0;    \
  netif->ifoutoctets = 0;     \
  netif->ifoutucastpkts = 0;  \
  netif->ifoutnucastpkts = 0; \
  netif->ifoutdiscards = 0
#else /* LWIP_SNMP */
#define NETIF_INIT_SNMP(netif, type, speed)
#endif /* LWIP_SNMP */


/** The list of network interfaces. */
extern struct netif *netif_list;
/** The default network interface. */
extern struct netif *netif_default;

#define netif_init() /* Compatibility define, not init needed. */

struct netif *netif_add(struct netif *netif, struct ip_addr *ipaddr, struct ip_addr *netmask,
      struct ip_addr *gw,
      void *state,
      err_t (* init)(struct netif *netif),
      err_t (* input)(struct pbuf *p, struct netif *netif));

void
netif_set_addr(struct netif *netif,struct ip_addr *ipaddr, struct ip_addr *netmask,
    struct ip_addr *gw);
void netif_remove(struct netif * netif);

/* Returns a network interface given its name. The name is of the form
   "et0", where the first two letters are the "name" field in the
   netif structure, and the digit is in the num field in the same
   structure. */
struct netif *netif_find(char *name);

void netif_set_default(struct netif *netif);

void netif_set_ipaddr(struct netif *netif, struct ip_addr *ipaddr);
void netif_set_netmask(struct netif *netif, struct ip_addr *netmask);
void netif_set_gw(struct netif *netif, struct ip_addr *gw);

void netif_set_up(struct netif *netif);
void netif_set_down(struct netif *netif);
u8_t netif_is_up(struct netif *netif);

#if LWIP_NETIF_STATUS_CALLBACK
/*
 * Set callback to be called when interface is brought up/down
 */
void netif_set_status_callback(struct netif *netif, void (* status_callback)(struct netif *netif));
#endif /* LWIP_NETIF_STATUS_CALLBACK */

#if LWIP_NETIF_LINK_CALLBACK
void netif_set_link_up(struct netif *netif);
void netif_set_link_down(struct netif *netif);
u8_t netif_is_link_up(struct netif *netif);
/*
 * Set callback to be called when link is brought up/down
 */
void netif_set_link_callback(struct netif *netif, void (* link_callback)(struct netif *netif));
#endif /* LWIP_NETIF_LINK_CALLBACK */

#ifdef __cplusplus
}
#endif

#if ENABLE_LOOPBACK
err_t netif_loop_output(struct netif *netif, struct pbuf *p, struct ip_addr *dest_ip);
void netif_poll(struct netif *netif);
#if !LWIP_NETIF_LOOPBACK_MULTITHREADING
void netif_poll_all(void);
#endif /* !LWIP_NETIF_LOOPBACK_MULTITHREADING */
#endif /* ENABLE_LOOPBACK */

#endif /* __LWIP_NETIF_H__ */
