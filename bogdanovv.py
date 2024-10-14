import matplotlib.pyplot as plt
import numpy as np

plt.plot([1,1,2,2,1],[1,2,2,1,1])
a=set()
for i in range(10):
    plt.plot(((1+np.random.uniform())),(1+np.random.uniform()), 'ro')
plt.show()