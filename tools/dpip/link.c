/*
 * DPVS is a software load balancer (Virtual Server) based on DPDK.
 *
 * Copyright (C) 2017 iQIYI (www.iqiyi.com).
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "common.h"
#include "dpip.h"
#include "conf/netif.h"
#include "sockopt.h"

#define LINK_DEV_NAME_MAXLEN    32
#define LINK_ARG_ITEM_MAXLEN    256
#define LINK_ARG_VALUE_MAXLEN   256

#define RESET   "\033[0m"
#define BLACK   "\033[30m"      /* Black */
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
#define WHITE   "\033[37m"      /* White */
#define BOLDBLACK   "\033[1m\033[30m"      /* Bold Black */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDYELLOW  "\033[1m\033[33m"      /* Bold Yellow */
#define BOLDBLUE    "\033[1m\033[34m"      /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */

#define CLEAR() printf("\033[2J")
#define CLEAR_LINE() printf("\033[K")
#define MOVEUP(x) printf("\033[%dA", (x))
#define MOVEDOWN(x) printf("\033[%dB", (x))
#define MOVELEFT(y) printf("\033[%dD", (y))
#define MOVERIGHT(y) printf("\033[%dC",(y))
#define MOVETO(x,y) printf("\033[%d;%dH", (x), (y))
#define RESET_CURSOR() printf("\033[H")
#define HIDE_CURSOR() printf("\033[?25l")
#define SHOW_CURSOR() printf("\033[?25h")
#define HIGHT_LIGHT() printf("\033[7m")
#define UN_HIGHT_LIGHT() printf("\033[27m")

typedef enum link_device {
    LINK_DEVICE_NIC = 0,
    LINK_DEVICE_CPU,
} link_device_t;

typedef struct link_stats {
    int enabled;
    int interval;
    int count;
} link_stats_t;

struct link_param
{
    int verbose;
    int status;
    link_stats_t stats;
    link_device_t dev_type;
    char dev_name[LINK_DEV_NAME_MAXLEN];
    char item[LINK_ARG_ITEM_MAXLEN]; /* for SET cmd */
    char value[LINK_ARG_VALUE_MAXLEN]; /* for SET cmd */
}; 

bool g_color = false;
netif_nic_num_get_t g_nports;

static inline int get_netif_port_nb(void)
{
    int ret;
    size_t len;
    netif_nic_num_get_t *p_nports = NULL;

    ret = dpvs_getsockopt(SOCKOPT_NETIF_GET_PORT_NUM, NULL, 0,
            (void **)&p_nports, &len);
    if (EDPVS_OK != ret || !p_nports || !len) {
        fprintf(stderr, "Fail to get configured NIC number: %s.\n",
                dpvs_strerror(ret));
        return ret;
    }
    g_nports = *p_nports;
    dpvs_sockopt_msg_free((void *)p_nports);

    return EDPVS_OK;
}

static inline int get_pid_by_name(const char *name)
{
    int pid;
    for (pid = 0; pid < g_nports.nic_num; pid++) {
        //printf("port[%d/%d]=%s\n", pid, g_nports.nic_num, g_nports.pid_name_map[pid]);
        if (pid >= NETIF_MAX_PORTS)
            break;
        if (!strcmp(g_nports.pid_name_map[pid], name))
            return pid;
    }
    return -1;
}

static void link_help(void)
{
    fprintf(stderr, 
            "Usage:\n"
            "    dpip link show [ NIC-NAME ]\n"
            "    dpip link show BOND-NAME status\n"
            "    dpip -v link show [ NIC-NAME ]\n"
            "    dpip -s link show [ i INTERVAL ] [ c COUNT ] [ NIC-NAME ]\n"
            "    dpip link show CPU-NAME\n"
            "    dpip -v link show CPU-NAME\n"
            "    dpip -s link show [ i INTERVAL ] [ c COUNT ]  CPU-NAME\n"
            "    dpip link set DEV-NAME ITEM VALUE\n"
            "    ---supported items---\n"
            "    promisc [on|off], forward2kni [on|off], link [up|down],"
            " addr, \n"
            "    bond-[mode|slave|primary|ximit-policy|monitor-interval|link-up-prop|"
            "link-down-prop]\n"
            "Examples:\n"
            "    dpip link show\n"
            "    dpip -s -v link show dpdk0\n"
            "    dpip link show cpu\n"
            "    dpip -s link show cpu2 i 3 c 5\n"
            "    dpip -s -v link show cpu3 i 2\n"
            "    dpip link show bond0 status\n"
            "    dpip link set dpdk0 promisc on/off\n"
            "    dpip link set dpdk0 forward2kni on/off\n"
            "    dpip link set bond0 link up/down\n"
           );
}

/* check device name, return 0 and fill its id if OK, otherwise return -1 */
static inline int link_check_dev_name(link_device_t type, const char *name, unsigned *id)
{
    char *endptr;
    assert(name && id);

    switch(type) {
        case LINK_DEVICE_NIC:
        {
            int res;
            res = get_pid_by_name(name);
            if (res < 0)
                return -1;
            *id = res;
            return 0;
        }
        case LINK_DEVICE_CPU:
        {
            if (strncmp(name, "cpu", 3))
                return -1;
            if (strlen(name) == 3)
                return 0;
            else {
                *id = strtol(&name[3], &endptr, 10);
                if (*endptr)
                    return -1;
            }
            return 0;
        }
        default:
            return -1;
    }
}

static int link_parse_args(struct dpip_conf *conf,
                           struct link_param *param)
{
    memset(param, 0, sizeof(*param));
    param->verbose = conf->verbose;
    param->stats.enabled = conf->stats;
    param->dev_type = LINK_DEVICE_NIC; /* default show NIC */

    while (conf->argc > 0) {
        if (!strncmp(conf->argv[0], "dpdk", 4) ||
                !strncmp(conf->argv[0], "eth", 3)) {
            snprintf(param->dev_name, sizeof(param->dev_name), "%s", conf->argv[0]);
        } else if (!strncmp(conf->argv[0], "bond", 4) &&
                strncmp(conf->argv[0], "bond-", 5)) {
            snprintf(param->dev_name, sizeof(param->dev_name), "%s", conf->argv[0]);
            if (conf->argc > 1 && !strncmp(conf->argv[1], "status", 6)) {
                param->status = 1;
                NEXTARG(conf);
            }
        } else if (strncmp(conf->argv[0], "cpu", 3) == 0) {
            param->dev_type = LINK_DEVICE_CPU;
            if (strcmp(conf->argv[0], "cpu") != 0)
                snprintf(param->dev_name, sizeof(param->dev_name), "%s", conf->argv[0]);
        } else if (strcmp(conf->argv[0], "i") == 0) {
            NEXTARG_CHECK(conf, "i");
            param->stats.interval = atoi(conf->argv[0]);
        } else if (strcmp(conf->argv[0], "c") == 0) {
            NEXTARG_CHECK(conf, "c");
            param->stats.count = atoi(conf->argv[0]);
        } else if (conf->cmd == DPIP_CMD_SET ||
                conf->cmd == DPIP_CMD_ADD ||
                conf->cmd == DPIP_CMD_DEL) {
            if (!param->item[0]) {
                snprintf(param->item, sizeof(param->item), "%s", conf->argv[0]);
                NEXTARG_CHECK(conf, param->item);
                snprintf(param->value, sizeof(param->value), "%s", conf->argv[0]);
            }
        }
        NEXTARG(conf);
    }

    if (conf->argc > 0) {
        fprintf(stderr, "too many arguments\n");
        return EDPVS_INVAL;
    }

    if (param->stats.count && !param->stats.interval) {
        fprintf(stderr, "miss statistics display interval");
        return EDPVS_INVAL;
    }

    return EDPVS_OK;
}

static int dump_nic_basic(portid_t pid)
{
    int err;
    size_t len = 0;
    netif_nic_basic_get_t get, *p_get = NULL;

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_PORT_BASIC, &pid, sizeof(pid), (void **)&p_get, &len);
    if (err != EDPVS_OK || !p_get || !len)
        return err;
    get = *p_get; 
    dpvs_sockopt_msg_free(p_get);

    assert(pid == get.port_id);
    printf("%d: %s: socket %d mtu %d rx-queue %d tx-queue %d\n",
            get.port_id + 1, get.name, get.socket_id, get.mtu, get.nrxq, get.ntxq);

    printf("    ");
    switch (get.link_status) {
        case ETH_LINK_UP:
            printf("UP ");
            break;
        case ETH_LINK_DOWN:
            printf("DOWN ");
            break;
    }

    printf("%d Mbps ", get.link_speed);

    switch (get.link_duplex) {
        case ETH_LINK_HALF_DUPLEX:
            printf("half-duplex ");
            break;
        case ETH_LINK_FULL_DUPLEX:
            printf("full-duplex ");
            break;
    }

    switch (get.link_autoneg) {
        case ETH_LINK_FIXED:
            printf("fixed-nego ");
            break;
        case ETH_LINK_AUTONEG:
            printf("auto-nego ");
            break;
    }

    switch (get.promisc) {
        case 0:
            printf("promisc-off ");
            break;
        case 1:
            printf("promisc-on ");
            break;
    }

    if (get.flags & NETIF_PORT_FLAG_FORWARD2KNI)
        printf("foward2kni-on");
    else
        printf("forward2kni-off");
    printf("\n");

    printf("    addr %s ", get.addr);
    if (get.flags & NETIF_PORT_FLAG_RX_IP_CSUM_OFFLOAD)
        printf("OF_RX_IP_CSUM ");
    if (get.flags & NETIF_PORT_FLAG_TX_IP_CSUM_OFFLOAD)
        printf("OF_TX_IP_CSUM ");
    if (get.flags & NETIF_PORT_FLAG_TX_TCP_CSUM_OFFLOAD)
        printf("OF_TX_TCP_CSUM ");
    if (get.flags & NETIF_PORT_FLAG_TX_UDP_CSUM_OFFLOAD)
        printf("OF_TX_UDP_CSUM ");
    printf("\n");

    return EDPVS_OK;
}

static int dump_nic_stats(portid_t pid)
{
    int err;
    size_t len = 0;
    netif_nic_stats_get_t get, *p_get = NULL;
    netif_nic_mbufpool_t *p_mbufpool_get = NULL;

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_PORT_STATS, &pid, sizeof(pid), (void **)&p_get, &len);
    if (err != EDPVS_OK || !p_get || !len)
        return err;
    get = *p_get; 
    dpvs_sockopt_msg_free(p_get);

    assert(len == sizeof(netif_nic_stats_get_t));

    printf("    %-16s%-16s%-16s%-16s\n",
            "ipackets", "opackets", "ibytes", "obytes");
    printf("    %-16lu%-16lu%-16lu%-16lu\n",
            get.ipackets, get.opackets, get.ibytes, get.obytes);
    printf("    %-16s%-16s%-16s%-16s\n",
            "ierrors", "oerrors", "imissed", "rx_nombuf");
    printf("    %-16lu%-16lu%-16lu%-16lu\n",
            get.ierrors, get.oerrors, get.imissed, get.rx_nombuf);

    /* Is necessary to display per-queue statistics? */
    /* Do not support display per-queue statistics now */

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_PORT_MBUFPOOL, &pid, sizeof(pid),
            (void **)&p_mbufpool_get, &len);
    if (err != EDPVS_OK || !p_mbufpool_get || !len)
        return err;
    assert(len == sizeof(netif_nic_mbufpool_t));
    printf("    %-16s%-16s\n", "mbuf-avail", "mbuf-inuse");
    printf("    %-16u%-16u\n", p_mbufpool_get->available, p_mbufpool_get->inuse);
    dpvs_sockopt_msg_free(p_mbufpool_get);

    return EDPVS_OK;
}

static int dump_nic_verbose(portid_t pid)
{
    int err, i;
    size_t len = 0;
    netif_nic_dev_get_t dev_get, *p_dev_get = NULL;
    netif_nic_conf_queues_t *p_que_get = NULL;
    struct netif_mc_list_conf *mc_list;

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_PORT_DEV_INFO, &pid, sizeof(pid),
        (void **)&p_dev_get, &len);
    if (err != EDPVS_OK || !p_dev_get || !len)
        return err;
    assert(len == sizeof(netif_nic_dev_get_t));
    dev_get = *p_dev_get;
    dpvs_sockopt_msg_free(p_dev_get);

    printf("    %-32s%-32s\n", "pci_addr", "driver_name");
    printf("    %-32s%-32s\n", dev_get.pci_addr, dev_get.driver_name);
    printf("    %-16s%-16s%-16s%-16s\n",
            "if_index", "min_rx_bufsize", "max_rx_pktlen", "max_mac_addrs");
    printf("    %-16u%-16u%-16u%-16u\n", dev_get.if_index, dev_get.min_rx_bufsize,
            dev_get.max_rx_pktlen, dev_get.max_mac_addrs);
    printf("    %-16s%-16s%-16s%-16s\n", "max_rx_queues", "max_tx_queues",
            "max_hash_addrs", "max_vfs");
    printf("    %-16u%-16u%-16u%-16u\n", dev_get.max_rx_queues, dev_get.max_tx_queues,
            dev_get.max_hash_mac_addrs, dev_get.max_vfs);
    printf("    %-16s%-16s%-16s%-16s\n", "max_vmdq_pools", "rx_ol_capa",
            "tx_ol_capa", "reta_size");
    printf("    %-16u%-16u%-16u%-16u\n", dev_get.max_vmdq_pools, dev_get.rx_offload_capa,
            dev_get.tx_offload_capa, dev_get.reta_size);
    printf("    %-16s%-16s%-16s%-16s\n", "hash_key_size", "flowtype_rss_ol",
            "vmdq_que_base", "vmdq_que_num");
    printf("    %-16u%-16lu%-16u%-16u\n", dev_get.hash_key_size, dev_get.flow_type_rss_offloads,
            dev_get.vmdq_queue_base, dev_get.vmdq_queue_num);
    printf("    %-16s%-16s%-16s%-16s\n", "rx_desc_max", "rx_desc_min",
            "rx_desc_align", "vmdq_pool_base");
    printf("    %-16u%-16u%-16u%-16u\n", dev_get.rx_desc_lim_nb_max, dev_get.rx_desc_lim_nb_min,
            dev_get.rx_desc_lim_nb_align, dev_get.vmdq_pool_base);
    printf("    %-16s%-16s%-16s%-16s\n", "tx_desc_max", "tx_desc_min",
            "tx_desc_align", "speed_capa");
    printf("    %-16u%-16u%-16u%-16u\n", dev_get.tx_desc_lim_nb_max, dev_get.tx_desc_lim_nb_min,
            dev_get.tx_desc_lim_nb_align, dev_get.speed_capa);

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_PORT_QUEUE, &pid, sizeof(pid),
            (void **)&p_que_get, &len);
    if (err != EDPVS_OK || !p_que_get || !len)
        return err;
    assert(len >= sizeof(netif_nic_conf_queues_t));
    printf("    Queue Configuration:\n%s", p_que_get->cf_queue);
    dpvs_sockopt_msg_free(p_que_get);

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_MC_ADDRS, &pid, sizeof(pid),
                          (void **)&mc_list, &len);
    if (err != EDPVS_OK)
        return err;
    assert(len >= sizeof(*mc_list));
    for (i = 0; i < mc_list->naddr; i++) {
        if (i == 0)
            printf("    HW mcast list:\n");

        printf("        link %02x:%02x:%02x:%02x:%02x:%02x\n",
               mc_list->addrs[i][0], mc_list->addrs[i][1],
               mc_list->addrs[i][2], mc_list->addrs[i][3],
               mc_list->addrs[i][4], mc_list->addrs[i][5]);
    }
    dpvs_sockopt_msg_free(mc_list);

    return EDPVS_OK;
}

static inline void calc_nic_stats_velocity(int t,
        const netif_nic_stats_get_t *start,
        const netif_nic_stats_get_t *stop,
        netif_nic_stats_get_t *velocity)
{
    int ii;
    assert(t > 0);

    velocity->ipackets = (stop->ipackets - start->ipackets)/t;
    velocity->opackets = (stop->opackets - start->opackets)/t;
    velocity->ibytes = (stop->ibytes - start->ibytes)/t;
    velocity->obytes = (stop->obytes - start->obytes)/t;
    velocity->imissed = (stop->imissed - start->imissed)/t;
    velocity->ierrors = (stop->ierrors - start->ierrors)/t;
    velocity->oerrors = (stop->oerrors - start->oerrors)/t;
    velocity->rx_nombuf = (stop->rx_nombuf - start->rx_nombuf)/t;
    for (ii = 0; ii < RTE_ETHDEV_QUEUE_STAT_CNTRS; ii++)
        velocity->q_ipackets[ii] = (stop->q_ipackets[ii] - start->q_ipackets[ii])/t;
    for (ii = 0; ii < RTE_ETHDEV_QUEUE_STAT_CNTRS; ii++)
        velocity->q_opackets[ii] = (stop->q_opackets[ii] - start->q_opackets[ii])/t;
    for (ii = 0; ii < RTE_ETHDEV_QUEUE_STAT_CNTRS; ii++)
        velocity->q_ibytes[ii] = (stop->q_ibytes[ii] - start->q_ibytes[ii])/t;
    for (ii = 0; ii < RTE_ETHDEV_QUEUE_STAT_CNTRS; ii++)
        velocity->q_obytes[ii] = (stop->q_obytes[ii] - start->q_obytes[ii])/t;
    for (ii = 0; ii < RTE_ETHDEV_QUEUE_STAT_CNTRS; ii++)
        velocity->q_errors[ii] = (stop->q_errors[ii] - start->q_errors[ii])/t;
}

static int dump_nic_stats_velocity(portid_t pid, int interval, int count)
{
    int tk = 1, err;
    size_t len = 0;
    netif_nic_stats_get_t get1, get2, velocity, *p_get = NULL;

    while (true) {
        err = dpvs_getsockopt(SOCKOPT_NETIF_GET_PORT_STATS, &pid, sizeof(pid),
                (void **)&p_get, &len);
        if (err != EDPVS_OK || !p_get || !len)
            return err;
        get1 = *p_get; 
        dpvs_sockopt_msg_free(p_get);
    
        sleep(interval);
    
        err = dpvs_getsockopt(SOCKOPT_NETIF_GET_PORT_STATS, &pid, sizeof(pid),
                (void **)&p_get, &len);
        if (err != EDPVS_OK || !p_get || !len)
            return err;
        get2 = *p_get; 
        dpvs_sockopt_msg_free(p_get);
    
        calc_nic_stats_velocity(interval, &get1, &get2, &velocity);
    
        if (g_color) {
            if (tk % 2)
                printf(BLUE);
            else
                printf(GREEN);
        }
    
        printf("    %-16s%-16s%-16s%-16s\n",
                "ipackets/pps", "opackets/pps", "ibytes/Bps", "obytes/Bps");
        printf("    %-16lu%-16lu%-16lu%-16lu\n",
                velocity.ipackets, velocity.opackets, velocity.ibytes, velocity.obytes);
        printf("    %-16s%-16s%-16s%-16s\n",
                "ierrors/pps", "oerrors/pps", "imissed/pps", "rx_nombuf/pps");
        printf("    %-16lu%-16lu%-16lu%-16lu\n",
                velocity.ierrors, velocity.oerrors, velocity.imissed, velocity.rx_nombuf);
    
        ++tk;
        if (count > 0 && tk > count)
            break;
    }

    if (g_color)
        printf(RESET);

    return EDPVS_OK;
}

static int dump_bond_status(portid_t pid)
{
    int i, err;
    size_t len = 0;
    netif_bond_status_get_t *p_get = NULL;
    if (pid < g_nports.bond_pid_base || pid > g_nports.bond_pid_end)
        return EDPVS_INVAL;

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_BOND_STATUS, &pid, sizeof(pid),
            (void **)(&p_get), &len);
    if (err != EDPVS_OK || !p_get || !len)
        return err;
    assert(len == sizeof(netif_bond_status_get_t));

    printf("    --- bonding status ---\n");
    printf("    mode %d mac_addr %s xmit_policy %s link_monitor %dms\n"
            "    ative/slaves %d/%d link_down_prop_delay %dms"
            " link_up_prop_delay %dms\n    slaves: ",
            p_get->mode,
            p_get->macaddr,
            p_get->xmit_policy,
            p_get->link_monitor_interval,
            p_get->active_nb,
            p_get->slave_nb,
            p_get->link_down_prop_delay,
            p_get->link_up_prop_delay);
    if (p_get->slave_nb > NETIF_MAX_BOND_SLAVES) {
        printf("too many slaves: %d\n", p_get->slave_nb);
        return EDPVS_INVAL;
    }
    for (i = 0; i < p_get->slave_nb; i++) {
        printf("%s(%s, %s", p_get->slaves[i].name,
                p_get->slaves[i].macaddr,
                p_get->slaves[i].is_active ? "active" : "inactive");
        if (p_get->slaves[i].is_primary)
            printf(", primary) ");
        else
            printf(") ");
    }
    printf("\n");

    return EDPVS_OK;
}

static int dump_cpu_basic(lcoreid_t cid)
{
    int err;
    size_t len = 0;
    netif_lcore_basic_get_t *p_get = NULL;

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_LCORE_BASIC, &cid, sizeof(cid),
        (void **)&p_get, &len);
    if (err != EDPVS_OK || !p_get || !len)
        return err;
    assert(len >= sizeof(netif_lcore_basic_get_t));

    printf("%d: cpu%d: socket %d\n", cid + 1, p_get->lcore_id, p_get->socket_id);
    if (p_get->queue_data_len > 0)
        printf("    queues %s\n", p_get->queue_data);
    else
        printf("    queues none\n");
    dpvs_sockopt_msg_free(p_get);

    return EDPVS_OK;
}

static int dump_cpu_stats(lcoreid_t cid)
{
    int err;
    size_t len = 0;
    netif_lcore_stats_get_t get, *p_get = NULL;

    err = dpvs_getsockopt(SOCKOPT_NETIF_GET_LCORE_STATS, &cid, sizeof(cid),
        (void **)&p_get, &len);
    if (err != EDPVS_OK || !p_get || !len)
        return err;
    assert(len == sizeof(netif_lcore_stats_get_t));
    get = *p_get;
    dpvs_sockopt_msg_free(p_get);

    printf("    %-16s%-16s%-16s%-16s\n",
            "lcore_loop", "pktburst", "zpktburst", "fpktburst");
    printf("    %-16lu%-16lu%-16lu%-16lu\n",
            get.lcore_loop, get.pktburst, get.zpktburst, get.fpktburst);

    printf("    %-16s%-16s%-16s\n",
            "z2hpktburst", "h2fpktburst", "dropped");
    printf("    %-16lu%-16lu%-16lu\n",
            get.z2hpktburst, get.h2fpktburst, get.dropped);

    printf("    %-16s%-16s%-16s%-16s\n",
            "ipackets", "ibytes", "opackets", "obytes");
    printf("    %-16lu%-16lu%-16lu%-16lu\n",
            get.ipackets, get.ibytes, get.opackets, get.obytes);

    return EDPVS_OK;
}

static int dump_cpu_verbose(lcoreid_t cid)
{
    return EDPVS_OK;
}

static inline void calc_cpu_stats_velocity(int t,
        const netif_lcore_stats_get_t *start,
        const netif_lcore_stats_get_t *stop,
        netif_lcore_stats_get_t *velocity)
{
    assert(t > 0);
    velocity->lcore_loop = (stop->lcore_loop - start->lcore_loop)/t;
    velocity->pktburst = (stop->pktburst - start->pktburst)/t;
    velocity->zpktburst = (stop->zpktburst - start->zpktburst)/t;
    velocity->fpktburst = (stop->fpktburst - start->fpktburst)/t;
    velocity->z2hpktburst = (stop->z2hpktburst - start->z2hpktburst)/t;
    velocity->h2fpktburst = (stop->h2fpktburst - start->h2fpktburst)/t;
    velocity->ipackets = (stop->ipackets - start->ipackets)/t;
    velocity->ibytes = (stop->ibytes - start->ibytes)/t;
    velocity->opackets = (stop->opackets - start->opackets)/t;
    velocity->obytes = (stop->obytes - start->obytes)/t;
    velocity->dropped = (stop->dropped - start->dropped)/t;
}

static int dump_cpu_stats_velocity(lcoreid_t cid, int interval, int count)
{
    assert(interval > 0 && count >= 0);

    int  err;
    int tk = 1;
    size_t len = 0;
    netif_lcore_stats_get_t get1, get2, *p_get = NULL;
    netif_lcore_stats_get_t velocity;

    while (true) {
        err = dpvs_getsockopt(SOCKOPT_NETIF_GET_LCORE_STATS, &cid, sizeof(cid),
                (void **)&p_get, &len);
        if (err != EDPVS_OK || !p_get || !len)
            return err;
        assert(len == sizeof(netif_lcore_stats_get_t));
        get1 = *p_get;
        dpvs_sockopt_msg_free(p_get);
        
        sleep(interval);

        err = dpvs_getsockopt(SOCKOPT_NETIF_GET_LCORE_STATS, &cid, sizeof(cid),
                (void **)&p_get, &len);
        if (err != EDPVS_OK || !p_get || !len)
            return err;
        assert(len == sizeof(netif_lcore_stats_get_t));
        get2 = *p_get;
        dpvs_sockopt_msg_free(p_get);

        calc_cpu_stats_velocity(interval, &get1, &get2, &velocity);
        
        if (g_color) {
            if (tk % 2)
                printf(BLUE);
            else
                printf(GREEN);
        }

        printf("    %-16s%-16s%-16s%-16s\n",
                "lcore_loop/lps", "pktburst/nps", "zpktburst/nps", "fpktburst/nps");
        printf("    %-16lu%-16lu%-16lu%-16lu\n",
                velocity.lcore_loop, velocity.pktburst, velocity.zpktburst, velocity.fpktburst);

        printf("    %-16s%-16s%-16s\n",
                "z2hpktburst/nps", "h2fpktburst/nps", "dropped/nps");
        printf("    %-16lu%-16lu%-16lu\n",
                velocity.z2hpktburst, velocity.h2fpktburst, velocity.dropped);
        
        printf("    %-16s%-16s%-16s%-16s\n",
                "ipackets/pps", "ibytes/Bps", "opackets/pps", "obytes/Bps");
        printf("    %-16lu%-16lu%-16lu%-16lu\n",
                velocity.ipackets, velocity.ibytes, velocity.opackets, velocity.obytes);

        tk++;
        if (count > 0 && tk > count)
            break;
    }

    if (g_color)
        printf(RESET);

    return EDPVS_OK;
}

static int link_nic_show(portid_t pid, const struct link_param *param)
{
    int err;
    if ((err = dump_nic_basic(pid)) != EDPVS_OK)
        return err;
    if (param->stats.enabled) {
        if (!param->stats.interval) {
            if((err = dump_nic_stats(pid)) != EDPVS_OK)
                return err;
        } else {
            /* FIXME: possible infinite loop here  */
            if ((err = dump_nic_stats_velocity(pid, param->stats.interval,
                            param->stats.count)) != EDPVS_OK)
                return err;
        }
    }
    if (param->verbose)
        if ((err = dump_nic_verbose(pid)) != EDPVS_OK)
            return err;
    if (param->status) {
        if ((err = dump_bond_status(pid) != EDPVS_OK))
            return err;
    }
    return EDPVS_OK;
}

static int link_cpu_show(lcoreid_t cid, const struct link_param *param)
{
    int err;
    if ((err = dump_cpu_basic(cid)) != EDPVS_OK)
        return err;
    if (param->stats.enabled) {
        if (!param->stats.interval) {
            if((err = dump_cpu_stats(cid)) != EDPVS_OK)
                return err;
        } else {
            /* FIXME: possible infinite loop here */
            if ((err = dump_cpu_stats_velocity(cid, param->stats.interval,
                            param->stats.count)) != EDPVS_OK)
                return err;
        }
    }
    if (param->verbose)
        if ((err = dump_cpu_verbose(cid)) != EDPVS_OK)
            return err;
    return EDPVS_OK;
}

static int link_show(struct link_param *param)
{
    assert(param);

    int ii, err, ret, cnt;
    unsigned id = 0;
    bool dev_specified = false;
    netif_lcore_mask_get_t lcores, *p_lcores = NULL;
    size_t len = 0;

    switch (param->dev_type) {
        case LINK_DEVICE_NIC:
        {
            if (get_netif_port_nb() < 0)
                return EDPVS_INVAL;

            if (param->dev_name[0]) { /* device name specified */
                if (link_check_dev_name(LINK_DEVICE_NIC, param->dev_name, &id)) {
                    link_help();
                    return EDPVS_INVAL;
                }
                dev_specified = true;
            }
            if (dev_specified) { /* show information of specified NIC */
                if (id < g_nports.nic_num)
                    return link_nic_show(id, param);
                else {
                    fprintf(stderr, "Device %s not exist.\n", param->dev_name);
                    return EDPVS_NOTEXIST;
                }
            } else { /* show infomation of all NIC */
                ret = EDPVS_OK;
                for (ii = 0; ii < g_nports.nic_num; ii++) {
                    snprintf(param->dev_name, sizeof(param->dev_name), "dpdk%d", ii);
                    err = link_nic_show(ii, param);
                    if (err) {
                        fprintf(stderr, "Fail to get information for dpdk%d\n", ii);
                        ret = err;
                    }
                }
                return ret;
            }
            break;
        }
        case LINK_DEVICE_CPU:
        {
            err = dpvs_getsockopt(SOCKOPT_NETIF_GET_LCORE_MASK, NULL, 0,
                    (void **)&p_lcores, &len);
            if (EDPVS_OK != err || !p_lcores || !len) {
                fprintf(stderr, "Fail to get configured CPU information: %s.\n",
                        dpvs_strerror(err));
                return err;
            }
            lcores = *p_lcores;
            dpvs_sockopt_msg_free((void *)p_lcores);

            if (param->dev_name[0]) { /* device name speicified */
                if(link_check_dev_name(LINK_DEVICE_CPU, param->dev_name, &id)) {
                    link_help();
                    return EDPVS_INVAL;
                }
                dev_specified = true;
            }
            if (dev_specified) { /* show information of specified CPU */
                if (id == lcores.master_lcore_id ||
                        lcores.slave_lcore_mask & (1L << id) ||
                        lcores.isol_rx_lcore_mask & (1L << id))
                    return link_cpu_show(id, param);
                else {
                    fprintf(stderr, "Device %s not exist.\n", param->dev_name);
                    return EDPVS_NOTEXIST;
                }
            } else { /* show information of all CPU */
                ret = EDPVS_OK;

                printf("<< Controll Plane >>\n");
                err = link_cpu_show(lcores.master_lcore_id, param);
                if (err) {
                    fprintf(stderr, "Fail to get information for Master cpu%d\n",
                            lcores.master_lcore_id);
                    ret = err;
                }

                printf("<< Data Plane >>\n");
                cnt = 0;
                for (ii = 0; cnt <= lcores.slave_lcore_num &&
                        ii <= NETIF_MAX_LCORES; ii++) {
                    if (lcores.slave_lcore_mask & (1L << ii)) {
                        err = link_cpu_show(ii, param);
                        if (err) {
                            fprintf(stderr, "Fail to get information for cpu%d\n", ii);
                            ret = err;
                        }
                        cnt++;
                    }
                }

                if (lcores.isol_rx_lcore_num > 0) {
                    cnt = 0;
                    printf("<< Isolate RX Lcores >>\n");
                    for (ii = 0; cnt <= lcores.isol_rx_lcore_num &&
                            ii <= NETIF_MAX_LCORES; ii++) {
                        if (lcores.isol_rx_lcore_mask & (1L << ii)) {
                            err = link_cpu_show(ii, param);
                            if (err) {
                                fprintf(stderr, "Fail to get information for cpu%d\n", ii);
                                ret = err;
                            }
                            cnt++;
                        }
                    }
                }
                return ret;
            }
            break;
        }
        default:
        {
            fprintf(stderr, "Unsupported device type.\n");
            return EDPVS_NOTSUPP;
        }
    }
    return EDPVS_OK;
}

static int link_nic_set_promisc(const char *name, const char *value)
{
    assert(value);

    netif_nic_set_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.pname, name, sizeof(cfg.pname) - 1);
    if (strcmp(value, "on") == 0)
        cfg.promisc_on = 1;
    else if(strcmp(value, "off") == 0)
        cfg.promisc_off = 1;
    else {
        fprintf(stderr, "invalid arguement value for 'promisc'\n");
        return EDPVS_INVAL;
    }

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_PORT, &cfg, sizeof(netif_nic_set_t));
}

static int link_nic_set_forward2kni(const char *name, const char *value)
{
    assert(value);

    netif_nic_set_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.pname, name, sizeof(cfg.pname) - 1);
    if (strcmp(value, "on") == 0)
        cfg.forward2kni_on = 1;
    else if(strcmp(value, "off") == 0)
        cfg.forward2kni_off = 1;
    else {
        fprintf(stderr, "invalid arguement value for 'forward2kni'\n");
        return EDPVS_INVAL;
    }

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_PORT, &cfg, sizeof(netif_nic_set_t));
}

static int link_nic_set_link_status(const char *name, const char *value)
{
    assert(value);

    netif_nic_set_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.pname, name, sizeof(cfg.pname) - 1);
    if (strcmp(value, "up") == 0)
        cfg.link_status_up = 1;
    else if (strcmp(value, "down") == 0)
        cfg.link_status_down = 1;
    else {
        fprintf(stderr, "invalid argument value for 'link'\n");
        return EDPVS_INVAL;
    }

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_PORT, &cfg, sizeof(netif_nic_set_t));
}

static int link_nic_set_addr(const char *name, const char *value)
{
    assert(value);

    netif_nic_set_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.pname, name, sizeof(cfg.pname) - 1);
    strncpy(cfg.macaddr, value, sizeof(cfg.macaddr) - 1);

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_PORT, &cfg, sizeof(netif_nic_set_t));
}

static int link_bond_add_bond_slave(const char *name, const char *value)
{
    unsigned dev_id;
    netif_bond_set_t cfg;

    if (link_check_dev_name(LINK_DEVICE_NIC, value, &dev_id)) {
        printf("invalid device name: %s\n", value);
        return EDPVS_INVAL;
    }

    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    strncpy(cfg.param.slave, value, sizeof(cfg.param.slave) - 1);

    cfg.act = ACT_ADD;
    cfg.opt = OPT_SLAVE;

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_BOND, &cfg, sizeof(netif_bond_set_t));
}

static int link_bond_del_bond_slave(const char *name, const char *value)
{
    unsigned dev_id;
    netif_bond_set_t cfg;

    if (link_check_dev_name(LINK_DEVICE_NIC, value, &dev_id)) {
        printf("invalid device name: %s\n", value);
        return EDPVS_INVAL;
    }

    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    strncpy(cfg.param.slave, value, sizeof(cfg.param.slave) - 1);

    cfg.act = ACT_DEL;
    cfg.opt = OPT_SLAVE;

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_BOND, &cfg, sizeof(netif_bond_set_t));
}

static int link_bond_set_mode(const char *name, const char *value)
{
    char *endptr;
    int mode;
    netif_bond_set_t cfg;

    assert(value);
    mode = strtol(value, &endptr, 10);
    if (*endptr || mode < 0 || mode > 6) {
        printf("invalid bonding mode: %s\n", value);
        return EDPVS_INVAL;
    }

    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    cfg.param.mode = mode;

    cfg.act = ACT_SET;
    cfg.opt = OPT_MODE;

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_BOND, &cfg, sizeof(netif_bond_set_t));
}

static int link_bond_set_primary(const char *name, const char *value)
{
    unsigned dev_id;
    netif_bond_set_t cfg;

    if (link_check_dev_name(LINK_DEVICE_NIC, value, &dev_id)){
        printf("invalid device name: %s\n", value);
        return EDPVS_INVAL;
    }

    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    strncpy(cfg.param.primary, value, sizeof(cfg.param.primary) - 1);

    cfg.act = ACT_SET;
    cfg.opt = OPT_PRIMARY;

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_BOND, &cfg, sizeof(netif_bond_set_t));
}

static int link_bond_set_xmit_policy(const char *name, const char *value)
{
    netif_bond_set_t cfg;

    assert(value);
    if (strcmp(value, "LAYER2") && strcmp(value, "layer2") &&
            strcmp(value, "LAYER23") && strcmp(value, "layer23") &&
            strcmp(value, "LAYER34") && strcmp(value, "layer34")) {
        printf("invalid xmit-poliy: %s\n", value);
        return EDPVS_INVAL;
    }

    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    strncpy(cfg.param.xmit_policy, value, sizeof(cfg.param.xmit_policy) - 1);

    cfg.act = ACT_SET;
    cfg.opt = OPT_XMIT_POLICY;

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_BOND, &cfg, sizeof(netif_bond_set_t));
}

static int link_bond_set_monitor_interval(const char *name, const char *value)
{
    char *endptr;
    int mi;
    netif_bond_set_t cfg;

    assert(value);
    mi = strtol(value, &endptr, 10);
    if (*endptr || mi <= 0) {
        printf("invalid link_monitor_interval: %s\n", value);
        return EDPVS_INVAL;
    }

    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    cfg.param.link_monitor_interval = mi;

    cfg.act = ACT_SET;
    cfg.opt= OPT_LINK_MONITOR_INTERVAL;

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_BOND, &cfg, sizeof(netif_bond_set_t));
}

static int link_bond_set_link_down_prop(const char *name, const char *value)
{
    char *endptr;
    int ldp;
    netif_bond_set_t cfg;

    assert(value);
    ldp = strtol(value, &endptr, 10);
    if (*endptr || ldp < 0) {
        printf("invalid link_down_prop: %s\n", value);
        return EDPVS_INVAL;
    }

    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    cfg.param.link_down_prop = ldp;

    cfg.act = ACT_SET;
    cfg.opt = OPT_LINK_DOWN_PROP;

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_BOND, &cfg, sizeof(netif_bond_set_t));
}

static int link_bond_set_link_up_prop(const char *name, const char *value)
{
    char *endptr;
    int lup;
    netif_bond_set_t cfg;

    assert(value);
    lup = strtol(value, &endptr, 10);
    if (*endptr || lup < 0) {
        printf("invalid link_up_prop: %s\n", value);
        return EDPVS_INVAL;
    }

    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    cfg.param.link_up_prop = lup;

    cfg.act = ACT_SET;
    cfg.opt = OPT_LINK_UP_PROP;

    return dpvs_setsockopt(SOCKOPT_NETIF_SET_BOND, &cfg, sizeof(netif_bond_set_t));
}

static int link_add(struct link_param *param)
{
    unsigned dev_id;
    assert(param);

    switch (param->dev_type) {
        case LINK_DEVICE_NIC:
        {
            if (get_netif_port_nb() < 0)
                return EDPVS_INVAL;
            if (link_check_dev_name(LINK_DEVICE_NIC, param->dev_name, &dev_id) < 0) {
                fprintf(stderr, "invalid device name '%s'\n", param->dev_name);
                return EDPVS_INVAL;
            }

            if (strcmp(param->item, "bond-slave") == 0)
                link_bond_add_bond_slave(param->dev_name, param->value);
            break;
        }
        case LINK_DEVICE_CPU:
        {
            if (link_check_dev_name(LINK_DEVICE_CPU, param->dev_name, &dev_id)) {
                fprintf(stderr, "invalid device name '%s'\n", param->dev_name);
                return EDPVS_INVAL;
            }

            break;
        }
        default:
        {
            fprintf(stderr, "Unsupported device type.\n");
            return EDPVS_NOTSUPP;
        }
    }

    return EDPVS_OK;
}

static int link_del(struct link_param *param)
{
    unsigned dev_id;
    assert(param);

    switch (param->dev_type) {
        case LINK_DEVICE_NIC:
        {
            if (get_netif_port_nb() < 0)
                return EDPVS_INVAL;
            if (link_check_dev_name(LINK_DEVICE_NIC, param->dev_name, &dev_id) < 0) {
                fprintf(stderr, "invalid device name '%s'\n", param->dev_name);
                return EDPVS_INVAL;
            }

            if (strcmp(param->item, "bond-slave") == 0)
                link_bond_del_bond_slave(param->dev_name, param->value);
            break;
        }
        case LINK_DEVICE_CPU:
        {
            if (link_check_dev_name(LINK_DEVICE_CPU, param->dev_name, &dev_id)) {
                fprintf(stderr, "invalid device name '%s'\n", param->dev_name);
                return EDPVS_INVAL;
            }

            break;
        }
        default:
        {
            fprintf(stderr, "Unsupported device type.\n");
            return EDPVS_NOTSUPP;
        }
    }

    return EDPVS_OK;
}

static int link_set(struct link_param *param)
{
    unsigned dev_id;
    assert(param);

    switch (param->dev_type) {
        case LINK_DEVICE_NIC:
        {
            if (get_netif_port_nb() < 0)
                return EDPVS_INVAL;
            if (link_check_dev_name(LINK_DEVICE_NIC, param->dev_name, &dev_id) < 0) {
                fprintf(stderr, "invalid device name '%s'\n", param->dev_name);
                return EDPVS_INVAL;
            }

            if (strcmp(param->item, "promisc") == 0)
                link_nic_set_promisc(param->dev_name, param->value);
            else if (strcmp(param->item, "forward2kni") == 0)
                link_nic_set_forward2kni(param->dev_name, param->value);
            else if (strcmp(param->item, "link") == 0)
                link_nic_set_link_status(param->dev_name, param->value);
            else if (strcmp(param->item, "addr") == 0)
                link_nic_set_addr(param->dev_name, param->value);
            else if (strcmp(param->item, "bond-mode") == 0)
                link_bond_set_mode(param->dev_name, param->value);
            else if (strcmp(param->item, "bond-primary") == 0)
                link_bond_set_primary(param->dev_name, param->value);
            else if (strcmp(param->item, "bond-xmit-policy") == 0)
                link_bond_set_xmit_policy(param->dev_name, param->value);
            else if (strcmp(param->item, "bond-monitor-interval") == 0)
                link_bond_set_monitor_interval(param->dev_name, param->value);
            else if (strcmp(param->item, "bond-link-down-prop") == 0)
                link_bond_set_link_down_prop(param->dev_name, param->value);
            else if (strcmp(param->item, "bond-link-up-prop") == 0)
                link_bond_set_link_up_prop(param->dev_name, param->value);
            break;
        }
        case LINK_DEVICE_CPU:
        {
            if (link_check_dev_name(LINK_DEVICE_CPU, param->dev_name, &dev_id)) {
                fprintf(stderr, "invalid device name '%s'\n", param->dev_name);
                return EDPVS_INVAL;
            }

            break;
        }
        default:
        {
            fprintf(stderr, "Unsupported device type.\n");
            return EDPVS_NOTSUPP;
        }
    }
    return EDPVS_OK;
}

static int link_do_cmd(struct dpip_obj *obj, dpip_cmd_t cmd,
                       struct dpip_conf *conf)
{
    struct link_param param;

    g_color = conf->color;

    if (link_parse_args(conf, &param) != EDPVS_OK) {
        link_help();
        return EDPVS_INVAL;
    }

    switch (conf->cmd) {
    case DPIP_CMD_ADD:
        return link_add(&param);
    case DPIP_CMD_DEL:
        return link_del(&param);
    case DPIP_CMD_SET:
        return link_set(&param);
    case DPIP_CMD_FLUSH:
        return EDPVS_NOTSUPP;
    case DPIP_CMD_SHOW:
        return link_show(&param);
    default:
        return EDPVS_NOTSUPP;
    }
}

struct dpip_obj dpip_link = {
    .name    = "link",
    .do_cmd = link_do_cmd,
    .help = link_help,
};

static void __init addr_init(void)
{
    dpip_register_obj(&dpip_link);
} 

static void __exit addr_exit(void)
{
    dpip_unregister_obj(&dpip_link);
}
