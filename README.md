# Isolating router

All started when ISP support wondered looking at their router's main page
"Wow, how many computers in your house!"

They know my address. Even if all those "computers" are LXC containers on a couple of RPI,
it's not wise to tease underpaid employees in Honduras.

## Design principles

1. Let's assume the LAN on ISP's router side is `public` and the LAN behind isolating router
   is `private`.
1. The router must be transparent and optional.
   It's primary purpose is to simply hide the details of your home network from your ISP.
   The home network should continue functioning without it, just with less degree of security.
1. A machine with statically configured address should be pluggable at the either side without re-configuration.
   This means both private and public subnets must be the same, 192.168.0.0/24.
1. The router should expose a single IP address on the public side: 192.168.0.2.
1. On the private side the router has at least two IP addresses:
   * 192.168.0.1: default gateway and DNS resolver, similar to ISP router
   * 192.168.0.2: VPN gateway and safe DNS resolver which works via VPN.
   * other optional addresses can be used to DNAT to specific machines in the public LAN.
1. On the public side the router should be a VPN gateway.
1. The VPN is primarily intended for SmartTVs to be able to play all those youtubes.
   The VPN should be automagically bypassed for domestic addresses and for "friendly" countries.
1. If the router is seized or stolen, there should be no evidence of using VPN.
1. The VPN is bootstrapped externally, using plausible deniability setup.
1. After power on, VPN should be down and 192.168.0.2 on the public side
   simply forwards traffic to ISP's router 192.168.0.1.
1. As long as WiFi is very often provided by ISP router and it makes no point to bring up
   another one on the private side, the router provides a Wireguard interface on the public side.
   That's mainly for IoT devices, but it also simplifies smartphone connnection because Wireguard
   client gets correct gateway and DNS from the router. Thus, smartphones can easily reach the IoT.

## Simplified diagram

```
     ISP router
     192.168.0.1
         |
     Public LAN
         |
+------ end0 ------+
|   192.168.0.2    |
|                  |
| Hardware:        |
| NanoPi R2S       |
|                  |
| VPN gateway:     |
|   192.168.0.2    |
| Normal gateway:  |
|   192.168.0.1    |
+------ eth0 ------+
         |
    Private LAN
```

## Full Diagram

```
                          ISP router: 192.168.0.1
                                     |
                                  Public LAN
                                192.168.0.0/24
                                     |
                                    end0
                                 192.168.0.2
                               NAT▲, DNS DNAT▼
                                     |
        +-------------+--------------+-------------------+----------------+--------+------------+
        |             |              |                   |                |        |            |
        |             |              |         fwmark transit traffic   LISTEN   LISTEN         |
    router_pub     vpn_pub       bypass_pub              |               SSH   port 10999       |
   192.168.200.1 192.168.210.1  192.168.212.1   ROUTE based on fwmark              |            |
       NAT▼          NAT▼            |              /           \                  |  DNAT▲ to 192.168.0.NNN
        |             |              |          table 1       table 2              |            |
        |             |              |             |             |                 |       dnat_NNN_pub
        |             |              |      router_transit   vpn_transit           |       192.168.1xx.1
        |             |              |      192.168.201.1   192.168.211.1          |            |
        |             |              |            NAT▼          NAT▼               |            |
+-LXC---+--router----------------------+           |             |                 |            |
|       |                DNS resolver  |           |             |                 |            |
|  router_out            + rpz zones   |           |             |       +--netns--+-iot-----+  |
| 192.168.200.2             master     |           |             |       |         |         |  |
|      NAT▲                            |           |             |       |        wg0        |  |
|       |                192.168.201.2 |           |             |       |    192.168.1.1    |  |
|       |<----------------router_tr_in-+-----------+             |       |                   |  |
|       |                              |                         |       |                   |  |
|       |<---------------192.168.202.1-+-------------------------|-------+-192.168.202.2     |  |
|       |                router_itr_in |                         |       | router_itransit   |  |
|   router_in                          |                         |       |                   |  |
|  192.168.0.1                         |                         |   +---+-192.168.213.2     |  |
|       |                              |                         |   |   | vpn_itransit      |  |
+-------+------------------------------+                         |   |   |                   |  |
        |             |              |                           |   |   +-------------------+  |
        |             |              |                           |   |                          |
        |     +-LXC---+--vpn---------+----------------------+    |   |                          |
        |     |       |              |        DNS resolver  |    |   |                          |
        |     |    vpn_out      bypass_out    + rpz zones   |    |   |                          |
        |     | 192.168.210.2  192.168.212.2     slave      |    |   |                          |
        |     |       |             NAT▲                    |    |   |                          |
        |     |      VPN            /     ____  vpn_tr_in---+----+   |                          |
        |     |       |            /     /    192.168.211.2 |        |                          |
        |     |  GeoIP fwmark routing <--                   |        |                          |
        |     |       |                  \____  vpn_itr_in  |        |                          |
        |     |       +--> SSH DNAT▼          192.168.213.1-+--------+                          |
        |     |       |           \                         |                                   |
        |     |       |           NAT▼                      |                                   |
        |     |  veth_private   veth_ssh                    |                                   |
        |     |  192.168.0.2  192.168.254.2                 |                                   |
        |     |       |            |                        |                                   |
        |     +-------+------------+------------------------+                                   |
        |             |            |                                                            |
        |             |            |when VPN is up                                              |
        |             |            |                                                            |
        |             |       veth_ssh_lsnr -- LISTEN SSH                                       |
        |             |       192.168.254.1                                                     |
        |             |            |                                                            |
        |             |  +-netns---+-ssh_dnat-+                                     +-netns-----+-dnat_NNN--+
        |             |  |         |          |                                     |           |           |
        |             |  |      veth_ssh      |                                     |     dnat_NNN_out      |
        |             |  |    192.168.254.2   |                                     |     192.168.1xx.2     |
        |             |  |        NAT▲        |                                     |          NAT▲         |
        |             |  |         |          |                                     |           |           |
        |             |  |      SSH DNAT▲     |                                     | DNAT to 192.168.1xx.1 |
        |             |  |         |          |                                     |           |           |
        |             |  |    veth_private    |                                     |      dnat_NNN_in      |
        |             |  |    192.168.0.2     |                                     |     192.168.0.NNN     |
        |             |  |         |          |                                     |           |           |
        |             |  +---------+----------+                                     +-----------+-----------+
        |             |  _________/                                                             |
        |             | /     when VPN is down                                                  |
        |             |/                                                                        |
        |             |             +-----------------------------------------------------------+
        |             |             |
+-netns-+-private-----+-------------+---------+
|       |             |             |         |
| +-br0-+-------------+-------------+-------+ |
| |     |             |             |       | |
| | router_br  veth_private_br  dnat_NNN_br | |
| |     |             |             |       | |
| |     +-------------+-------------+       | |
| |                   |                     | |
| |                  eth0                   | |
| |                   |                     | |
| +-------------------+---------------------+ |
|                     |                       |
+---------------------+-----------------------+
                      |
                  Private LAN
                 192.168.0.0/24
```

## Implementation notes

1. Public `end0` interface is configured with `ifupdown`.
   This makes things robust if networking setup fails.
1. As long as the router has multiple IP addresses on the private side, use bridge for that.
1. Although `private` namespace seems to be unnecessary because of no routing in it,
   it's required to isolate ARP.
1. Assigning MAC addresses to veth interfaces is important to avoid problems and delays with ARP.
   For ease of tracking MAC addresses assignment, their last four octets are equal to IP addresses.
   The first octet has only "locally administered" bit set and equals to 0x02.
   The second octet is 0xbb if veth peer is a part of bridge and has no IP address assigned.
   For example: `router_in` MAC address is `02:00:c0:a8:00:01`
   and `router_br` MAC address is `02:bb:c0:a8:00:01`.
   Bridge MAC addresses are in the form `02:bb:cc:dd:00:NN` where `NN` is a bridge number.
1. LXC containers are not absolutely necessary, but they are very convenient to run unprivileged
   DNS, VPN, and other services.
   And they are fucking convenient for plausible deniability setups.
1. The traffic sent to the gateway address `192.168.0.1` flows to the other side thanks to double NAT.
   The first NAT takes place in the `router` container when it leaves `router_out` interface.
   The second NAT is performed in the root namespace when the traffic leaves `end0` interface.
   VPN bypass uses the same scheme.
1. DNS DNAT on `end0` interface routes requests to `router_out` or `vpn_out` interfaces where
   DNS server/resolver is listening too. NAT is required on `router_pub`/`vpn_pub` because
   packets are coming from public LAN which is same as private LAN.
1. For any DNAT it's important to specify `ip daddr` to avoid interfering with transit traffic.
1. SSH runs in the root namespace and listens on both sides:
   * directly on `end0` on the public side
   * on `veth_ssh_lsnr` which is connected to the private side either via `ssh_dnat` namespace
     or via `vpn` container.
1. The networking is configured with `/etc/init.d/basic-networking`.
   Configuration parameters are defined in `/etc/default/basic-networking`.
   This script creates only veth_private and veth_ssh pairs to make SSH accessible
   from the private LAN.
   Other veth interfaces are created by LXC hooks on demand.
   The principle of such a separation is simple:
   * container is up: interfaces exist, routing is working
   * container is down: no interfaces, no routing
1. Interfaces for `router` and `vpn` LXC containers are created by
   `/var/lib/lxc/{container-name}/networking` script which is invoked as a hook.
   See `/var/lib/lxc/{container-name}/config` for details.
1. Files where IP/MAC addresses may appear:
   1. `/etc/network/interfaces`
   1. `/etc/default/basic-networking`
   1. `/etc/nftables.conf`: fwmark rules
   1. `/etc/nftables-ssh-dnat.conf`: SSH DNAT address
   1. `/var/lib/lxc/router/config`
   1. `/var/lib/lxc/router/networking`
   1. `/var/lib/lxc/router/rootfs/etc/bind/named.conf.local`: zone transfer settings
   1. `/var/lib/lxc/vpn/config`
   1. `/var/lib/lxc/vpn/networking`
   1. `/var/lib/lxc/vpn/rootfs/etc/nftables.conf`: SSH DNAT address
   1. `/var/lib/lxc/vpn/rootfs/etc/runit/boot-run/10-tunsetup.sh`: VPN address
   1. `/var/lib/lxc/vpn/rootfs/etc/runit/boot-run/50-routes.sh`: fwmark routing
   1. `/var/lib/lxc/vpn/rootfs/etc/bind/named.conf.local`: zone transfer settings
1. On the public side the router acts as a gateway, accepting transit traffic
   on `end0`. This traffic is marked as 0x6aea and routed to `router` or `vpn`
   container using an IP rule and a separate routing table 1 or 2.
1. Wireguard interfaces can't be bridged, so using veth pairs to connect them to containers.
1. Containers are reachable from each other and from outside via `iot` namespace.

## Important!

Because some interfaces are created on demans, not all netfilter rules are in `/etc/nftables.conf` on the host system.
See `/var/lxc/{container-name}/networking`.

**After making changes, restart the container!**

## Advanced settings

### DNAT from public to private LAN

Easy done via double DNAT: the first is at `end0` interface, similar to DNS DNAT, to the `router` container.
The second one is within container to a host in the private LAN.

Use public->private DNAT with caution to minimize risks of attacks.
Place shitty services such as Samba to the public LAN.

### DNAT from private to public LAN

For example, port 80 to the ISP router: first DNAT in the `router` container to `router_pub` 192.168.200.1,
the second one at the `router_pub` interface.

For relaying traffic to hosts that are always in public LAN, an additional interfaces can be added to `br0`.
One stage of double NAT is performed in a separate namespace.

### DNS

The router runs master DNS server which provides the following zones:
* rpz-filter: based on https://github.com/StevenBlack/hosts and https://www.circuitshelter.com/posts/bind9-rpz-firewall.
* rpz-home: host names in the IoT network.
* onion: prevents all requests for this zone from leaking outside.

### Bypass VPN using GeoIP database

The simplest way is based on slightly modified version of https://github.com/pvxe/nftables-geoip
and it's not the best one. There's a lots of IP subnets and the router is tiny.
Had to set minimal CPU frequency to 816MHz in `less /etc/default/cpufrequtils`.

Another approach is building two sets of IP addresses dynamically.
One set contains addresses for which the VPN should be bypassed.
Another set is for addresses routed via VPN.
If an address is not in any set, the packet is queued to userspace where a program,
namely [ip4_classifier.c](ip4_classifier.c), classifies it
and adds to the appropriate set.

The program requires a bitmap for class C networks which can be prepared with
[make_ip4_country_bitmap.c](make_ip4_country_bitmap.c).
That's not an ideal solution, however, it's the simplest and fastest one,
I don't need IPv6, and I haven't seen free geoip databases with better accuracy.

Packet classification takes place in prerouting chain by setting fwmark:
```
set bypass_vpn {
    type ipv4_addr
    flags timeout
    elements = {
        127.0.0.1  # dummy element, set can't be empty
    }
}

set via_vpn {
    type ipv4_addr
    flags timeout
    elements = {
        127.0.0.1  # dummy element, set can't be empty
    }
}

chain prerouting {
    type filter hook prerouting priority 0; policy accept;

    iif { vpn_tr_in, vpn_itr_in, veth_private } ip daddr 192.168.0.0/24 accept

    iif { vpn_tr_in, vpn_itr_in, veth_private } ip daddr @bypass_vpn meta mark set 0xd0 accept
    iif { vpn_tr_in, vpn_itr_in, veth_private } ip daddr @via_vpn accept
    iif { vpn_tr_in, vpn_itr_in, veth_private } queue
}
```

Runit script for `ip4_classifier`:
```
#!/bin/sh

exec 1>&2

if [ -e /etc/runit/verbose ]; then
        echo "invoke-run: starting ${PWD##*/}"
fi

exec /root/ip4_classifier /etc/ip4_ru_map inet filter bypass_vpn via_vpn
```

Where `/etc/ip4_ru_map` is a bitmap generated by `make_ip4_country_bitmap`.
