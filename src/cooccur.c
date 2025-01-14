//  Tool to calculate word-word cooccurrence statistics
//
//  Copyright (c) 2014, 2018 The Board of Trustees of
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
#include <math.h>
#include "common.h"

typedef struct cooccur_rec_id {
    int word1;
    int word2;
    real val;
    int id;
} CRECID;

int verbose = 2; // 0, 1, or 2
long long max_product; // Cutoff for product of word frequency ranks below which cooccurrence counts will be stored in a compressed full array
long long overflow_length; // Number of cooccurrence records whose product exceeds max_product to store in memory before writing to disk
int window_size = 15; // default context window size
int symmetric = 1; // 0: asymmetric, 1: symmetric
real memory_limit = 3; // soft limit, in gigabytes, used to estimate optimal array sizes
int distance_weighting = 1; // Flag to control the distance weighting of cooccurrence counts
char *vocab_file, *file_head;

/* Search hash table for given string, return record if found, else NULL */
HASHREC *hashsearch(HASHREC **ht, char *w) {
    HASHREC     *htmp, *hprv;
    unsigned int hval = HASHFN(w, TSIZE, SEED);
    for (hprv = NULL, htmp=ht[hval]; htmp != NULL && scmp(htmp->word, w) != 0; hprv = htmp, htmp = htmp->next);
    if ( htmp != NULL && hprv!=NULL ) { // move to front on access
        hprv->next = htmp->next;
        htmp->next = ht[hval];
        ht[hval] = htmp;
    }
    return(htmp);
}

/* Insert string in hash table, check for string duplicates which should be absent */
void hashinsert(HASHREC **ht, char *w, long long id) {
    HASHREC     *htmp, *hprv;
    unsigned int hval = HASHFN(w, TSIZE, SEED);
    for (hprv = NULL, htmp = ht[hval]; htmp != NULL && scmp(htmp->word, w) != 0; hprv = htmp, htmp = htmp->next);
    if (htmp == NULL) {
        htmp = (HASHREC *) malloc(sizeof(HASHREC));
        htmp->word = (char *) malloc(strlen(w) + 1);
        strcpy(htmp->word, w);
        htmp->num = id;
        htmp->next = NULL;
        if (hprv == NULL) ht[hval] = htmp;
        else hprv->next = htmp;
    }
    else fprintf(stderr, "Error, duplicate entry located: %s.\n",htmp->word);
    return;
}

/* Write sorted chunk of cooccurrence records to file, accumulating duplicate entries */
int write_chunk(CREC *cr, long long length, FILE *fout) {
    if (length == 0) return 0;

    long long a = 0;
    CREC old = cr[a];
    
    for (a = 1; a < length; a++) {
        if (cr[a].word1 == old.word1 && cr[a].word2 == old.word2) {
            old.val += cr[a].val;
            continue;
        }
        fwrite(&old, sizeof(CREC), 1, fout);
        old = cr[a];
    }
    fwrite(&old, sizeof(CREC), 1, fout);
    return 0;
}

/* Check if two cooccurrence records are for the same two words, used for qsort */
int compare_crec(const void *a, const void *b) {
    int c;
    if ( (c = ((CREC *) a)->word1 - ((CREC *) b)->word1) != 0) return c;
    else return (((CREC *) a)->word2 - ((CREC *) b)->word2);
}

/* Check if two cooccurrence records are for the same two words */
int compare_crecid(CRECID a, CRECID b) {
    int c;
    if ( (c = a.word1 - b.word1) != 0) return c;
    else return a.word2 - b.word2;
}

/* Swap two entries of priority queue */
void swap_entry(CRECID *pq, int i, int j) {
    CRECID temp = pq[i];
    pq[i] = pq[j];
    pq[j] = temp;
}

/* Insert entry into priority queue */
void insert(CRECID *pq, CRECID new, int size) {
    int j = size - 1, p;
    pq[j] = new;
    while ( (p=(j-1)/2) >= 0 ) {
        if (compare_crecid(pq[p],pq[j]) > 0) {swap_entry(pq,p,j); j = p;}
        else break;
    }
}

/* Delete entry from priority queue */
void delete(CRECID *pq, int size) {
    int j, p = 0;
    pq[p] = pq[size - 1];
    while ( (j = 2*p+1) < size - 1 ) {
        if (j == size - 2) {
            if (compare_crecid(pq[p],pq[j]) > 0) swap_entry(pq,p,j);
            return;
        }
        else {
            if (compare_crecid(pq[j], pq[j+1]) < 0) {
                if (compare_crecid(pq[p],pq[j]) > 0) {swap_entry(pq,p,j); p = j;}
                else return;
            }
            else {
                if (compare_crecid(pq[p],pq[j+1]) > 0) {swap_entry(pq,p,j+1); p = j + 1;}
                else return;
            }
        }
    }
}

/* Write top node of priority queue to file, accumulating duplicate entries */
int merge_write(CRECID new, CRECID *old, FILE *fout) {
    if (new.word1 == old->word1 && new.word2 == old->word2) {
        old->val += new.val;
        return 0; // Indicates duplicate entry
    }
    fwrite(old, sizeof(CREC), 1, fout);
    *old = new;
    return 1; // Actually wrote to file
}

/* Merge [num] sorted files of cooccurrence records */
int merge_files(int num) {
    int i, size;
    long long counter = 0;
    CRECID *pq, new, old;
    char filename[200];
    FILE **fid, *fout;
    fid = calloc(num, sizeof(FILE));
    pq = malloc(sizeof(CRECID) * num);
    fout = stdout;
    if (verbose > 1) fprintf(stderr, "Merging cooccurrence files: processed 0 lines.");
    
    /* Open all files and add first entry of each to priority queue */
    for (i = 0; i < num; i++) {
        sprintf(filename,"%s_%04d.bin",file_head,i);
        fid[i] = fopen(filename,"rb");
        if (fid[i] == NULL) {log_file_loading_error("file", filename); free_fid(fid, num); free(pq); return 1;}
        fread(&new, sizeof(CREC), 1, fid[i]);
        new.id = i;
        insert(pq,new,i+1);
    }
    
    /* Pop top node, save it in old to see if the next entry is a duplicate */
    size = num;
    old = pq[0];
    i = pq[0].id;
    delete(pq, size);
    fread(&new, sizeof(CREC), 1, fid[i]);
    if (feof(fid[i])) size--;
    else {
        new.id = i;
        insert(pq, new, size);
    }
    
    /* Repeatedly pop top node and fill priority queue until files have reached EOF */
    while (size > 0) {
        counter += merge_write(pq[0], &old, fout); // Only count the lines written to file, not duplicates
        if ((counter%100000) == 0) if (verbose > 1) fprintf(stderr,"\033[39G%lld lines.",counter);
        i = pq[0].id;
        delete(pq, size);
        fread(&new, sizeof(CREC), 1, fid[i]);
        if (feof(fid[i])) size--;
        else {
            new.id = i;
            insert(pq, new, size);
        }
    }
    fwrite(&old, sizeof(CREC), 1, fout);
    fprintf(stderr,"\033[0GMerging cooccurrence files: processed %lld lines.\n",++counter);
    for (i=0;i<num;i++) {
        sprintf(filename,"%s_%04d.bin",file_head,i);
        remove(filename);
    }
    fprintf(stderr,"\n");
    free_fid(fid, num);
    free(pq);
    return 0;
}

void free_resources(HASHREC** vocab_hash, CREC *cr, long long *lookup, real *bigram_table) {
    free_table(vocab_hash);
    free(cr);
    free(lookup);
    free(bigram_table);
}

void count_occour(long long target_freq_rank, long long context_freq_rank, real cntxt_weight, long long *lookup, CREC *cr, long long *ind, real *bigram_table) {
    if (verbose > 2) fprintf(stderr, "Adding cooccur between words %lld and %lld.\n", context_freq_rank, target_freq_rank);

    if ( context_freq_rank < max_product / target_freq_rank ) { 
        // Product is small enough to store in a full array
        // Weight by inverse of distance between words if needed
        bigram_table[lookup[context_freq_rank - 1] + target_freq_rank - 2] += cntxt_weight; 
        if (symmetric > 0){
            // If symmetric context is used, exchange roles of w2 and w1 (ie look at right context too)
            bigram_table[lookup[target_freq_rank - 1] + context_freq_rank - 2] += cntxt_weight; 
        }
    }
    else { 
        // Entries in which the frequency product is too big are likely to be sparse
        // These are probably two not-so-frequent words occouring together; it isnt efficient to keep this in bigram table given sparseness
        // Store these entries in a temporary buffer to be sorted, merged (accumulated), and written to file when it gets full.
        cr[*ind].word1 = context_freq_rank;
        cr[*ind].word2 = target_freq_rank;
        cr[*ind].val = cntxt_weight;
        *ind = *ind + 1; // Keep track of how full temporary buffer is
        if (symmetric > 0) { // Symmetric context, adds both ways
            cr[*ind].word1 = target_freq_rank;
            cr[*ind].word2 = context_freq_rank;
            cr[*ind].val = cntxt_weight;
            *ind = *ind + 1;
        }
    }
}

void count_context(char *str, char *sub_str, int j, char history[][MAX_STRING_LENGTH + 1], long long *lookup, CREC *cr, long long *ind, real *bigram_table, HASHREC** vocab_hash) {
    long long w1, w2, k, l, i;
    real cntxt_weight;
    HASHREC *htmp1, *htmp2;

    htmp1 = hashsearch(vocab_hash, str);
    char *context_str;



    if (htmp1 == NULL) { // Skip out-of-vocabulary words
        if (verbose > 2) fprintf(stderr, "Not getting coocurs as word not in vocab\n");
        // Adds to history anyway since subtokens may be used
        strcpy(history[j % window_size], str);
        return; 
    }
    w1 = htmp1->num; // Target word (frequency rank)
    
    // Iterate over all words to the left of target word, but not past beginning of line
    // If token is phrase, iterates also over its subtokens (actually tokens)
    for (k = j - 1; k >= ( (j > window_size) ? j - window_size : 0 ); k--) { 
        cntxt_weight = distance_weighting ? (1.0/(real)(j-k)) : 1.0;

        context_str = history[k % window_size]; // Context word

        htmp2 = hashsearch(vocab_hash, context_str);
        if (htmp2 != NULL) { // Process only words in vocabulary
            w2 = htmp2->num; // Context word (frequency rank)
            count_occour(w1, w2, cntxt_weight, lookup, cr, ind, bigram_table);
        }
        
        // For each context_str subtoken, call count_occour if it isnt OOV
        if (strchr(context_str, SEP_CHAR) != NULL) {
            for (l = 0, i = 0; context_str[i]; i++, l++) {
                if (context_str[i] == SEP_CHAR) {
                    sub_str[l] = '\0';
                    htmp2 = hashsearch(vocab_hash, sub_str);
                    if (htmp2 != NULL) {
                        w2 = htmp2->num;
                        count_occour(w1, w2, cntxt_weight, lookup, cr, ind, bigram_table);
                    }
                    l = -1;
                }
                else {
                    sub_str[l] = context_str[i];
                }
            }
        }
    }

    // Target word is stored in circular buffer to become context word in the future
    strcpy(history[j % window_size], str);
}

/* Collect word-word cooccurrence counts from input stream */
int get_cooccurrence() {
    int flag, x, y, fidcounter = 1;
    long long a, j = 0, id, counter = 0, ind = 0, vocab_size, *lookup = NULL;
    char format[20], filename[200], str[MAX_STRING_LENGTH + 1];
    char history[window_size][MAX_STRING_LENGTH + 1], sub_str[MAX_STRING_LENGTH + 1];
    FILE *fid, *foverflow;
    real *bigram_table = NULL, r;
    HASHREC **vocab_hash = inithashtable();
    CREC *cr = malloc(sizeof(CREC) * (overflow_length + 1));
    
    fprintf(stderr, "COUNTING COOCCURRENCES\n");
    if (verbose > 0) {
        fprintf(stderr, "window size: %d\n", window_size);
        if (symmetric == 0) fprintf(stderr, "context: asymmetric\n");
        else fprintf(stderr, "context: symmetric\n");
    }
    if (verbose > 1) fprintf(stderr, "max product: %lld\n", max_product);
    if (verbose > 1) fprintf(stderr, "overflow length: %lld\n", overflow_length);
    sprintf(format,"%%%ds %%lld", MAX_STRING_LENGTH); // Format to read from vocab file, which has (irrelevant) frequency data
    if (verbose > 1) fprintf(stderr, "Reading vocab from file \"%s\"...", vocab_file);
    fid = fopen(vocab_file,"r");
    if (fid == NULL) { 
        log_file_loading_error("vocab file", vocab_file);
        free_resources(vocab_hash, cr, lookup, bigram_table);
        return 1;
    }
    while (fscanf(fid, format, str, &id) != EOF){
        // Here id is not used: inserting vocab words into hash table with their frequency rank, j
        // vocab_file is a list of (word, count) entries, sorted non-ascending by count
        hashinsert(vocab_hash, str, ++j); 
    }
        
    fclose(fid);
    vocab_size = j;
    j = 0;
    if (verbose > 1) fprintf(stderr, "loaded %lld words.\nBuilding lookup table...", vocab_size);
    
    /* Build auxiliary lookup table used to index into bigram_table */
    lookup = (long long *)calloc( vocab_size + 1, sizeof(long long) );
    if (lookup == NULL) {
        fprintf(stderr, "Couldn't allocate memory!");
        free_resources(vocab_hash, cr, lookup, bigram_table);
        return 1;
    }
    lookup[0] = 1;
    // lookup[a]: lookup[a - 1] + min(max_product/a, vocab_size)
    
    // this value is an offset for the row in bigram table from freqrank a
    // bigram table isnt a square matrix, some rows have a non-full length; 
    // lookup keeps an accumulated sum for such lenghts
    // higher max_product, more rare freqrank combinations we will see; more volatile memory needs to be used
    for (a = 1; a <= vocab_size; a++) {
        if ((lookup[a] = max_product / a) < vocab_size) lookup[a] += lookup[a-1];
        else lookup[a] = lookup[a-1] + vocab_size;
    }
    if (verbose > 1) fprintf(stderr, "table contains %lld elements.\n",lookup[a-1]);
    
    /* Allocate memory for full array which will store all cooccurrence counts for words whose product of frequency ranks is less than max_product */
    bigram_table = (real *)calloc( lookup[a-1] , sizeof(real) );
    if (bigram_table == NULL) {
        fprintf(stderr, "Couldn't allocate memory!");
        free_resources(vocab_hash, cr, lookup, bigram_table);
        return 1;
    }
    
    fid = stdin;
    // sprintf(format,"%%%ds",MAX_STRING_LENGTH);
    sprintf(filename,"%s_%04d.bin", file_head, fidcounter);
    foverflow = fopen(filename,"wb");
    if (verbose > 1) fprintf(stderr,"Processing token: 0");

    // if symmetric > 0, we can increment ind twice per iteration,
    // meaning up to 2x window_size in one loop
    int overflow_threshold = symmetric == 0 ? overflow_length - window_size : overflow_length - 2 * window_size;
    
    /* For each token in input stream, calculate a weighted cooccurrence sum within window_size */
    while (1) {
        if (ind >= overflow_threshold) {
            // If overflow buffer is (almost) full, sort it and write it to temporary file
            qsort(cr, ind, sizeof(CREC), compare_crec);
            write_chunk(cr,ind,foverflow);
            fclose(foverflow);
            fidcounter++;
            sprintf(filename,"%s_%04d.bin",file_head,fidcounter);
            foverflow = fopen(filename,"wb");
            ind = 0;
        }
        flag = get_word(str, fid);
        if (verbose > 2) fprintf(stderr, "Maybe processing token: %s\n", str);
        if (flag == 1) {
            // Newline, reset line index (j); maybe eof.
            if (feof(fid)) {
                if (verbose > 2) fprintf(stderr, "Not getting coocurs as at eof\n");
                break;
            }
            j = 0;
            if (verbose > 2) fprintf(stderr, "Not getting coocurs as at newline\n");
            continue;
        }
        counter++;
        count_context(str, sub_str, j, history, lookup, cr, &ind, bigram_table, vocab_hash);
        if ((counter%100000) == 0){
            if (verbose > 1) fprintf(stderr,"\033[19G%lld",counter);
        }
        j++;
    }
    
    /* Write out temp buffer for the final time (it may not be full) */
    if (verbose > 1) fprintf(stderr,"\033[0GProcessed %lld tokens.\n",counter);
    qsort(cr, ind, sizeof(CREC), compare_crec);
    write_chunk(cr,ind,foverflow);
    sprintf(filename,"%s_0000.bin",file_head);
    
    /* Write out full bigram_table, skipping zeros */
    if (verbose > 1) fprintf(stderr, "Writing cooccurrences to disk");
    fid = fopen(filename,"wb");
    j = 1e6;
    for (x = 1; x <= vocab_size; x++) {
        if ( (long long) (0.75*log(vocab_size / x)) < j) {
            j = (long long) (0.75*log(vocab_size / x));
            if (verbose > 1) fprintf(stderr,".");
        } // log's to make it look (sort of) pretty
        for (y = 1; y <= (lookup[x] - lookup[x-1]); y++) { //(lookup[x] - lookup[x-1]) size of xth row
            if ((r = bigram_table[lookup[x-1] - 2 + y]) != 0) {
                fwrite(&x, sizeof(int), 1, fid);
                fwrite(&y, sizeof(int), 1, fid);
                fwrite(&r, sizeof(real), 1, fid);
            }
        }
    }
    
    if (verbose > 1) fprintf(stderr,"%d files in total.\n",fidcounter + 1);
    fclose(fid);
    fclose(foverflow);
    free_resources(vocab_hash, cr, lookup, bigram_table);
    return merge_files(fidcounter + 1); // Merge the sorted temporary files
}

int main(int argc, char **argv) {
    int i;
    real rlimit, n = 1e5;
    vocab_file = malloc(sizeof(char) * MAX_STRING_LENGTH);
    file_head = malloc(sizeof(char) * MAX_STRING_LENGTH);
    
    if (argc == 1) {
        printf("Tool to calculate word-word cooccurrence statistics\n");
        printf("Author: Jeffrey Pennington (jpennin@stanford.edu)\n\n");
        printf("Usage options:\n");
        printf("\t-verbose <int>\n");
        printf("\t\tSet verbosity: 0, 1, 2 (default), or 3\n");
        printf("\t-symmetric <int>\n");
        printf("\t\tIf <int> = 0, only use left context; if <int> = 1 (default), use left and right\n");
        printf("\t-window-size <int>\n");
        printf("\t\tNumber of context words to the left (and to the right, if symmetric = 1); default 15\n");
        printf("\t-vocab-file <file>\n");
        printf("\t\tFile containing vocabulary (truncated unigram counts, produced by 'vocab_count'); default vocab.txt\n");
        printf("\t-memory <float>\n");
        printf("\t\tSoft limit for memory consumption, in GB -- based on simple heuristic, so not extremely accurate; default 4.0\n");
        printf("\t-max-product <int>\n");
        printf("\t\tLimit the size of dense cooccurrence array by specifying the max product <int> of the frequency counts of the two cooccurring words.\n\t\tThis value overrides that which is automatically produced by '-memory'. Typically only needs adjustment for use with very large corpora.\n");
        printf("\t-overflow-length <int>\n");
        printf("\t\tLimit to length <int> the sparse overflow array, which buffers cooccurrence data that does not fit in the dense array, before writing to disk. \n\t\tThis value overrides that which is automatically produced by '-memory'. Typically only needs adjustment for use with very large corpora.\n");
        printf("\t-overflow-file <file>\n");
        printf("\t\tFilename, excluding extension, for temporary files; default overflow\n");
        printf("\t-distance-weighting <int>\n");
        printf("\t\tIf <int> = 0, do not weight cooccurrence count by distance between words; if <int> = 1 (default), weight the cooccurrence count by inverse of distance between words\n");

        printf("\nExample usage:\n");
        printf("./cooccur -verbose 2 -symmetric 0 -window-size 10 -vocab-file vocab.txt -memory 8.0 -overflow-file tempoverflow < corpus.txt > cooccurrences.bin\n\n");
        free(vocab_file);
        free(file_head);
        return 0;
    }

    if ((i = find_arg((char *)"-verbose", argc, argv)) > 0) verbose = atoi(argv[i + 1]);
    if ((i = find_arg((char *)"-symmetric", argc, argv)) > 0) symmetric = atoi(argv[i + 1]);
    if ((i = find_arg((char *)"-window-size", argc, argv)) > 0) window_size = atoi(argv[i + 1]);
    if ((i = find_arg((char *)"-vocab-file", argc, argv)) > 0) strcpy(vocab_file, argv[i + 1]);
    else strcpy(vocab_file, (char *)"vocab.txt");
    if ((i = find_arg((char *)"-overflow-file", argc, argv)) > 0) strcpy(file_head, argv[i + 1]);
    else strcpy(file_head, (char *)"overflow");
    if ((i = find_arg((char *)"-memory", argc, argv)) > 0) memory_limit = atof(argv[i + 1]);
    if ((i = find_arg((char *)"-distance-weighting", argc, argv)) > 0)  distance_weighting = atoi(argv[i + 1]);
    
    /* The memory_limit determines a limit on the number of elements in bigram_table and the overflow buffer */
    /* Estimate the maximum value that max_product can take so that this limit is still satisfied */
    rlimit = 0.85 * (real)memory_limit * 1073741824/(sizeof(CREC));
    while (fabs(rlimit - n * (log(n) + 0.1544313298)) > 1e-3) n = rlimit / (log(n) + 0.1544313298);
    max_product = (long long) n;
    overflow_length = (long long) rlimit/6; // 0.85 + 1/6 ~= 1
    
    /* Override estimates by specifying limits explicitly on the command line */
    if ((i = find_arg((char *)"-max-product", argc, argv)) > 0) max_product = atoll(argv[i + 1]);
    if ((i = find_arg((char *)"-overflow-length", argc, argv)) > 0) overflow_length = atoll(argv[i + 1]);
    
    const int returned_value = get_cooccurrence();
    free(vocab_file);
    free(file_head);
    return returned_value;
}

