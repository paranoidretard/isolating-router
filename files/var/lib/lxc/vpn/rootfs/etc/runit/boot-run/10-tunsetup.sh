ip tuntap add dev tun0 mode tun
ip address add 10.10.0.2 dev tun0
ip link set tun0 up
ip route add 10.10.0.0/24 dev tun0
ip route replace default via 10.10.0.1 dev tun0
