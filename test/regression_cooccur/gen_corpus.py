import random

CORPUS_SIZE = 1000000
TOKEN_VOCAB_SIZE = 10000
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



tokens = get_tokens(TOKEN_VOCAB_SIZE) 

counter = dict()
with open("tmp.txt", "w") as wf:
    for _ in range(CORPUS_SIZE):
        x = get_random_token(tokens)
        x = str(x)

        wf.write(x)
        if not x in counter: counter[x] = 0
        counter[x] += 1
        if random.randint(1, CHANCE_LINE_BREAK) == 1:
            wf.write("\n")
        else:
            wf.write(" ")