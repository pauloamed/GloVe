def open_count(filename):
    count = []
    with open(filename) as rf:
        for line in rf.readlines():
            v, c = line.strip().split()
            count.append((v, c))
    return sorted(count)

a = open_count('vocab.txt')
b = open_count('correct_vocab_count.txt')

if a == b:
    print("PASS!")
else:
    print("NOT PASS")