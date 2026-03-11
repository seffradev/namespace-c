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

build target="namespace":
    gcc -xc -Wall -Wextra -Werror -std=c23 -fuse-ld=mold -fsanitize=address -g -D_GNU_SOURCE {{target}}.c -o {{target}}

_build_no_asan:
    gcc -xc -Wall -Wextra -Werror -std=c23 -fuse-ld=mold -g -D_GNU_SOURCE main.c -o namespace

run: (build "namespace")
    sudo ./namespace {{namespace}} {{bind_address}} {{bind_port}} {{target_address}} {{target_port}} {{tun_name}}

valgrind: _build_no_asan
    valgrind -s --leak-check=full ./namespace {{namespace}} {{bind_address}} {{bind_port}} {{target_address}} {{target_port}} {{tun_name}}
