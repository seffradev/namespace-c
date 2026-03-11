interface := "enp14s0u1u4"
namespace := "ns10"
vlan_id1 := "10"
gateway := "192.168.10.1"
mask := "24"
bind_address := "192.168.10.2"
bind_port := "12345"
target_address := "192.168.10.139"
target_port := "12345"
tun_name := "tun10"
tun_ip := "192.168.30.2"
tun_mask := "24"
tun_net := tun_ip + "/" + tun_mask
vlan_name := interface + "." + vlan_id1

setup:
    sudo ip netns add {{namespace}}

    sudo ip tuntap add mode tun dev {{tun_name}}
    sudo ip addr add {{tun_net}} dev {{tun_name}}
    sudo ip link set dev {{tun_name}} up

    sudo ip link add link {{interface}} name {{vlan_name}} type vlan id {{vlan_id1}}
    sudo ip link set {{vlan_name}} netns {{namespace}}
    sudo ip netns exec {{namespace}} ip link set {{vlan_name}} up
    sudo ip netns exec {{namespace}} ip link set lo up
    sudo ip netns exec {{namespace}} ip addr add {{bind_address}}/{{mask}} dev {{vlan_name}}
    sudo ip netns exec {{namespace}} ip route add default via {{gateway}} dev {{vlan_name}}

    sudo ip route add 192.168.10.0/24 via {{tun_ip}} dev {{tun_name}}

teardown:
    sudo ip netns del {{namespace}}
    sudo ip tuntap del mode tun dev {{tun_name}}

build target="namespace":
    gcc -xc -Wall -Wextra -Werror -std=c23 -fuse-ld=mold -fsanitize=address -g -D_GNU_SOURCE {{target}}.c -o {{target}}

_build_no_asan:
    gcc -xc -Wall -Wextra -Werror -std=c23 -fuse-ld=mold -g -D_GNU_SOURCE main.c -o namespace

run: (build "namespace")
    sudo ./namespace {{namespace}} {{bind_address}} {{bind_port}} {{target_address}} {{target_port}} {{tun_name}}

valgrind: _build_no_asan
    valgrind -s --leak-check=full ./namespace {{namespace}} {{bind_address}} {{bind_port}} {{target_address}} {{target_port}} {{tun_name}}
