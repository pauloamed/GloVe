#!/bin/bash

(cd ../../ && make)
make

CORPUS=tmp.txt

NEW_VOCAB_FILE=new_vocab.txt
NEW_BUILDDIR=../../build
NEW_COOCCURRENCE_FILE=new_cooccurrence.bin

OLD_VOCAB_FILE=old_vocab.txt
OLD_BUILDDIR=build
OLD_COOCCURRENCE_FILE=old_cooccurrence.bin

VERBOSE=0
VOCAB_MIN_COUNT=3
MEMORY=6.0
WINDOW_SIZE=5

python gen_corpus.py

$NEW_BUILDDIR/vocab_count -min-count $VOCAB_MIN_COUNT -verbose $VERBOSE < $CORPUS > $NEW_VOCAB_FILE
$NEW_BUILDDIR/cooccur -memory $MEMORY -vocab-file $NEW_VOCAB_FILE -verbose $VERBOSE -window-size $WINDOW_SIZE < $CORPUS > $NEW_COOCCURRENCE_FILE

$OLD_BUILDDIR/vocab_count -min-count $VOCAB_MIN_COUNT -verbose $VERBOSE < $CORPUS > $OLD_VOCAB_FILE
$OLD_BUILDDIR/cooccur -memory $MEMORY -vocab-file $OLD_VOCAB_FILE -verbose $VERBOSE -window-size $WINDOW_SIZE < $CORPUS > $OLD_COOCCURRENCE_FILE

DIFF_VOCAB=$(diff new_vocab.txt old_vocab.txt);
DIFF_COOCCUR=$(diff new_cooccurrence.bin old_cooccurrence.bin);

if [ "$DIFF_VOCAB" == "" ];
then
    echo "Vocabs are identical! Regression ok"
else
    echo "Failed regression test on vocab_count"
fi

if [ "$DIFF_COOCCUR" == "" ];
then
    echo "Cooccur are identical! Regression ok"
else
    echo "Failed regression test on cooccur"
fi

rm tmp.txt
rm new_vocab.txt new_cooccurrence.bin
rm old_vocab.txt old_cooccurrence.bin 
rm build -r