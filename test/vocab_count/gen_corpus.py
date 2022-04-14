import random

CORPUS_SIZE = 1000000
TOKEN_VOCAB_SIZE = 10000
PHRASE_VOCAB_SIZE = 500
MAX_PHRASE_SIZE = 5
CHANCE_LINE_BREAK = 50

def get_tokens(num_tokens):
    return list(range(num_tokens))

def get_random_token(tokens):
    x = random.choice(tokens)
    if x % 2 == 0:
        return x
    else:
        if x % 3 == 0:
            if random.randint(1, 2) == 1:
                return x
            else:
                return x - 1
        else:
            if random.randint(1, 3) == 1:
                return x
            else:
                return x - 1

def get_phrases(tokens, num_phrases):
    phrases = set()
    while len(phrases) < num_phrases:
        phrase_size = random.randint(1, MAX_PHRASE_SIZE)
        phrase = [get_random_token(tokens) for _ in range(phrase_size)]
        phrases.add(tuple(phrase))
    return list(phrases)


tokens = get_tokens(TOKEN_VOCAB_SIZE)
phrases = get_phrases(tokens, PHRASE_VOCAB_SIZE)

counter = dict()
with open("tmp.txt", "w") as wf:
    for _ in range(CORPUS_SIZE):
        x = None
        if random.randint(1, TOKEN_VOCAB_SIZE + PHRASE_VOCAB_SIZE) <= TOKEN_VOCAB_SIZE:
            x = get_random_token(tokens)
        else:
            x = random.choice(phrases)

        if type(x) is int:
            x = str(x)
        else:
            x = "_".join([str(y) for y in x]) + "_"

        wf.write(x)
        if not x in counter: counter[x] = 0
        counter[x] += 1
        if random.randint(1, CHANCE_LINE_BREAK) == 1:
            wf.write("\n")
        else:
            wf.write(" ")

counter = sorted(list(counter.items()))
with open("correct_vocab_count.txt", "w") as wf:
    for v, c in counter:
        wf.write(f"{v} {c}\n")