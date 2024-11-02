# iot interconnection routes

ip route add 192.168.213.0/24 dev transit_iot
ip route add 192.168.202.0/24 via 192.168.213.2 dev transit_iot
ip route add 192.168.1.0/24   via 192.168.213.2 dev transit_iot

# route marked packets via bypass interface

ip rule add fwmark 0xd0 table 1 pref 100

ip route add 192.168.210.0/24 dev public table 1
ip route add 192.168.212.0/24 dev bypass table 1
ip route add 192.168.211.0/24 dev transit table 1
ip route add 192.168.213.0/24 dev transit_iot table 1

ip route add default via 192.168.212.1 dev bypass table 1
