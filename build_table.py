

print("struct number_lut_element{\n\tu16 E[16];\n};")
print("number_lut_element NumberLUT[] =\n{")

offsets = []
for p in range(2 ** 5):

    a = 1
    c = []
    found = False
    for i in reversed(range(5)):
        b = 2 ** i
        if (b & p) > 0:
            found = True
            offsets.append(i)
            c.append(a)
        else:
            c.append(0)
        if found:
            a = a * 10
    print("\t{", end="")
    for i in range(5):
        j = c[-i - 1]
        if i < 4:
            print(f"{j}, ", end="")
        else:
            print(f"{j}", end="")
    print("},", end="")
    print()
print("\n};")

print("u8 NumberLUT[] =\n{\n\t", end="")
for p in range(2 ** 5):
    print(f"{offsets[p]}", end="")
    if p < 2 ** 5 - 1:
        print(", ", end="")

print("\n};")
