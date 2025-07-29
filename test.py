def upperTriangularToArray(i, j):
  mn = min(i, j)
  mx = max(i, j)
  idx = (mx * (mx - 1)) / 2 + mn
  return idx

def particiant_i_j_have_common(a,b):
    for i in range(7):
        for j in range(i+1,7):
            if i!=a and i!=b and j!=a and j!= b:
                print(str(i)+", "+str(j))
NP = 7
id_ = 0
for i in range(NP):
    if((i+1)%NP == id_ or (i+2)%NP == id_ or (i+3)%NP == id_ or i == id_):
        print(str((i+1)%NP)+", "+str((i+2)%NP)+", "+str((i+3)%NP)+", "+str((i)%NP))
    if((i+4)%NP == id_ or (i+5)%NP == id_ or (i+6)%NP == id_ or i == id_):
        print(str((i+4)%NP)+", "+str((i+5)%NP)+", "+str((i+6)%NP)+", "+str((i)%NP))

          

