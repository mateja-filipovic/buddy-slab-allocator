#!/bin/sh
# Copies the allocator core and one xv6 port into an xv6 source tree,
# or packages the submission archive.
#
#   port/xv6/install.sh <xv6-dir> user [public-test.c]
#   port/xv6/install.sh <xv6-dir> kernel
#   port/xv6/install.sh package <out.zip>
set -e

here=$(cd "$(dirname "$0")/../.." && pwd)
core="$here/src/buddy.c $here/src/cache.c $here/src/slab.c"

if [ "$1" = "package" ]; then
	out=${2:?output zip path}
	(cd "$here" && zip -q -r "$out" inc src/buddy.c src/cache.c src/slab.c port/xv6-user port/xv6-kernel port/xv6/install.sh)
	echo "wrote $out"
	exit 0
fi

xv6=${1:?xv6 directory}
mode=${2:?user or kernel}
[ -d "$xv6/kernel" ] || { echo "$xv6 does not look like an xv6 tree" >&2; exit 1; }

case "$mode" in
user)
	dst="$xv6/user"
	cp "$here"/inc/*.h $core "$here/port/xv6-user/kmem_port.h" "$dst/"
	if [ -n "$3" ]; then
		{
			printf '#include "kernel/types.h"\n#include "user/user.h"\n#include "slab.h"\n\n'
			awk '{
				sub(/\r$/, "");
				if ($0 ~ /^void main\(\)/) print "int main() {";
				else if ($0 ~ /^[ \t]*free\(space\);[ \t]*$/) { print; print "    return 0;" }
				else print
			}' "$3"
		} > "$dst/javni_test.c"
		echo "Adapted $3 into $dst/javni_test.c"
	fi
	cat <<MAKE
Copied the allocator into $dst.

Add to the Makefile, after the UPROGS list:

UPROGS += \$U/_javni_test

\$U/_javni_test: \$U/javni_test.o \$U/buddy.o \$U/cache.o \$U/slab.o \$(ULIB) \$U/user.ld
	\$(LD) \$(LDFLAGS) -T \$U/user.ld -o \$@ \$(filter %.o,\$^)

Then run: make qemu
At the xv6 prompt run: javni_test
MAKE
	;;
kernel)
	dst="$xv6/kernel"
	cp "$here"/inc/*.h $core "$here/port/xv6-kernel/kmem_port.h" "$dst/"
	cat <<MAKE
Copied the allocator into $dst.

Add to the OBJS list in the Makefile:

  \$K/buddy.o \\
  \$K/cache.o \\
  \$K/slab.o \\

Add to kernel/defs.h:

  void            kmem_init(void*, int);

In kernel/kalloc.c, give the allocator a page range before the page
allocator takes the rest. In kinit():

  #define KMEM_BLOCKS 1024
  char *kmem_space = (char *)PGROUNDUP((uint64)end);
  kmem_init(kmem_space, KMEM_BLOCKS);
  freerange(kmem_space + KMEM_BLOCKS * PGSIZE, (void *)PHYSTOP);

Kernel files that use the allocator include "slab.h". Do not include it
from kalloc.c: the port header renames kfree to kmem_kfree, because the
kernel already has a page-level kfree.
MAKE
	;;
*)
	echo "mode must be user or kernel" >&2
	exit 1
	;;
esac
