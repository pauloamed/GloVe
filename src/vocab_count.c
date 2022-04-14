//  Tool to extract unigram counts
//
//  GloVe: Global Vectors for Word Representation
//  Copyright (c) 2014 The Board of Trustees of
//  The Leland Stanford Junior University. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//
//
//  For more information, bug reports, fixes, contact:
//    Jeffrey Pennington (jpennin@stanford.edu)
//    Christopher Manning (manning@cs.stanford.edu)
//    https://github.com/stanfordnlp/GloVe/
//    GlobalVectors@googlegroups.com
//    http://nlp.stanford.edu/projects/glove/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

typedef struct vocabulary {
    char *word;
    long long count;
} VOCAB;

int verbose = 2; // 0, 1, or 2
long long min_count = 1; // min occurrences for inclusion in vocab
long long max_vocab = 0; // max_vocab = 0 for no limit


/* Vocab frequency comparison; break ties alphabetically */
int CompareVocabTie(const void *a, const void *b) {
    long long c;
    if ( (c = ((VOCAB *) b)->count - ((VOCAB *) a)->count) != 0) return ( c > 0 ? 1 : -1 );
    else return (scmp(((VOCAB *) a)->word,((VOCAB *) b)->word));
    
}

/* Vocab frequency comparison; no tie-breaker */
int CompareVocab(const void *a, const void *b) {
    long long c;
    if ( (c = ((VOCAB *) b)->count - ((VOCAB *) a)->count) != 0) return ( c > 0 ? 1 : -1 );
    else return 0;
}

/* Search hash table for given string, insert if not found */
void hashinsert(HASHREC **ht, char *w) {
    HASHREC     *htmp, *hprv;
    unsigned int str_hash_value = HASHFN(w, TSIZE, SEED);
    
    // htmp: current pointer, hprv: previous pointer
    hprv = NULL, htmp = ht[str_hash_value];
    while(htmp != NULL && scmp(htmp->word, w) != 0) { // searching for string in its bucket
        hprv = htmp, htmp = htmp->next; // walking both pointers at once
    }
    if (htmp == NULL) { // string isnt in bucket, adding now
        htmp = (HASHREC *) malloc( sizeof(HASHREC) );
        htmp->word = (char *) malloc( strlen(w) + 1 );
        strcpy(htmp->word, w);
        htmp->num = 1;
        htmp->next = NULL;
        if (hprv == NULL)
            ht[str_hash_value] = htmp;
        else
            hprv->next = htmp;
        /* new records are not moved to front */
    }
    else { // string is in bucket, increments counter and moves to front
        htmp->num++;
        if (hprv != NULL) { // isnt an empty list
            /* moves to first position of linked list */
            hprv->next = htmp->next;
            htmp->next = ht[str_hash_value];
            ht[str_hash_value] = htmp;
        }
    }
    return;
}

/* Search and, if found, increment */
void hashincrement(HASHREC **ht, char *w, int value) {
    HASHREC     *htmp, *hprv;
    unsigned int str_hash_value = HASHFN(w, TSIZE, SEED);
    
    // htmp: current pointer, hprv: previous pointer
    hprv = NULL, htmp = ht[str_hash_value];
    while(htmp != NULL && scmp(htmp->word, w) != 0){ // searching for string in its bucket
        hprv = htmp, htmp = htmp->next; // walking both pointers at once
    }
    if (htmp != NULL) { // wont increment non existent entries
        htmp->num += value;
        if (hprv != NULL) { // isnt an empty list
            /* moves to first position of linked list */
            hprv->next = htmp->next;
            htmp->next = ht[str_hash_value];
            ht[str_hash_value] = htmp;
        }
    }
    return;
}

int get_counts() {
    long long i = 0, j = 0, k = 0, vocab_size = 12500;
    // char format[20];
    char str[MAX_STRING_LENGTH + 1], sub_str[MAX_STRING_LENGTH + 1];
    HASHREC **vocab_hash = inithashtable();
    HASHREC *htmp;
    VOCAB *vocab;
    FILE *fid = stdin;
    
    fprintf(stderr, "BUILDING VOCABULARY\n");
    if (verbose > 1) fprintf(stderr, "Processed %lld tokens.", i);
    // sprintf(format,"%%%ds",MAX_STRING_LENGTH);
    while ( ! feof(fid)) {
        // Insert all tokens into hashtable
        int nl = get_word(str, fid);
        if (nl) continue; // just a newline marker or feof
        if (strcmp(str, "<unk>") == 0) {
            fprintf(stderr, "\nError, <unk> vector found in corpus.\nPlease remove <unk>s from your corpus (e.g. cat text8 | sed -e 's/<unk>/<raw_unk>/g' > text8.new)");
            free_table(vocab_hash);
            return 1;
        }
        hashinsert(vocab_hash, str);
        if (((++i)%100000) == 0) if (verbose > 1) fprintf(stderr,"\033[11G%lld tokens.", i);
    }
    if (verbose > 1) fprintf(stderr, "\033[0GProcessed %lld tokens.\n", i);

    // increment occorences of subtokens from MWTs (separated by SEP_CHAR)
    // bipartite DAG, no specific token processing order is needed
    // if subtokens exists, increments counts; if it doesnt, skips
    for (i = 0; i < TSIZE; i++) {
        htmp = vocab_hash[i];
        while (htmp != NULL) {
            if (strchr(htmp->word, SEP_CHAR) != NULL){
                k = 0;
                for (j = 0; htmp->word[j]; j++, k++) {
                    if (htmp->word[j] == SEP_CHAR) {
                        sub_str[k] = '\0';
                        hashincrement(vocab_hash, sub_str, htmp->num);
                        k = -1;
                    }
                    else {
                        sub_str[k] = htmp->word[j];
                    }
                }
            }
            htmp = htmp->next;
        }
    }

    vocab = malloc(sizeof(VOCAB) * vocab_size);
    for (i = 0, j = 0; i < TSIZE; i++) { // Migrate vocab to array
        htmp = vocab_hash[i];
        while (htmp != NULL) {
            vocab[j].word = htmp->word;
            vocab[j].count = htmp->num;
            j++;
            if (j>=vocab_size) {
                vocab_size += ARRAY_SIZE_INCREMENT;
                vocab = (VOCAB *)realloc(vocab, sizeof(VOCAB) * vocab_size);
            }
            htmp = htmp->next;
        }
    }
    if (verbose > 1) fprintf(stderr, "Counted %lld unique words.\n", j);
    if (max_vocab > 0 && max_vocab < j)
        // If the vocabulary exceeds limit, first sort full vocab by frequency without alphabetical tie-breaks.
        // This results in pseudo-random ordering for words with same frequency, so that when truncated, the words span whole alphabet
        qsort(vocab, j, sizeof(VOCAB), CompareVocab);
    else max_vocab = j;
    qsort(vocab, max_vocab, sizeof(VOCAB), CompareVocabTie); //After (possibly) truncating, sort (possibly again), breaking ties alphabetically
    
    for (i = 0; i < max_vocab; i++) {
        if (vocab[i].count < min_count) { // If a minimum frequency cutoff exists, truncate vocabulary
            if (verbose > 0) fprintf(stderr, "Truncating vocabulary at min count %lld.\n",min_count);
            break;
        }
        printf("%s %lld\n",vocab[i].word,vocab[i].count);
    }
    
    if (i == max_vocab && max_vocab < j) if (verbose > 0) fprintf(stderr, "Truncating vocabulary at size %lld.\n", max_vocab);
    fprintf(stderr, "Using vocabulary of size %lld.\n\n", i);
    free_table(vocab_hash);
    free(vocab);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 &&
        (!scmp(argv[1], "-h") || !scmp(argv[1], "-help") || !scmp(argv[1], "--help"))) {
        printf("Simple tool to extract unigram counts\n");
        printf("Author: Jeffrey Pennington (jpennin@stanford.edu)\n\n");
        printf("Usage options:\n");
        printf("\t-verbose <int>\n");
        printf("\t\tSet verbosity: 0, 1, or 2 (default)\n");
        printf("\t-max-vocab <int>\n");
        printf("\t\tUpper bound on vocabulary size, i.e. keep the <int> most frequent words. The minimum frequency words are randomly sampled so as to obtain an even distribution over the alphabet.\n");
        printf("\t-min-count <int>\n");
        printf("\t\tLower limit such that words which occur fewer than <int> times are discarded.\n");
        printf("\nExample usage:\n");
        printf("./vocab_count -verbose 2 -max-vocab 100000 -min-count 10 < corpus.txt > vocab.txt\n");
        return 0;
    }

    int i;
    if ((i = find_arg((char *)"-verbose", argc, argv)) > 0) verbose = atoi(argv[i + 1]);
    if ((i = find_arg((char *)"-max-vocab", argc, argv)) > 0) max_vocab = atoll(argv[i + 1]);
    if ((i = find_arg((char *)"-min-count", argc, argv)) > 0) min_count = atoll(argv[i + 1]);
    return get_counts();
}

