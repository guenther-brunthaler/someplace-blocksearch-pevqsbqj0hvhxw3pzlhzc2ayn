#! /bin/sh

# Run this script in order to generate the ./configure script!
#
# v2023.127

touch NEWS README AUTHORS ChangeLog
autoreconf -i
