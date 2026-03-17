interface := "enp14s0u1u4"
namespace := "ns10"
vlan_id1 := "10"
gateway := "192.168.10.1"
mask := "24"
bind_address := "10.10.10.112"
bind_port := "12345"
namespace_bind_address := "192.168.10.2"
namespace_bind_port := "12345"
target_address := "10.10.10.222"
target_port := "12345"
target_namespace_address := "192.168.10.139"
target_namespace_port := "12345"
vlan_name := interface + "." + vlan_id1

setup:
    sudo ip netns add {{namespace}}
    sudo ip link add link {{interface}} name {{vlan_name}} type vlan id {{vlan_id1}}
    sudo ip link set {{vlan_name}} netns {{namespace}}
    sudo ip netns exec {{namespace}} ip link set {{vlan_name}} up
    sudo ip netns exec {{namespace}} ip link set lo up
    sudo ip netns exec {{namespace}} ip addr add {{namespace_bind_address}}/{{mask}} dev {{vlan_name}}
    sudo ip netns exec {{namespace}} ip route add default via {{gateway}} dev {{vlan_name}}

teardown:
    sudo ip netns del {{namespace}}

build target="namespace":
    gcc -xc -Wall -Wextra -Werror -std=c23 -fuse-ld=mold -fsanitize=address -g -D_GNU_SOURCE {{target}}.c -o {{target}}

_build_no_asan:
    gcc -xc -Wall -Wextra -Werror -std=c23 -fuse-ld=mold -g -D_GNU_SOURCE main.c -o namespace

run: (build "namespace")
    sudo ./namespace {{namespace}} {{bind_address}} {{bind_port}} {{namespace_bind_address}} {{namespace_bind_port}} {{target_address}} {{target_port}} {{target_namespace_address}} {{target_namespace_port}}

valgrind: _build_no_asan
    valgrind -s --leak-check=full ./namespace {{namespace}} {{bind_address}} {{bind_port}} {{namespace_bind_address}} {{namespace_bind_port}} {{target_address}} {{target_port}} {{target_namespace_address}} {{target_namespace_port}}
