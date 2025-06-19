

f = open("measurements10k.txt", "r+", encoding="utf-8")

contents = f.read()
contents = contents.replace('\0', ';')
f.write(contents)
