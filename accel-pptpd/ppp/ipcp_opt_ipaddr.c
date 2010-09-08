#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_ppp.h>

#include "ppp.h"
#include "ppp_ipcp.h"
#include "log.h"
#include "ipdb.h"

static struct ipcp_option_t *ipaddr_init(struct ppp_ipcp_t *ipcp);
static void ipaddr_free(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt);
static int ipaddr_send_conf_req(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt, uint8_t *ptr);
static int ipaddr_send_conf_nak(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt, uint8_t *ptr);
static int ipaddr_recv_conf_req(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt, uint8_t *ptr);
//static int ipaddr_recv_conf_ack(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt, uint8_t *ptr);
static void ipaddr_print(void (*print)(const char *fmt,...),struct ipcp_option_t*, uint8_t *ptr);

struct ipaddr_option_t
{
	struct ipcp_option_t opt;
	in_addr_t addr;
	in_addr_t peer_addr;
};

static struct ipcp_option_handler_t ipaddr_opt_hnd=
{
	.init=ipaddr_init,
	.send_conf_req=ipaddr_send_conf_req,
	.send_conf_nak=ipaddr_send_conf_nak,
	.recv_conf_req=ipaddr_recv_conf_req,
	.free=ipaddr_free,
	.print=ipaddr_print,
};

static struct ipcp_option_t *ipaddr_init(struct ppp_ipcp_t *ipcp)
{
	struct ipaddr_option_t *ipaddr_opt=malloc(sizeof(*ipaddr_opt));
	memset(ipaddr_opt,0,sizeof(*ipaddr_opt));
	ipaddr_opt->opt.id=CI_ADDR;
	ipaddr_opt->opt.len=6;

	return &ipaddr_opt->opt;
}

static void ipaddr_free(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt)
{
	struct ipaddr_option_t *ipaddr_opt=container_of(opt,typeof(*ipaddr_opt),opt);

	if (ipaddr_opt->peer_addr)
		ipdb_put(ipcp->ppp, ipaddr_opt->addr, ipaddr_opt->peer_addr);

	free(ipaddr_opt);
}

static int ipaddr_send_conf_req(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt, uint8_t *ptr)
{
	struct ipaddr_option_t *ipaddr_opt=container_of(opt,typeof(*ipaddr_opt),opt);
	struct ipcp_opt32_t *opt32=(struct ipcp_opt32_t*)ptr;
	
	if (!ipaddr_opt->addr && ipdb_get(ipcp->ppp, &ipaddr_opt->addr, &ipaddr_opt->peer_addr)) {
		log_warn("ppp:ipcp: no free IP address\n");
		return -1;
	}
	
	opt32->hdr.id=CI_ADDR;
	opt32->hdr.len=6;
	opt32->val=ipaddr_opt->addr;
	return 6;
}

static int ipaddr_send_conf_nak(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt, uint8_t *ptr)
{
	struct ipaddr_option_t *ipaddr_opt=container_of(opt,typeof(*ipaddr_opt),opt);
	struct ipcp_opt32_t *opt32=(struct ipcp_opt32_t*)ptr;
	opt32->hdr.id=CI_ADDR;
	opt32->hdr.len=6;
	opt32->val=ipaddr_opt->peer_addr;
	return 6;
}

static int ipaddr_recv_conf_req(struct ppp_ipcp_t *ipcp, struct ipcp_option_t *opt, uint8_t *ptr)
{
	struct ipaddr_option_t *ipaddr_opt = container_of(opt,typeof(*ipaddr_opt), opt);
	struct ipcp_opt32_t *opt32 = (struct ipcp_opt32_t*)ptr;
	struct ifreq ifr;
	struct sockaddr_in addr;
	struct npioctl np;

	if (ipaddr_opt->peer_addr == opt32->val)
		goto ack;
		
	if (!ipaddr_opt->peer_addr) {
		ipaddr_opt->peer_addr = opt32->val;
		goto ack;
	}
	
	return IPCP_OPT_NAK;

ack:
	memset(&ifr, 0, sizeof(ifr));
	memset(&addr, 0, sizeof(addr));

	sprintf(ifr.ifr_name,"ppp%i",ipcp->ppp->unit_idx);

	addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ipaddr_opt->addr;
	memcpy(&ifr.ifr_addr,&addr,sizeof(addr));

	if (ioctl(sock_fd, SIOCSIFADDR, &ifr))
		log_error("\nipcp: failed to set PA address: %s\n", strerror(errno));
	
  addr.sin_addr.s_addr = ipaddr_opt->peer_addr;
	memcpy(&ifr.ifr_dstaddr,&addr,sizeof(addr));
	
	if (ioctl(sock_fd, SIOCSIFDSTADDR, &ifr))
		log_error("\nipcp: failed to set remote PA address: %s\n", strerror(errno));

	if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr))
		log_error("\nipcp: failed to get interface flags: %s\n", strerror(errno));

	ifr.ifr_flags |= IFF_UP | IFF_POINTOPOINT;

	if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr))
		log_error("\nipcp: failed to set interface flags: %s\n", strerror(errno));

	np.protocol = PPP_IP;
	np.mode = NPMODE_PASS;

	if (ioctl(ipcp->ppp->unit_fd, PPPIOCSNPMODE, &np))
		log_error("\nipcp: failed to set NP mode: %s\n", strerror(errno));

	return IPCP_OPT_ACK;
}

static void ipaddr_print(void (*print)(const char *fmt,...),struct ipcp_option_t *opt, uint8_t *ptr)
{
	struct ipaddr_option_t *ipaddr_opt=container_of(opt,typeof(*ipaddr_opt),opt);
	struct ipcp_opt32_t *opt32=(struct ipcp_opt32_t*)ptr;
	struct in_addr in;

	if (ptr) in.s_addr=opt32->val;
	else in.s_addr=ipaddr_opt->addr;
	
	print("<addr %s>",inet_ntoa(in));
}

static void __init ipaddr_opt_init()
{
	ipcp_option_register(&ipaddr_opt_hnd);
}

