#!/bin/bash
set -e

(cd ../../ && make)

CORPUS=tmp.txt
VOCAB_FILE=vocab.txt
BUILDDIR=../../build
VERBOSE=2
VOCAB_MIN_COUNT=3

python test.py

$BUILDDIR/vocab_count -min-count $VOCAB_MIN_COUNT -verbose $VERBOSE < $CORPUS > $VOCAB_FILE

python compare.py

rm correct_vocab_count.txt vocab.txt tmp.txt