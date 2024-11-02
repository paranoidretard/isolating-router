# iot interconnection routes

ip route add 192.168.202.0/24 dev transit_iot
ip route add 192.168.213.0/24 via 192.168.202.2 dev transit_iot
ip route add 192.168.1.0/24   via 192.168.202.2 dev transit_iot
