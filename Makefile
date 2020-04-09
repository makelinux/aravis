m=build.$(shell uname --machine)

all:
	test -e $m/build.ninja || meson $m
	ninja -C $m

clean:
	meson setup --wipe $m
