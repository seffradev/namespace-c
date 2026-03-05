# `namespace-c`

A minimal example reproducing a behavior I have with network
namespaces and VLAN:s, written in C.

## Dependencies

The easy way is to have Nix and to use the included flake.

Otherwise, the following list is recommended:
- `just`
- `mold`
- `clangd`
- `clang-format`
- `gcc`
- `valgrind`
- `iproute2`
- `gdb`

## Running

There are a couple available commands depending on how you want to
run the project. Use `just --list` to see them.

When you run with `just run`, the application will:
- Create a network namespace.
- Create a VLAN.
- Move the VLAN into the network namespace.
- Create a child process.
- Move the child into the network namespace.
- Create a UDP socket in the namespace.
- Send the file descriptor to the parent process.
- Start listening and sending on the socket.
