def netmp_sim(n = 4, party = 0, localhost = False):
    ios = [-1 for i in range(n)]
    ios2 = [-1 for i in range(n)]
    
    for i in range(n):
        for j in range(i+1,n):
            if i==party:
                if localhost:
                    ios[j] = 2*(i*n+j)
                else:
                    ios[j] = 2*i
                    
                if localhost:
                    ios2[j] = 2*(i*n+j)
                else:
                    ios2[j] = 2*i
            elif j==party:
                if localhost:
                    ios[i] = 2*(i*n+j)
                else:
                    ios[i] = 2*i
                
                if localhost:
                    ios2[i] = 2*(i*n+j)
                else:
                    ios2[i] = 2*i
    return ios, ios2

for i in range(4):
    ios,ios2= netmp_sim(4,i,False)
    print(ios)
    print(ios2)
    print()
                
                