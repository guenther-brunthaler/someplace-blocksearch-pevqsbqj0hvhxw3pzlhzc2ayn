#! /bin/sh

# Run this script in order to generate the ./configure script!
#
# v2023.127

set -e
trap 'test $? = 0 || echo "\"$0\" failed!" >& 2' 0

cd -- "`dirname -- "$0"`"
for f in NEWS README AUTHORS ChangeLog
do
	test -e $f && continue
	> "$f"
done
autoreconf -i
