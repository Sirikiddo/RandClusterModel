class Settings:
    def __init__(self):    
        self.loadTest = True

        #0 - shaedule
        #1 - concurent
        self.protocol = 1

        self.rho = 1
        self.seed = 20
        #absolute size in km
        self.size = [20, 20]
        self.center = [0,0]
        self.range = [[self.center[0] - self.size[0]/2, self.center[0] + self.size[0]/2], 
                      [self.center[1] - self.size[1]/2, self.center[1] + self.size[1]/2]]
        
        self.pointGenType = 3
        self.f_val = 40.
        self.relability = 0.05
        self.probabilityFuncType = 2
        self.calculateGraphMatrix = True
        
        #for nodes whith message, target nodes
        self.margin = 0.1




