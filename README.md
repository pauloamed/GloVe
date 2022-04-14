## GloVe: Global Vectors for Word Representation ~ Phrase support

Extension for handling phrases, to be separated with `SEP_CHAR`.
A phrase needs to be marked like:
```
// SEP_CHAR = '\1'
hot dog => hot\1dog\1
```

### Phrase adaptation
No methodological adaptation was needed, only modifications in token count (`vocab_count.c`) and cooccourence count (`cooccur.c`) were done. Some unsupported code from the original Glove paper had to be removed for repository consistency.

## Train word vectors on a new corpus

You can train word vectors on your own corpus. Adapt `demo.sh` for such.

    $ ./demo.sh


### License
All work contained in this package is licensed under the Apache License, Version 2.0. See the include LICENSE file.
