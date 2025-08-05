count = 0
NUM_PARTIES = 7
def remain(i,j,k):
    L = []
    for num in {0,1,2,3,4,5,6}:
        if num != i and num != j and num != k:
            L.append(num)
    return L[0:3]
for i in range(NUM_PARTIES):
    for j in range(i+1, NUM_PARTIES):
        for k in range(j+1, NUM_PARTIES):
            L = remain(i,j,k)
            l = L[0]
            m = L[1]
            n = L[2]
            print("i="+str(i)+", j="+str(j)+", k="+str(k)+", l="+str(l)+", m="+str(m)+", n="+str(n))
