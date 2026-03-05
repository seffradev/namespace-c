interface := "eno1"
namespace := "ns10"
vlan_id := "10"
gateway := "192.168.10.1"
mask := "24"
bind_ip := "192.168.10.2"
bind_port := "12345"
target_ip := "192.168.20.2"
target_port := "12345"

build:
    gcc -xc -Wall -Wextra -Werror -std=c23 -fuse-ld=mold -fsanitize=address -g -D_GNU_SOURCE main.c -o namespace

_build_no_asan:
    gcc -xc -Wall -Wextra -Werror -std=c23 -fuse-ld=mold -g -D_GNU_SOURCE main.c -o namespace

run: build
    sudo ./namespace {{ interface }} {{ namespace }} {{ vlan_id }} {{ gateway }} {{ bind_ip }} {{ mask }} {{ bind_port }} {{ target_ip }} {{ target_port }}

valgrind: _build_no_asan
    sudo valgrind -s --leak-check=full ./namespace {{ interface }} {{ namespace }} {{ vlan_id }} {{ gateway }} {{ bind_ip }} {{ mask }} {{ bind_port }} {{ target_ip }} {{ target_port }}
