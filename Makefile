a:
	test -d build || meson build
	echo "make: Entering directory '${PWD}/build'"
	cd build && ninja 1>&2
